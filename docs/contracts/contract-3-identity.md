# Contract 3 — Identity (content, entities, players)

**Owner:** world data · **Consumers:** networking (wire widths), scripting (handle semantics),
persistence (registry, player tables) · **Status:** Draft

## What this contract freezes

- **Content:** `mod:name` → u32 global ID via a world-scoped, append-only registry with
  tombstones, persisted once in the world manifest (no per-block name-id maps).
- **Entities:** u64 {generation, index} runtime handles aligned with EnTT, plus persistent entity
  UUIDs for anything saved or referenced across sessions; documented resolve path between the two.
- **Players:** UUIDv7 primary keys; display name is a mutable attribute.
- **Validation semantics (review-mandated):** only generation-validated handles or persistent
  UUIDs ever cross the Lua/protocol boundary — never a raw recyclable index. Every API accepting a
  handle validates generation and returns a typed "gone" result, preserving the 1.x
  null-on-removal guarantee.

## Open questions

- Registry compaction/GC story for worlds with heavy mod churn (append-only growth bound).
- UUIDv7 creation-time leakage: acceptable, or mask the timestamp bits for players?
