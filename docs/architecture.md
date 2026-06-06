# voxel2026 — Architecture (June 2026)

A clean-break modernization architecture for a Luanti-class voxel engine, designed against the
Luanti 5.17-dev source tree and the June-2026 state of the art, and hardened by two adversarial
review rounds (Part VIII). Scope decisions that shaped it: **clean break allowed** (APIs, world
format, and protocol may be redesigned; old content gets a porting guide, not a guarantee) and
**desktop flagship first** (design target: RTX 4090-class; mobile/web out of scope).

Reference flagship hardware: RTX 4090 (24 GB VRAM; Ada: mesh shaders, HW ray tracing,
DLSS 4.5-capable, SER), i9-14900KF (8P+16E = 24 cores / 32 threads), 64 GB RAM, 3440×1440
high-refresh.

---

## Part I — Where the engine actually is today (survey of 9 subsystems)

**Rendering core (irr/ + src/client/shader.cpp):** Three GL drivers — GL 3.2 *Compatibility*
profile, GLES2, legacy GL 2.1. GLSL 150/300es/100/120 generated via `#define` string injection.
No instancing, no MultiDrawIndirect, no bindless, no compute shaders, no persistent mapped buffers
(`GL_MAP_PERSISTENT_BIT` defined, never used), compressed textures explicitly disabled. One
`setMaterial()` + draw call per mesh buffer. A 4090 is being driven like a 2009 GPU.

**World rendering (src/client/clientmap.cpp, content_mapblock.cpp):** Naive per-node face emission
(no greedy meshing), 38-byte `S3DVertex`, per-frame CPU distance sort of the draw list, merged
buffers capped at 65,535 vertices and evicted after 1 frame on camera move, occlusion culling
disabled at cell_size≥4, no real LOD (cell_size pseudo-LOD renders far cells at full detail).
Single orthographic sun shadow map; post chain has bloom/auto-exposure/FXAA but no TAA/motion
vectors → no upscaler integration possible.

**Concurrency (src/client/game.cpp, src/server.cpp, src/emerge.cpp):** Client main loop strictly
serial (input→step→render) on one thread; `ClientEnvironment` documented non-thread-safe; all GPU
uploads main-thread. Server: ONE coarse `m_env_mutex` serializes ServerThread vs 1-4 EmergeThreads
so badly that `Server::yieldToOtherThreads()` (server.cpp:1184) inserts literal `sleep_ms(1)`
calls. Emerge hard-capped at 4 threads "because lock-heavy". On a 24-core i9, ~6 cores do useful
work.

**Networking (src/network/mtp/):** Custom reliable-UDP ("MTP"), **512-byte MTU**, no encryption
(SRP handshake + all traffic plaintext), no client-side prediction (server force-sets position →
rubber-banding), full-state entity updates (~48 bytes each, no deltas), distance-only interest
management, zstd map compression (the one modern bit).

**Scripting (src/script/, builtin/):** PUC Lua **5.1.4** by default (LuaJIT optional; LuaJIT
itself frozen/community-maintained = bus-factor risk). One shared `lua_State` per env — all mods
share `_G`, isolation is path-string checks. Per-entity `on_step` Lua call every tick, unbatched;
ABMs iterate every active block × every ABM with no content prefilter; `LuaVoxelManip.get_data()`
copies 4096 nodes into a fresh Lua table per call, manual `close()` (leaks). SSCSM (roadmap item
number one) is a working scaffold without sandbox hardening.

**World data (src/mapnode.h, mapblock.h, mapgen/):** 4-byte MapNode (u16 content = ~59k usable IDs
hard cap, 4+4-bit day/night light in param1), 16³ AoS blocks, s16 coords (±31k nodes),
serialization v29 zstd, SQLite/LevelDB/Postgres/Redis backends. Lighting = single-threaded CPU
BFS. Mapgens CPU-only, serialized against the env lock. Liquids = budgeted event queue
(`m_transforming_liquid` UniqueQueue drained under `liquid_loop_max`; settled liquid already costs
zero — review-corrected: this subsystem's event-driven bone is already good; Part III §3's
contribution is typed cell state, mass conservation, and generalizing the frontier model to what
ABMs brute-force today).

**Entities (src/server/\*\_sao.\*, src/client/content_cao.\*):** Monolithic SAO/CAO
virtual-`step()` class hierarchies, u16 ID cap (~65k), k-d-tree spatial index, AABB-sweep-only
collision with no CCD (fast objects tunnel — acknowledged FIXME), no rigid-body dynamics.
Per-entity network messages with threshold-based sends.

**UI/Audio/Input (src/gui/, src/client/sound/):** Formspec DSL = 5,317-line monolithic string
parser, no layout system, zero accessibility. OpenAL + OggVorbis-only, no HRTF, no voice. Limited
key subset, no haptics, no gestures, IME stubbed. Flat key-value config.

**Build/platform:** C++17, CMake 3.12, vcpkg + vendored libs, good CI matrix
(GCC/Clang/MSVC/Android), ASan on one job only, no TSan/UBSan, Tracy opt-in, crash handling MSVC
SEH only. Upstream cadence: 3-month releases (5.16.x current).

## Part II — June 2026 state of the art (researched)

- **Vulkan 1.4** is the modern baseline (dynamic rendering, MDI, push descriptors
  core+mandatory); `VK_EXT_descriptor_heap`/`descriptor_buffer` for bindless;
  `VK_EXT_shader_object`/mesh shaders cross-vendor. **GL_EXT_mesh_shader (Oct 2025)** exists
  because of Minecraft's Nvidium mod (~10× chunk rendering via mesh shaders).
- **Slang** (Khronos-governed, in Vulkan SDK, ships in Valve Source 2) is the emerging
  shader-toolchain standard; the GLSL→Slang transition is still in progress industry-wide.
- **Radiance Cascades** (Sannikov/PoE2) — noiseless, temporal-free, constant-cost GI proven in 3D
  for bounded scenes; Voxel Cone Tracing revival; ReSTIR/DDGI for the HW-RT tier. Minecraft RTX
  discontinued → Vibrant Visuals (PBR+volumetrics, *not* path tracing) is the pragmatic industry
  signal.
- **DLSS 4.5 via Streamline 2.x** (open-source, vendor-agnostic layer), FSR Redstone (Dec 2025),
  XeSS 2/3; OptiScaler bridges them.
