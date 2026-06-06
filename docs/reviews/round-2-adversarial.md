# Adversarial review — round 2 (six-lens attack on the final architecture)

**When:** 2026-06-05, after final synthesis (including the round-1 resolutions).
**Method:** six adversarial reviewers, each instructed to *break* the design through a distinct
lens, with read access to the Luanti 5.17-dev tree and web fact-checking against June-2026
reality. Every finding was then judged by an independent verifier (plan text + source tree + web)
with instructions to be skeptical in both directions — neither rubber-stamping findings nor
reflexively defending the design.

**Result: 48 findings → 39 confirmed (7 major, 32 minor), 9 refuted. All confirmed findings are
folded into [architecture.md](../architecture.md), marked "review-corrected/-added/-quantified".**

## Lenses

1. Rendering feasibility & performance honesty
2. Concurrency & runtime correctness
3. World data & networking
4. Scripting & ecosystem migration honesty
5. Project realism & legal/governance
6. Internal consistency & claim-vs-fact audit

## Confirmed findings (39)

Severity reflects the independent verifier's adjusted rating, not the reviewer's claim.

### Major (7)

| Lens | Finding | Resolution |
|------|---------|------------|
| Rendering + Consistency | "Vulkan 1.2 / ~2016+ floor" mutually exclusive with a renderer requiring descriptor_buffer/heap (broken pre-Turing on NVIDIA; EXT-only, pre-KHR Jan 2026) and mesh shaders (Turing+) | Floor raised to working-Vulkan-1.3+, Turing/RDNA2-class (~2018–2020+); descriptor strategy tiered: descriptor_indexing base → descriptor_buffer/heap fast path → shader_object with pipeline fallback |
| Concurrency | "Compiled identically into client and server" does not yield bit-identical floats (FMA contraction, autovectorized reductions, libm divergence) — prediction would oscillate | FP mechanism pinned: isolated TU, `-ffp-contract=off`, no fast-math, vendored deterministic libm; cross-build prediction-replay promoted to a blocking CI gate |
| World data | "Coarse block-light" left the server light contract's spatial resolution undefined; per-node `get_node_light()` 0–15 semantics (mob spawn, farming) would silently break | Contract pinned at per-node granularity: single-channel 0–15 + sky-exposure bit; day/night blend reconstructed at query time; only the param1 dual-bank *encoding* dies |
| Networking | "checkMovementCheat semantics preserved" is incompatible with prediction: the legacy check never simulated physics and disables itself when attached | Server authoritatively re-simulates the shared collision step, replacing the legacy check; physics_override/attach/teleport are input-sequence-stamped and replayed in reconciliation; attached players suspend prediction |
| Ecosystem | The engine's own `core.serialize`/`core.deserialize` is built on `string.dump` (removed in Luau), `loadstring`-of-bytecode (disabled), and `setfenv` (deprecated, deoptimizing, load-bearing for safe-mode sandboxing) | Named engine reimplementation deliverable: non-bytecode emitter, Luau-native sandbox (`luaL_sandbox`), function-dumping dropped; "mods port nearly unchanged" softened to "with edits, for actively-maintained mods" |
| Ecosystem | "Zero-copy Luau `buffer` views over palette arrays" is not expressible — Luau buffers own their allocation; no external-memory view constructor exists in the VM | Claim retracted: single-memcpy packed snapshots (copy-in/mutate/blit-out), honestly priced; true aliasing views would require a costed Luau-fork ADR with unload/rebase invalidation |
| Project realism | Risk mitigations overclaimed: "Luau lineage keeps logic portable" ignored the unmaintained long tail of 3,000+ ContentDB packages; "1.x maintained" was load-bearing and unstaffed (the same 2–3 reviewers gate upstream master) | Risk 1 quantified (15–40 person-years; 5–8 dedicated engineers or this is a fork blueprint); Risk 2 reweighted (effort-reducer, not schism-preventer; ContentDB-2 is external); Risk 6 added (1.x = security-only scope, published EOL, staffing tax stated) |

### Minor (32) — grouped by theme

**Rendering honesty (6):** Tier A "current model kept" conflated look with mechanism (param1 banks
die; the mesher now bakes from the server light contract) · "Radiance Cascades PoE2-proven"
overclaimed for horizon-scale 3D (cascade-0 voxelization unproven at scale → research spike, VCT
de-risked default) · VCT "from a voxelization the engine already trivially has" was false (no
compute shaders exist; new radiance-voxelization pass scoped as real work) · the glslang-HLSL
deprecation was a non-sequitur justification for Slang (and Slang is an unacknowledged single
point of failure → risk entry + fallback) · motion vectors are an engineering contract, not a
forward+ byproduct (animated-vertex velocity, translucent velocity policy, edit disocclusion) ·
DLSS-as-LGPL-plugin framed a redistribution-rights question as a linkage question (per-channel
distribution policy: bundled where EULA permits, extra-data on Flathub).

