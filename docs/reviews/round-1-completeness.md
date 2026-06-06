# Adversarial review — round 1 (completeness critique)

**When:** 2026-06-05, before final synthesis.
**Method:** four independent design lenses (graphics, CPU runtime, modding/content, networking)
produced 43 proposals against a 9-subsystem code survey of the Luanti 5.17-dev tree plus two
June-2026 state-of-the-art research sweeps. One adversarial completeness critic then reviewed the
combined design with read access to the source tree, hunting omissions, cross-lens conflicts, and
claims contradicted by the survey.

**Result: 18 findings, all resolved in the synthesized architecture.**

## Findings and resolutions

| # | Finding | Resolution in the architecture |
|---|---------|--------------------------------|
| 1 | Audio surveyed in depth, absent from every design lens | Audio pillar added (§7): miniaudio + Steam Audio, voxel-derived acoustics, Opus/FLAC, music director, optional voice |
| 2 | Formspec/UI overhaul — the #1 upstream roadmap item — had no design | UI pillar added (§6): reactive widget tree, Luau DSL, GPU vector renderer, unified HUD/menu/world surfaces |
| 3 | Mapgen modernization missing despite being the natural parallel/GPU beneficiary | Mapgen redesign added (§3): staged parallel pipeline, SIMD/GPU noise, async snapshot-isolated Lua hooks |
| 4 | Persistence strategy missing: content_t u16 cap, LOD-cache schema, broken Postgres CI never addressed | World-data pillar added (§3): palette chunks + u32 registry, embedded default + RocksDB opt-in, LOD column family |
| 5 | Input (upstream roadmap item) undesigned: no haptics, gestures, IME | Input rearchitecture added (§6): SDL3 raw layer + action mapping, haptics, gestures, real IME |
| 6 | Graphics RHI choice (SDL3 GPU/Slang precompiled) conflicted with the web-client goal — an unfunded fifth backend | Resolved by descoping: web client out of scope (flagship-first); transport/RHI abstractions leave the door open |
| 7 | Three uncoordinated identity changes (entity u16→u32, player→UUID, GUID tables) across lenses | Unified identity registry (§3): content u32 + entity u64 + player UUIDv7, one subsystem, Contract 3 |
| 8 | Client prediction demands unified collision math, but Jolt proposal threatened determinism | Jolt fenced to non-predicted dynamics only; shared deterministic collision step compiled into both binaries (§2, Contract 5) |
| 9 | Zero-copy LuaJIT-FFI VoxelManip existed on only one of three candidate VMs; async bytecode not portable | Replaced with a VM-portable buffer API — *itself corrected again in round 2* (Luau buffers cannot alias external memory → single-memcpy snapshots) |
| 10 | Shared-GL-context worker uploads contradicted the GL retirement and driver fragility | Dropped entirely: Vulkan transfer-queue uploads only (§1) |
| 11 | MTU-raise rationale overclaimed (media cap derives from the reliable window, not MTU) | Corrected; moot under the clean break — MTP is replaced by GNS (§5) |
| 12 | "Async workers = cores−2 / ~6 cores used" misquoted the code; real ceiling is env-lock serialization | Motivation re-anchored on `m_env_mutex` contention (§2); async pool sizing claim dropped |
| 13 | No supply-chain posture despite tripling the dependency surface (incl. proprietary DLSS blobs in an LGPL engine) | Supply-chain-first ops pillar (§8): SBOM, license CI gate, blob quarantine, pinning policy |
| 14 | No crash reporting/telemetry modernization (MSVC-SEH-only today) while the redesign adds crash risk | Crashpad + opt-in aggregate telemetry + OTel observability (§8) |
| 15 | Steam Deck/handheld/power tiers claimed as reachable but never designed | Power/thermal governor with device-class presets (§8) |
| 16 | Far-LOD render and far-LOD network streaming mutually dependent with no shared schema owner | LOD aggregate pyramid as a first-class persisted artifact owned by world-data (§3, Contract 2) |
| 17 | Deferred-lighting phasing ignored the foliage/transparency conflict it admitted | Clustered forward+ chosen as the unified shading path (§1) |
| 18 | i18n regression risk and accessibility unaddressed across a UI rewrite | Accessibility structural in the widget node (AccessKit) + Fluent i18n bound into the UI design (§6) |