- Minecraft's ecosystem proves the voxel scaling playbook: Sodium (CPU/draw opt), **Nvidium**
  (mesh-shader GPU-driven chunks), **Voxy/Distant Horizons** (LOD to 4,000+ chunks).
- **Jolt** is now Godot's default physics (4.5+). **EnTT** ships in Minecraft Bedrock.
  enkiTS/Taskflow for job systems. C++26 shipped (Mar 2026) but engines sit at C++17-23.
- **Luau** (typed, actively maintained, GA type solver Nov 2025) adopted beyond Roblox (Warframe,
  FS25, Second Life); LuaJIT frozen at 5.1, community-only. **Wasm 3.0 + WASI P2 + Wasmtime** =
  production mod-sandbox tech.
- **GameNetworkingSockets** (QUIC-style, AES-GCM; v1.5.0 Apr 2026) / WebTransport-over-HTTP/3 =
  2026 game transport standards. Minecraft Java itself is moving OpenGL→Vulkan in summer 2026.
- glTF + KTX2/BasisU + meshoptimizer = standard asset pipeline. Steam Audio / miniaudio for
  spatial audio.
- Cautionary tale: Hytale's full-engine rewrite died (cancelled Jun 2025) — phased rearchitecture
  with a shippable engine at every step is the lesson.

## Part III — The architecture

### North star

A clean-break voxel engine that treats the GPU as the primary compute device for the world, the
CPU as a 24-core job machine instead of a single hot thread, and the modding ecosystem as the
product. **Hardware floor: a working-Vulkan-1.3+ GPU of Turing/RDNA2 vintage (~2018–2020+)** —
the bindless GPU-driven path requires it in practice; an earlier "Vulkan 1.2 / 2016+" wording was
contradicted by the chosen backend (descriptor_buffer is broken pre-Turing on NVIDIA) and was
corrected in adversarial review. RTX-4090-class is the design target. Build toolchain is a
separate axis from hardware: GCC 12+/Clang 16+/MSVC 19.36+ for C++23 — pre-2022 LTS distros need
a newer compiler to *build*, regardless of GPU. The engine must remain **shippable at every
milestone** (the Hytale rewrite died in the dark; see Part V phase gates and Part VI for what
"1.x maintained" concretely costs).

### Design tenets

1. **GPU-resident world**: chunk data lives on the GPU in the same byte layout as on the CPU — no
   per-frame transcoding (disk stores the same layout compressed at rest).
2. **Jobs, not threads**: no subsystem owns a thread; everything is tasks on a work-stealing
   scheduler.
3. **One schema per concept**: one LOD pyramid (renderer + network), one identity registry
   (content + entities + players), one widget tree (menus + HUD + world UI).
4. **Server authority, client prediction**: the only honest anti-cheat for open source.
5. **Capability security**: mods and server-sent code get explicit grants, not path-string
   filters.
6. **Typed contracts**: machine-readable API schema; the LSP, the sandbox, the docs, and the
   conformance suite all derive from it.

### 1. Rendering — Vulkan-first, GPU-driven