**Concurrency & runtime (6):** canonical lock ordering only works because §3 bounds CPU light
cascades — cross-reference made explicit (unbounded BFS is not lockable that way) · "all 32
threads useful" conflated logical threads with throughput and ignored the serial per-env Lua
ceiling (perf table corrected to 24 physical + Amdahl caveat) · C++23 compiler floor is a separate
axis from the hardware floor (documented) · origin rebasing left the Lua-visible coordinate
semantics undecided (pinned: mods see stable virtual absolutes; caches offset atomically) ·
"position-seeded determinism" lacked the actual hard part — a deterministic seam-ownership merge
rule for cross-chunk structures (now a named Phase 0 spike deliverable; async on_generated is
snapshot-isolated) · EnTT generation-index handles needed pinned validation semantics at the Lua
boundary (Contract 3: validated handles or UUIDs only; typed "gone" results).

**World data & networking (7):** palette repack-on-growth had no policy (width hysteresis, batched
repack, read-unpack perf budget) · "byte-identical CPU/GPU/disk" overclaimed the disk leg
(compressed at rest; one decompress on load) · microblock occupancy in a CPU side-table
contradicted the zero-transcode GPU tenet (occupancy moved in-blob; ownership rule added) ·
RocksDB-as-sole-default was wrong for casual single-player worlds and carried an unexamined
LGPL×license question (SQLite-class zero-config default restored; RocksDB opt-in; license ADR) ·
GNS does not provide signaling or off-Steam relay (operated rendezvous/TURN infrastructure named)
· the Part I "liquids = per-step scan" survey claim was wrong (already a budgeted event queue;
contribution reframed) · event-bus/batched dispatch changes observable callback ordering (legacy
semantics remain default; changes are documented porting items).

**Ecosystem & UI (5):** SSCSM "instruction-budgeted, watchdogged" was imprecise (cooperative
interrupts can't bound native calls → external wall-clock watchdog; side channels routed to the
WASM tier; "skeleton" corrected to "working scaffold") · the formspec transpiler scope ignored
stateful parser directives (46 handlers, not 43; stateful directives enumerated as hand-port) ·
"UI executes in the SSCSM isolate" had no host for serverless surfaces (local menu isolate named)
· ContentDB-2 was assumed as a program deliverable (relabeled external dependency) · the
Risk-2 mitigation as originally written leaned on it (reweighted to engine-owned legs).

**Program & consistency (8):** "every phase ships" was not falsifiable (gates now cite the Part
VII budgets + converter parity; Phase 1 honestly labeled a trunk fork) · reviewer-capacity data
added to Risk 1 · 1.x maintenance tax concretized (Risk 6) · golden-image "per GPU vendor in CI"
not deliverable on GitHub-hosted runners (software-raster subset is the CI gate; vendor goldens
are scheduled benches) · DLSS version labels drifted across sections (pinned to 4.5/Streamline
2.x; hardware-capability vs integration distinguished) · perf-table denominator contradicted the
prose (24 vs 32) · stale Android-tiering sentence in the context superseded by the scope decisions
· determinism harnesses were listed under testing strategy but absent from the blocking CI-gate
list (promoted).

## Refuted findings (9)

Kept for the record — a finding being plausible is not the same as it being right.

| Finding | Why refuted |
|---------|-------------|
| Perf table "leans on frame generation, inverting its latency math" | Misread: DLSS Quality upscaling reaches the 120–165 figure; FG is explicitly "headroom *beyond*" to saturate the 165 Hz display — the latency-honest use the reviewer themselves recommended |
| Mesh-shader "~10×" claimed as default-path/compounded with mapgen gains | Already scoped: render-side only, flagship-fast-path only, "where present", with the MDI floor in the same bullet; the compounding was the reviewer's inference across unrelated sections |
| "Job-system migration one pool at a time is incoherent under the shared env lock" | First migrated pool (meshing) never touches `m_env_mutex` (client-side, snapshot-based); sharded locking is sequenced before env-coupled pools migrate |
| Morton ordering "fights" greedy meshing's planar scans | Conflates the zero-transcode storage blob with the mesher's read pattern; a 16³ chunk (4–8 KB) sits in L1 during meshing — traversal order has no DRAM penalty |
| Capability sandbox "rests on the fenv mechanism Luau penalizes" | Luau's native sandbox (`luaL_sandbox`/`luaL_sandboxthread`, fresh global table per script) is the *replacement* for fenv and is performance-positive — a closer fit, not a bigger re-architecture |
| "Typed API can't be generated — registrations carry no types" | Attacks the legacy PUC bindings the clean break discards; the design states schema-first greenfield bindings with drift-fails-CI |
| "Clean break is a relicensing-class change needing 144+ copyright holders' consent" | Conflates rewriting/replacing code *under the same license* (LGPL expressly permits) with changing license terms (not proposed) |
| "GNS effectively unmaintained for four years; worse bus-factor than LuaJIT" | Conflates tagged-release cadence with activity: master commits continued through 2022–2024 and v1.5.0 shipped Apr 2026; LuaJIT by contrast is permanently frozen at 5.1 |
| "Supply-chain gate can't absorb the dependency flood" | The phasing already staggers dependencies across five phases with the gate first; the capacity figures cited were imported from outside the document's stated scope |

## Process note

Reviewers were explicitly barred from attacking the user's chosen premises (clean break,
flagship-first, document-only deliverable) — attacks on *whether the design delivers on those
premises honestly* were in scope, attacks on the premises themselves were not. Verifier verdicts
included refutations of reviewer overclaims even within confirmed findings; the resolutions
adopted are the verifier-validated versions, not the raw findings.