**Replace the IrrlichtMt video stack entirely** (irr/src/OpenGL\*, the setMaterial-per-draw model,
GLSL #define injection in src/client/shader.cpp). Irrlicht is fully retired: mesh loading →
glTF-first (tiniergltf + meshoptimizer), GUI → new widget tree (§6), math → GLM-class library.

- **Thin internal RHI, exactly one initial backend: Vulkan** (1.3 required, 1.4 preferred;
  dynamic rendering, MDI, push descriptors). The RHI exists so a Metal/D3D12 port is *possible
  later*, not to carry GL. No GL, no GLES, no compatibility profile. **Descriptor strategy is
  explicitly tiered** (review-corrected — the original unconditional descriptor_heap baseline
  exists only on Ampere+ beta drivers): guaranteed base = bindless via `descriptor_indexing`
  (core since 1.2, solid on Turing/RDNA2+); fast path = `VK_EXT_descriptor_buffer`/
  `descriptor_heap` (EXT-only, pre-KHR as of Jan 2026, broken pre-Turing on NVIDIA) behind
  capability checks; `VK_EXT_shader_object` with classic pipeline objects as fallback. Mesh
  shaders (Turing/RDNA2+) sit above a plain-MDI path that works on everything at the floor.
- **Shader toolchain: Slang** → offline SPIR-V + shader cache. Feature flags become
  specialization constants, not string #defines. Honest status (review-corrected):
  Khronos-governed since Nov 2024, in the Vulkan SDK, shipping in Source 2 — the *emerging*
  standard with the GLSL→Slang transition still in progress industry-wide (the earlier
  glslang-HLSL-deprecation justification was a non-sequitur; glslang's GLSL front-end remains
  supported). Slang is a single point of failure for the whole render pipeline: pin a version,
  write the exit-strategy ADR ([ADR-0002](adr/0002-slang-exit-strategy.md)), validate
  mesh/task-stage emission in the Phase 0 spike, and keep a DXC/GLSL fallback for any stage where
  Slang tooling proves immature.
- **GPU-driven chunk rendering**: all chunk meshes in large persistent device buffers; per-draw
  args in an indirect buffer; compute shader does frustum + occlusion (two-pass HiZ) culling and
  compaction → `vkCmdDrawIndexedIndirectCount`. Bindless material/texture indexing. Deletes the
  per-frame CPU draw-list sort, `MapBlockComparer`, the U16_MAX merge cap, per-block material
  swaps. **Flagship fast path: mesh-shader chunk pipeline** (Nvidium-proven ~10×) consuming the
  same packed chunk data — the 4090 runs this by default.
- **Meshing: greedy meshing + packed vertices** (~8–12 bytes: u16³ chunk-relative position,
  3-bit face normal, packed light/AO, tile index) replacing 38-byte S3DVertex and naive per-node
  faces. Special drawtypes (liquids, plants, nodeboxes) keep a non-greedy path. Workers write
  into staging rings; **uploads go through a dedicated Vulkan transfer queue** (no
  shared-GL-context hacks — that idea is dead with GL).
- **Far-terrain LOD**: octree/clipmap renderer consuming the **persisted LOD aggregate pyramid**
  (§3 — single schema shared with network streaming). Near rings full detail, far rings
  downsampled aggregate meshes/ray-march impostors. Target: horizon-scale view distances
  (Voxy/Distant-Horizons-class, 64–128+ chunks effective).
- **Shading: clustered forward+** (one path for opaque *and* translucent — resolves the
  deferred-vs-foliage conflict) with a thin G-buffer (depth/normals/motion vectors) for post. PBR
  materials via optional KTX2 channel conventions.
- **Lighting tiers** (all fed by one emissive/material convention):
  - **Tier A (floor)**: baked per-vertex light + AO — the same *look and gameplay semantics* as
    1.x, but re-plumbed (review-corrected): the mesher bakes it from the §3 server light contract
    (per-node light + sky-exposure), not from the deleted param1 banks. Plus CSM (3–4 cascades)
    sun shadows and clustered dynamic point lights — torches finally *light things* dynamically.
  - **Tier B (mid, no RT hardware)**: voxel GI, two candidates honestly scoped (review-corrected).
    **Radiance Cascades**: PoE2 proves 3D RC for *bounded* scenes; at horizon-scale view distances
    the cascade-0 voxelization cost and ringing-fix overhead are **unproven — a research spike,
    not a solved import**. De-risked default: **voxel cone tracing fed by a new GPU
    radiance-voxelization compute pass** (3D clipmap, radiance injected from emissive + lit
    surfaces, mipmapped, incrementally updated on edit) — scoped as real Tier B work with its own
    VRAM/compute budget, *not* something the engine "already has" (it has no compute shaders
    today).
  - **Tier C (flagship)**: hardware ray query — RT reflections/AO + DDGI/ReSTIR-style GI. BVH
    over chunk geometry rebuilt incrementally on edit.
  - **Experimental**: brickmap ray-marched renderer (Teardown-style) as a dev-flag research mode,
    never the shipping path.
- **Temporal stack**: TAA + motion vectors as an explicit engineering contract, not a freebie
  (review-corrected — snapshot interpolation yields *rigid-transform* motion only): (a) vertex
  shaders doing wind/water displacement emit per-fragment velocity (current minus prior-frame
  animated position); (b) a defined velocity-write policy for alpha-blended translucents; (c)
  disocclusion/history-reject masks on block edits so upscaler history invalidates where chunk
  topology changes. Then **Streamline 2.x → DLSS 4.5 / FSR / XeSS upscaling + frame generation**;
  FSR as the cross-vendor default. DLSS distribution (review-corrected, EULA-aware): prebuilt
  blobs are never vendored into the source repo; bundled in Steam/installer builds (NVIDIA's EULA
  permits in-app object-code distribution), runtime-fetched via extra-data on Flathub — formalized
  in [ADR-0004](adr/0004-dlss-distribution.md). HDR (scRGB/FP16) swapchain.
- **Textures: KTX2/BasisU** transcoded to BC7/BC5 at load; `^`-composition semantics preserved by
  baking compositions into array slices at media load. Per-pack opt-out to RGBA8 for pixel-art
  purity.

### 2. CPU runtime — 24 cores of jobs

- **Job system (enkiTS)** replaces every dedicated thread (ServerThread/EmergeThread/MeshUpdate/
  Connection/Async pools). Mapgen, meshing, lighting, ABM scans, network serialization, audio
  decode = tasks. Reserved small I/O pool for blocking DB/HTTP. Tracy (pinned version) instruments
  the task graph.
- **Kill `m_env_mutex`**: region-sharded locking (striped locks keyed by block position; canonical
  lock ordering; coarse structural lock only for rare global ops). Emerge takes shard locks only
  at fetch/writeback. `Server::yieldToOtherThreads()` and its `sleep_ms(1)` hack are deleted.
  **TSan CI job is mandatory before this lands.** Why canonical ordering actually suffices
  (review-added cross-ref): §3 bounds every remaining CPU-side light cascade — the unbounded
  v29-style BFS moves to the GPU/visual tier, and the residual server light BFS is radius-bounded,
  so its lock set is computable *before* acquisition. Operations spanning many shards
  (worldedit-scale edits, `clear_objects`) take the coarse structural lock; an unbounded
  discovery-order BFS would not be lockable this way.
- **ECS (EnTT) as the entity substrate** — and under clean break, the *new* Lua API exposes entity
  handles natively (u64 generation+index, §3 identity). Transform/physics/network-dirty/animation
  as dense components; batched SIMD-friendly passes; network delta lists built by iterating dirty
  components. The 65k entity cap dies.
- **Fixed-timestep simulation decoupled from render**: sim at fixed Hz, render at display rate
  (165 Hz ultrawide) interpolating snapshots; clean *rigid-transform* motion vectors fall out
  (review-corrected: animated-vertex velocity — wind, water — is shader-emitted per the §1
  temporal contract; it does not fall out of interpolation).
- **Physics**: collision step (`collision.cpp` successor) compiled **identically into client and
  server** — necessary but *not sufficient* for prediction (review-corrected: identical source
  across two binaries still diverges via FMA contraction, autovectorized reduction order, and
  libm differences). The determinism mechanism is pinned: the shared step builds in its own TU
  with `-ffp-contract=off`, no fast-math, no autovectorized reductions, and a vendored
  deterministic implementation of the few libm calls it makes (`truncf` et al.) — **plus pinned
  FTZ/DAZ (MXCSR/FPCR) on simulation threads, which is runtime state no compile flag controls
  (spike-3 finding)**. The cross-build prediction-replay harness is a **blocking CI gate**
  (Part VII) with a contraction-enabled negative control that must diverge — keeping the
  mechanism testably load-bearing, not decorative. **Jolt**
  integrated as an opt-in backend for non-predicted dynamics only (projectiles with CCD, vehicles,
  ragdolls); predicted player movement stays on the deterministic shared AABB path. Jolt never
  touches authority.
- **C++23** (GCC 12+/Clang 16+/MSVC 19.36+): `jthread`/`stop_token`, `span`, `expected`, `<bit>`.
  Modules deferred. Unity builds on. ASan everywhere it runs, UBSan added, TSan as above.

### 3. World data — palette chunks, one identity, one LOD pyramid

The lens where clean break pays most. Replaces mapnode.h/mapblock.h/database/\* wholesale; a
**one-way `luanti1-to-2` world converter** ships instead of live compat.

- **Voxel cell**: u32 global content ID + typed state field, stored **palette-compressed per
  chunk** (per-chunk palette + adaptive 1/2/4/8/16-bit packed indices — the modern Minecraft
  model). Kills the u16 content cap and per-block name-id maps. **State-ownership rule
  (review-added)**: exactly one GPU-visible in-cell store and one CPU-only side-table, with a
  precedence rule. Anything the renderer reads per frame — including **microblock/sub-voxel
  occupancy** (2³/4³ masks for slabs/stairs/destruction) — lives *inside the packed blob* as
  fixed-width in-cell state (its width frozen in Contract 1); the sparse side-table holds CPU-only
  rich state (named metadata, timers, references) the renderer never touches. **Repack policy
  (review-added)**: index width grows in steps with hysteresis; bulk writes (mapgen fill,
  schematic paste, explosions) batch under one repack; a hot-path read-unpack budget joins Part
  VII's perf gates.
- **Layout**: palette-indexed — a dense Morton-ordered per-cell index stream over AoS palette
  records, 8³ brick granularity ("SoA" in earlier drafts was imprecise: the dense per-cell array
  is the index stream, while palette entries are 16-byte records; frozen as such in Contract 1,
  divergence recorded in ADR-0009). **Byte-identical CPU↔GPU**
  — the mesher/ray-marcher reads the per-chunk palette + width header in-shader, no per-frame
  transcode. Disk holds the same layout *compressed at rest* — one decompress on load, canonical
  thereafter ("byte-identical three ways" was an overclaim; review-corrected).
  Metadata/timers/static-objects move out of the hot chunk into sparse server-side stores.
- **Coordinates**: s32 node coords (±2.1B; the ±31k limit dies) + **streaming origin rebasing**
  for float precision at distance. **The Lua/mod API exposes stable virtual absolutes (s32 node
  space) — rebasing is engine-internal and invisible to mods** (review-added: this is an
  API-semantics decision, now pinned in Contract 4); all intra-process absolute caches (anti-cheat
  last-good-position, attachments, particles, audio anchors, queued position commands) are offset
  atomically inside the rebase critical section. Server and client rebase identically (prediction
  determinism).
- **Lighting split (resolution pinned — review-corrected)**: (a) the **server light contract keeps
  per-node granularity** — one single-channel 0–15 light value + a sky-exposure bit per node,
  event-driven bounded BFS; day/night blend reconstructs at query time from light + sky-exposure +
  time-of-day, so `get_node_light()`/`get_natural_light()` return the values mods see today
  (mob-spawn/farming thresholds unchanged). What dies is the dual 4+4-bit param1 *encoding* and
  the lighting-complete bookkeeping — not per-node resolution. (b) Higher-tier **visual** light
  (GI, dynamic lights, shadows) is GPU-resolved; the Tier A mesher bakes per-vertex light from
  (a).
- **Persistence (review-corrected)**: a **zero-config single-file embedded default (SQLite-class)
  stays for casual/single-player worlds** — RocksDB's LSM write-stalls and compaction threads are
  wrong for a one-disk laptop. **RocksDB is the opt-in large-world/server backend**, with column
  families: voxels / metadata / timers / static-objects / light-heightmap / **lod-aggregate**
  (LOD streaming reads aggregates without touching voxel blobs). Thin interface retained for
  Postgres operators. License ADR required before adoption
  ([ADR-0003](adr/0003-rocksdb-default-and-licensing.md)): take RocksDB's Apache-2.0 arm via the
  engine's LGPL-2.1-*or-later* upgrade path. World manifest holds format version, content
  registry, mapgen params.
- **Mapgen**: staged pipeline (density → caves → biomes → surface → ores/deco → structures) as
  parallel jobs; **FastNoise2-class SIMD noise** (AVX2/512); optional GPU compute density pass on
  flagship (CPU fallback mandatory — headless servers). **Determinism needs a seam-merge design,
  not just seeding (review-corrected)** — named deliverables of the Phase 0 spike: (1) a
  deterministic boundary-cell ownership rule for structures overflowing chunk edges, replacing
  today's scheduling-dependent first-writer-wins blit-back; (2) async `on_generated` hooks are
  snapshot-isolated — they never read mutable neighbor-chunk state — so chunk output stays a pure
  function of (position, seed). The 1/4/32-worker identical-output CI gate then holds *by
  construction*.
- **Fluids (framing corrected per review)**: today's `transforming_liquid` is already a budgeted
  event queue — settled water already costs zero. The genuinely new parts: typed cell state
  replacing param2 bit-masks, optional mass-conserving mode per fluid def, and **generalizing the
  frontier scheduler to phenomena ABMs brute-force today** (fire, falling, growth).
- **Unified identity registry** (one subsystem, not three migrations): content `mod:name` → u32
  (append-only with tombstones, persisted once in the manifest); entities u64 {generation,index}
  aligned with EnTT + persistent entity UUIDs for saved references; **players keyed by UUIDv7**,
  display name mutable (renames + unicode names — already queued upstream). **Handle semantics
  pinned in Contract 3 (review-added)**: only generation-validated handles or persistent UUIDs
  cross the Lua/protocol boundary — never a raw recyclable index; every API accepting a handle
  validates generation and returns a typed "gone" result, preserving 1.x's null-on-removal
  guarantee. Live calls use runtime handles; anything persisted uses UUIDs, with a documented
  resolve path between them.
- **LOD aggregate pyramid as a first-class persisted artifact** — *the* shared schema (renderer +
  network): sparse octree, per-node dominant-material palette + occupancy/heightfield + content
  bounds; generated as a mapgen byproduct; edit-dirty bubbles up with content hashes for client
  refetch; coarse levels stored, fine levels regenerable.

### 4. Scripting & modding — Luau, capabilities, typed contracts

- **Luau as the primary VM** (typed, actively maintained, sandbox-designed, GA type solver). Key
  fact making the break *gentler* — not gentle (review-corrected): **Luau is a Lua 5.1
  derivative**, so mod logic ports *with edits* for actively-maintained mods; `setfenv`/
  `loadstring` survive deprecated, but **`string.dump` bytecode is removed** — which breaks the
  engine's own `core.serialize`/`core.deserialize` (built on string.dump + loadstring + setfenv
  sandboxing, used by entity staticdata everywhere). That is a **named reimplementation
  deliverable**: re-encode data serialization on a non-bytecode emitter, rebuild safe-deserialize
  sandboxing on Luau's native per-script environment model (`luaL_sandbox` —
  performance-positive, unlike fenv), drop function-dumping outright. Mod call sites of
  `core.serialize` stay unchanged. LuaJIT dependency (frozen, bus-factor risk) exits.
- **Capability-based sandbox**: mods declare grants in mod.conf (`fs:read:…`, `net:http:<host>`,
  `voice`, `storage`); deny-by-default; per-mod isolation instead of one shared `_G`.
- **SSCSM completed**: server-sent client scripts run in a sandboxed Luau isolate. Budgeting
  mechanism made precise (review-corrected): a cooperative Luau interrupt budget for the common
  case **plus an external watchdog thread with a hard wall-clock deadline that tears down the
  VM** — safepoint interrupts cannot bound long native calls, and instruction counts are not
  wall-clock; outstanding buffer lifetimes invalidate safely on teardown. Timing/cache side
  channels are explicitly out of scope for the Luau tier; genuinely hostile-server paranoia routes
  to the **WASM (Wasmtime) hardened tier**. (Also corrected: current SSCSM is a working scaffold
  with a dedicated thread and IPC plumbing, not a bare skeleton — what's missing is exactly this
  hardening.)
- **Bulk voxel access — zero-copy claim retracted (review finding: Luau `buffer`s own their
  memory; aliasing views over engine arrays are not expressible without forking the VM)**: the
  VoxelManip successor uses **single-memcpy packed snapshots** — copy-in/mutate/blit-out of the
  palette/state arrays as one contiguous, bounds-checked Luau `buffer`, RAII lifetime, plus an
  async variant on worker isolates (snapshot-in, blit-back under shard locks). Still a massive win
  over 1.x — one memcpy versus 4,096 boxed table entries per call — now honestly priced. If
  profiling ever justifies true aliasing views, that is a Luau-fork ADR with a
  chunk-unload/rebase invalidation protocol, costed as a first-class dependency.
- **Dispatch modernization**: ABM content-bitset prefilter (block palette → which ABMs *can*
  fire), server-wide timer heap (O(log n)), batched entity-step API, opt-in event bus.
  Compatibility semantics pinned (review-added): registration-order globalsteps and
  per-entity-per-tick full-dt `on_step` remain the **default** behavior; the event bus and
  batched stepping are opt-in, and any dt-coalescing or reordering is a documented porting-guide
  breaking item — never silent drift. Per-mod cost attribution with flame-graph export.
- **Typed, machine-readable API** from day one: LuaCATS/JSON schema generated from registrations;
  LSP autocomplete; DAP debugger bridge (singleplayer/dev); CI fails on schema/doc drift.
- **Assets**: glTF 2.0 first-class (models + skeletal animation with named clips, crossfade,
  blend layers); KTX2 textures; Opus/FLAC audio; meshoptimizer/gltfpack as the blessed authoring
  path. B3D/X loaders kept read-only for porting.

### 5. Networking — encrypted, predicted, streamed

Clean break: **new protocol, no 1.x wire compat** (a bridging proxy is explicitly out of scope).

- **Transport: GameNetworkingSockets** (QUIC-style congestion control, AES-GCM-256, message
  lanes) — the hand-rolled 512-byte-MTU MTP dies, and with it the plaintext-everything exposure.
  SRP auth runs inside the encrypted tunnel. Infrastructure honesty (review-added): GNS ships a
  dependency-free ICE/STUN hole-punch client, but **no signaling service and no relay fallback
  off-Steam** (SDR is Valve-proprietary) — player-hosted servers need a lightweight rendezvous
  service, and symmetric-NAT clients need optional TURN relays; direct-IP/port-forward connection
  remains supported as today. That is *operated infrastructure*, not just a library
  ([ADR-0005](adr/0005-gns-operated-infrastructure.md)). (Transport abstraction retained; a
  WebTransport backend is the *future* web-client door, not current scope.)
- **Replication: snapshot + delta** with per-client acked baselines, dirty-bitmask field encoding,
  periodic keyframes + per-object CRC; property dedup via content hashes. Entity traffic drops
  ~5–10×.
- **Client prediction + reconciliation** for the local player (input sequence numbers, server
  echoes last-processed seq, client re-simulates unacked inputs over the shared collision step).
  Rubber-banding dies; server stays authoritative — but honestly (review-corrected): the legacy
  `checkMovementCheat` loose max-speed bound *cannot* coexist with prediction (it disables itself
  when attached and never simulated physics); **the server authoritatively re-simulates the shared
  collision step, replacing it**. Server-pushed parameter changes are sequenced:
  `physics_override` updates, attach/detach, and teleports carry the input-sequence number at
  which they take effect and replay deterministically during reconciliation; attached players
  suspend prediction (server-driven transform). Remote entities: tick-indexed jitter buffer
  (100–150 ms) interpolation.
- **Interest management**: spatial-hash relevancy + frustum/recency weighting + **per-client
  bandwidth governor** allocating budget across block sends / entity deltas / media / LOD stream.
- **World streaming**: full chunks near; **LOD pyramid aggregates far** (same artifact as §3) at
  low priority — visual range decouples from active-simulation range; coarse-first refinement on
  approach.
- **Server**: async write-back map I/O (WAL), parallel packet serialization/compression jobs,
  sharded env (§2). Rate limiting + token buckets on handshake (DoS posture). Anti-cheat doctrine
  documented: server authority + validation, never client trust.

### 6. UI, input, accessibility — one widget tree

- **Formspec dies.** Replacement: **retained reactive widget tree** authored as a **Luau-native
  declarative DSL** (component functions returning node trees; signal-based reactive state;
  O(dirty) updates). ~30 C++ widget primitives; **flexbox/constraint layout** (Yoga/Taffy/
  Clay-class). Transpiler scope made honest (review-corrected — the dispatch table has 46
  handlers, not 43): the formspec→DSL transpiler mechanically covers *stateless* geometry/widget
  elements; the stateful parser directives — `container`/`scroll_container` nesting,
  `style`/`style_type` cascades, `listring`, `real_coordinates`, `tableoptions`/`tablecolumns`,
  and `list` inventory-location semantics — need semantic re-authoring, listed explicitly in the
  porting guide.
- **Server-driven UI executes in the SSCSM client isolate**: hover/drag/validation/animation are
  client-local; only semantic intents cross the wire (typed messages, server-validated). The
  per-click round-trip era ends. **Serverless surfaces (review-added — SSCSM cannot host UI with
  no server)**: main menu, server browser, settings, and singleplayer pre-load run the same Luau
  widget DSL in a dedicated *local* menu isolate — the successor to today's `mainmenu` env. One
  tree, host-agnostic.
- **One tree, three surfaces**: modal menus, screen-space HUD (the 9 hardcoded HUD types become
  composable widgets), and **world-space panels** (depth-correct in-world UI). One layout engine,
  one styling system, one accessibility tree.
- **GPU vector UI renderer** in the Vulkan frame graph: MSDF glyph atlas + HarfBuzz shaping
  (CJK/RTL correctness), resolution-independent vector primitives, SVG icons — crisp at any DPI,
  animated for free. Irrlicht CGUI\* and the software scaler die.
- **Accessibility is structural**: every widget carries semantic role/name/state forming an
  accessibility tree bridged via **AccessKit** (UIA/AT-SPI/NSAccessibility); keyboard-complete
  navigation enforced by the framework; colorblind LUTs, reduced-motion, contrast and scale
  settings; schema lint fails interactive widgets without accessible names.
- **Input**: SDL3 raw layer (full scancode space) → **action-mapping layer** (named actions,
  binding contexts, chords, analog curves, deadzones); gamepad rumble + adaptive triggers; touch
  gesture recognizer; **real IME composition**.
- **Settings**: typed schema-described store (TOML; auto-generated settings UI from schema; .conf
  importer). **i18n: Fluent** replaces gettext (real plurals/selectors, runtime locale switching,
  RTL mirroring).

### 7. Audio — the voxel world as an acoustic model

- **miniaudio** as device backend (WASAPI/CoreAudio/PipeWire in one header) feeding **Steam
  Audio** for HRTF binaural spatialization.
- **Voxel-derived environmental acoustics** — the signature feature: greedy-meshed acoustic proxy
  hulls from chunk data + per-material absorption in nodedefs → **occlusion, transmission, and
  reverb computed from the actual world** (footsteps muffled by the stone wall that's actually
  there; caves that echo because they're caves). No hand-authored reverb zones. OpenAL-Soft-class
  fallback on the floor tier.
- **Opus + FLAC** codecs join Vorbis; decode on the job system.
- **Music/ambience director**: Luau-scriptable layered stems (biome/depth/weather/time inputs),
  cue state machine, stingers — the missing system every game currently fakes.
- **Optional proximity + party voice** (Opus + RNNoise + VAD over encrypted GNS lanes),
  positionally routed through Steam Audio (voices occluded by walls), capability-gated,
  default-off, server-mediated mute/moderation.

### 8. Engineering ops — the unglamorous load-bearing walls

- **Supply chain first** (gates every new dep): CycloneDX SBOM per release; CI license-audit
  gate; **DLSS/Streamline proprietary blobs quarantined into a separable, dynamically-loaded,
  off-by-default plugin** (LGPL core stays pure); written pinning/vendoring policy; reproducible
  Linux builds + provenance.
- **Crashpad** cross-platform crash capture (replacing MSVC-SEH-only), out-of-process, symbol
  server fed by CI, opt-in submission to self-hostable Sentry; `VK_ERROR_DEVICE_LOST` as a
  first-class crash category.
- **Observability**: OpenTelemetry layer over the existing MetricsBackend (server + client),
  Tracy for dev tracing; **opt-in, default-off, aggregate-only, no-persistent-ID client
  telemetry** (hardware tier, frametime percentiles, crash-free rate, capability failures) —
  self-hostable, published schema; this is how a flagship-first engine avoids torching laptops in
  the wild.
- **Distribution**: Steam (optional integration, FOSS build stays store-free) + Flathub; signed
  delta auto-updater with stable/beta/nightly channels; **ContentDB-2** with explicit 1.x-vs-2
  content tiers, typed-API lint, compat matrix (an *external* deliverable — see Part VI risk 2).
  Version 2.0 = hard semver break; 1.x maintained on its own channel (Part VI risk 6).
- **Power/thermal governor**: device-class detection → Flagship/Balanced/Battery/Potato presets
  driving render scale, frame caps, LOD distance, GI tier, audio tier; closed-loop
  frametime/thermal controller with hysteresis. Steam Deck is the named mid-tier target (Deck
  Verified gate).

### What the 4090 / i9-14900KF specifically unlocks (honest targets)

| Dimension | Today (measured/derived) | Target |
|---|---|---|
| View distance | ~150–300 nodes usable | 1,000–2,000+ nodes effective (LOD pyramid), Voxy-class horizons |
| Frame rate @ 3440×1440 | CPU-bound on one thread | 120–165 fps with Tier B/C lighting (DLSS Quality + FG headroom beyond) |
| Draw submission | 1 draw + state change per buffer | 1 MDI submit per pass; mesh-shader path on Ada |
| Lighting | 4-bit baked + 1 shadow map | Clustered dynamic lights + voxel GI; RT reflections/GI on Tier C |
| Chunk generation | ~4 threads, scalar noise, lock-throttled | 24+ cores saturated, SIMD/GPU noise — order-of-magnitude faster exploration |
| Active entities | low thousands, u16 cap, per-entity Lua calls | 10k+ (ECS batches, delta replication, u64 handles) |
| Cores doing useful work | ~6 of 24 physical | 24 cores well-utilized via job graph + sharded env (review-honest: E-core/SMT throughput is not linear, and serial per-env gameplay Lua remains the Amdahl ceiling on mod-heavy worlds — engine passes parallelize, mod callbacks within an env do not) |
| Latency feel | server-snapped movement, per-click UI round-trips | predicted movement, client-local UI |

### What this design deliberately does NOT do

- **No Rust/full rewrite from scratch** — phased rearchitecture; the engine boots devtest at
  every milestone (Hytale is the cautionary tale).
- **No path-traced-only renderer** — even Mojang retreated from RTX to PBR+volumetrics; raster +
  tiered GI is the pragmatic voxel target; brickmap ray-marching stays a research flag.
- **No web client now** — flagship-first; the transport/RHI abstractions leave the door open, but
  WebGPU/WASM is a future track, not budgeted scope.
- **No ECS dogma in the Lua API** — mods see entities and handles, not components; ECS is the
  engine's substrate.
- **No 1.x protocol/world live-compat** — one-way converter + porting guide + maintained 1.x
  branch is cheaper and more honest than a forever-compat layer.
- **No mandatory telemetry, no auto-sent crashes, no non-separable proprietary blobs** — each
  would rightly burn community trust.

## Part IV — Cross-cutting contracts (freeze before parallel work starts)

Specs live in [docs/contracts/](contracts/).

1. **Packed voxel blob layout** (palette + index widths + Morton + brick granularity + in-cell
   state/occupancy widths + repack policy) — world-data ↔ renderer ↔ Lua buffers.
2. **LOD aggregate pyramid schema** — world-data ↔ renderer ↔ networking (one owner: world-data).
3. **Identity** (content u32 / entity u64 / player UUIDv7) — widths *and* validation semantics:
   only generation-validated handles or persistent UUIDs cross the Lua/protocol boundary.
4. **Coordinate representation** — s32 + origin-rebasing rules, identical client/server; mods see
   stable virtual absolutes.
5. **Shared collision step** — semantics *and* FP determinism mechanism (isolated TU, pinned
   flags, deterministic libm).
6. **Capability taxonomy** — sandbox grants used by mods, SSCSM, UI, voice.

## Part V — Phasing (every phase ships a running engine)

**Phase 0 — Foundations (everything else queues behind this):** C++23 + Unity builds + TSan/UBSan
CI; supply-chain gate (SBOM/license CI, pin Tracy); Crashpad; enkiTS vendored; Vulkan device
bring-up + Slang pipeline rendering the *existing* world data (validating mesh/task-stage emission
and the DXC/GLSL fallback); freeze the six contracts above; spike the hard invariants — mapgen
determinism **including the seam-ownership merge rule**, origin-rebasing correctness,
shared-collision-step FP determinism across builds, and 3D-radiance-cascades-at-scale feasibility
(with VCT as the de-risked fallback).

**Phase 1 — Three parallel pillars:** (a) *Renderer*: greedy meshing + packed vertices →
GPU-driven culling/MDI → transfer-queue uploads; (b) *World*: identity registry + persistence
(SQLite-class zero-config default, RocksDB opt-in) + palette chunks + L1 converter; (c) *Runtime*:
sharded env locking (kill yieldToOtherThreads), job-system migration one pool at a time (meshing →
emerge → ABM). **Honesty note (review-added): Phase 1 is where the trunk genuinely *forks*** —
renderer + world format + concurrency change together, coupled by Contract 1 — so during this
window the shippable fallback is the maintained 1.x branch, not in-place compat; "strangler fig"
properly describes Phases 2–4.

**Phase 2 — Systems:** two-tier lighting + clustered forward+ + CSM; far-LOD pyramid (generation,
persistence, rendering, streaming); GNS transport + snapshot/delta + prediction; Luau VM +
capability sandbox + packed-snapshot buffers + typed API/LSP; EnTT entity migration; parallel
mapgen + SIMD noise; event-driven fluids.

**Phase 3 — Experience:** widget tree + vector UI renderer + SSCSM-hosted UI + unified HUD; SDL3
input/action mapping/IME; miniaudio + Steam Audio + voxel acoustics; settings/Fluent i18n;
accessibility tree (built into widgets from day one, not retrofitted).

**Phase 4 — Flagship & ship:** mesh-shader chunk path; Tier C ray query GI/reflections;
Streamline upscaling/frame-gen (DLSS as separable plugin); voice chat; power/thermal governor +
Steam Deck tier; Steam/Flathub + auto-updater + ContentDB-2 coordination; porting guide +
formspec transpiler + L1 converter polish.

**Dependency spine:** Phase 0 contracts → palette chunks (everything reads them) → GPU-driven
renderer + sharded env (the two big unlocks) → LOD pyramid (needs both) → flagship tiers last
(least reach, most polish).

**Phase gates are falsifiable, not vibes (review-added):** a phase ships only when it meets the
Part VII budgets vs a 5.16 baseline (frame-time p99, server tick p99, mapgen chunks/sec, mesh
time per chunk), passes the 24-hour soak (no leak growth, no tick drift), and — for the world
pillar — passes converter round-trip parity (L1 → L2 loads and renders an equivalent world, zero
data loss, foreign content mapped to placeholders with a registry note). "Devtest runs
end-to-end" is the entry criterion for a gate review, not the gate.

## Part VI — Top risks

1. **Scope vs real capacity (review-quantified)** — this is a 15–40 person-year program; the
   binding constraint is not calendar time but **senior review throughput**: upstream master is
   effectively gated by ~2–3 reviewers today, none of whom currently owns
   Vulkan/ECS/Luau/accessibility domains. Realistic delivery needs ~5–8 dedicated domain
   engineers with per-pillar reviewers; absent that, this document is a **fork blueprint plus an
   upstream cherry-pick list** (greedy meshing, ABM prefilter, Crashpad, prediction), not an
   upstream roadmap. Mitigation: strangler-fig phasing (Phases 2–4), every phase shippable,
   flagship tiers strictly optional layers on a working core.
2. **Ecosystem split (review-reweighted)** — clean break risks a Python-2/3 schism, and the
   mitigation must be honest: Luau's 5.1 lineage *reduces per-mod port effort for
   actively-maintained mods* — it does not save the unmaintained long tail of ContentDB's 3,000+
   packages, which simply will not be ported; the surviving 2.0 ecosystem is bounded by the
   actively-maintained subset (a minority for a volunteer ecosystem), with the 1.x branch as the
   only answer for the rest. Engine-owned mitigation legs: transpiler + converter + typed-API
   lint. **ContentDB-2 is an external deliverable** owned by ContentDB's separate maintainers —
   coordination required, never assumed.
3. **Parallelism correctness** — sharded env + parallel mapgen determinism are the two hardest
   invariants. Mitigation: TSan gating, the Phase 0 spikes (seeding **and** seam ownership), old
   code paths behind flags during migration.
4. **Dependency weight** — GNS/Steam Audio/RocksDB/Wasmtime/Crashpad triple the dep surface;
   **Slang specifically is a single point of failure for the entire shader pipeline** (pinned
   version + exit-strategy ADR + DXC/GLSL fallback). GNS additionally implies *operated
   infrastructure* (rendezvous/TURN) beyond the library. Mitigation: supply-chain gate lands
   first; everything optional/compile-out where feasible.
5. **Community governance** — upstream's actual roadmap is far more modest; this design exceeds
   it deliberately. Honest framing: this is an unconstrained design — adoptable wholesale as a
   fork, or quarried piecemeal upstream (greedy meshing, ABM prefilter, Crashpad, prediction are
   all upstream-palatable today).
6. **The 1.x maintenance tax (review-added — "1.x maintained" was load-bearing and unstaffed)** —
   concrete commitment, not an open-ended promise: once Phase 1 lands, 1.x scope narrows to
   security + critical bugfix only (no features); a published EOL tied to a 2.0 milestone (e.g.,
   18 months after content parity); and the staffing assumption stated plainly — the same core
   maintainers split time, so the 2.x timeline already absorbs a nonzero 1.x tax. It is not free,
   and pretending otherwise was this document's own dishonesty before review.

## Part VII — Engineering quality standards (non-negotiable program mandate)

All software produced under this architecture is professional-grade. No corner-cutting.
Concretely:

**Definition of done (every change):** domain-owner code review; tests included (no "tests
later"); docs updated in the same change; no `TODO` as a substitute for a tracked issue;
perf-relevant changes ship with before/after Tracy captures or benchmark numbers.

**Testing strategy by construction, not retrofit:**

- *Unit*: Catch2 throughout; new subsystems land with their tests or don't land.
- *Property-based / round-trip invariants*: serialization, palette pack/unpack, coordinate
  rebasing, identity registry — fuzz the encode/decode pairs (round-trip equality is the
  property).
- *Determinism harnesses*: mapgen seed-reproducibility across thread counts (CI matrix: 1/4/32
  workers must produce identical chunks); prediction replay (recorded input streams re-simulate
  to identical positions on client and server builds).
- *Golden-image rendering tests* (review-corrected — the project has no GPU CI runners):
  lavapipe/SwiftShader software-raster goldens are the blocking CI subset, per lighting tier.
  Per-GPU-vendor goldens require self-hosted GPU runners that do not exist today; until they do,
  flagship-tier output (ray query, mesh-shader path, DLSS) is validated by a *scheduled bench on
  developer hardware* — stated plainly, not implied to be CI.
- *Fuzzing (libFuzzer/OSS-Fuzz)*: mandatory for every parser and untrusted-input surface —
  network protocol decoder, world converter, glTF/KTX2 ingestion, UI DSL, Fluent bundles.
- *Integration & soak*: devtest-style multiplayer harness on every PR; 24-hour soak with
  telemetry assertions (no leak growth, no tick-time drift) as a release gate.

**CI gates (blocking, not advisory):** ASan + UBSan on every PR; TSan required on any change
touching threading/sharded locks/job system; clang-tidy; license/SBOM gate; performance-regression
benchmarks with explicit budgets (frame-time p99, server tick p99, mapgen chunks/sec, mesh time
per chunk, palette read-unpack) that fail the build on regression beyond threshold; **the
determinism harnesses are blocking, not advisory (review-promoted): cross-build prediction-replay
(bit-identical re-simulation) on any change touching the shared collision step or FP flags, and
1/4/32-worker mapgen-identity on any change touching mapgen or the job system**; symbol upload
for the crash pipeline.

**Design discipline:** an ADR for each of the six frozen contracts and for every new dependency
(why this lib, license, maintenance posture, exit strategy); public API changes (Lua surface,
protocol, save format) go through a written RFC with a review window; the typed API schema is the
single source of truth — doc/schema/implementation drift fails CI.

**Security discipline:** threat model written for every network-facing and sandbox-facing
component before implementation; capability grants reviewed like permissions, not config; fuzz
coverage is a merge requirement for parsers (above); dependencies enter only through the
supply-chain gate.

**No prototype laundering:** experimental paths (brickmap renderer, GPU mapgen) live behind dev
flags and cannot graduate to default without meeting the full bar above — same tests, same docs,
same review. "It works on the 4090" is not a definition of done.

**Observability built in:** every subsystem ships with Tracy zones and OTel counters from its
first merge — instrumentation is part of the component, not an afterthought.

## Part VIII — Adversarial review log

Two full adversarial passes were run against this design; all confirmed findings are folded into
the text above (marked "review-corrected/-added/-quantified"). Full logs:
[round 1](reviews/round-1-completeness.md), [round 2](reviews/round-2-adversarial.md).

**Round 1 (pre-synthesis):** one completeness critic vs the four original design lenses →
**18 findings** (missing subsystems, cross-lens conflicts, claims contradicted by code) — all
resolved in the synthesis.

**Round 2 (post-synthesis):** six adversarial reviewers (rendering feasibility, concurrency
correctness, world-data/networking, ecosystem-migration honesty, project realism/legal, internal
consistency), every finding independently verified against the document, the Luanti source tree,
and June-2026 external facts → **48 findings: 39 confirmed (7 major, 32 minor), 9 refuted**. The
majors and their resolutions:

1. **Vulkan floor self-contradiction** (1.2/2016 floor vs descriptor_heap backend) → floor raised
   to Turing/RDNA2-class (~2018–2020+); descriptor strategy explicitly tiered with
   descriptor_indexing as the guaranteed base.
2. **"Identical source" ≠ FP determinism** for the shared collision step → mechanism pinned
   (isolated TU, `-ffp-contract=off`, no fast-math, deterministic libm); replay harness promoted
   to blocking CI gate.
3. **Server light contract resolution unspecified** (would silently break mob-spawn/farming
   thresholds) → per-node 0–15 + sky-exposure bit pinned; only the param1 *encoding* dies.
4. **Prediction vs legacy anti-cheat mutually exclusive** → server authoritatively re-simulates,
   replacing `checkMovementCheat`; physics_override/attach/teleport sequenced into the input
   stream; attached players suspend prediction.
5. **`core.serialize` is built on Luau-removed primitives** (string.dump/loadstring-bytecode/
   setfenv) → named engine reimplementation deliverable; "mods port nearly unchanged" softened to
   "with edits, for maintained mods."
6. **"Zero-copy Luau buffer views" not expressible** (Luau buffers own their memory) → claim
   retracted; single-memcpy packed snapshots, honestly priced; aliasing views would be a costed
   VM-fork ADR.
7. **Ecosystem-portability and 1.x-maintenance overclaims** → Risks 1/2 rewritten with quantified
   reviewer capacity (2–3 today; 15–40 person-years; 5–8 dedicated engineers or it's a fork
   blueprint) and Risk 6 added (1.x scope/EOL/staffing made concrete).

**Notable refuted findings (kept for the record, with why):** the frame-gen-latency attack
misread the perf table (FG is headroom *beyond* the DLSS-Quality figure, not the floor);
"job-system migration is incoherent" ignored that meshing never touches the env lock; the
CLA/relicensing objection conflated rewriting-under-the-same-license with relicensing (LGPL
permits the former without contributor consent); "GNS is unmaintained" conflated tagged-release
cadence with actual activity (v1.5.0 shipped Apr 2026); "typed API can't be generated —
registrations are untyped" attacked the legacy bindings the clean break discards.
