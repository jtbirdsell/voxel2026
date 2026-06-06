# Contract 3 — Identity (content, entities, players)

**Owner:** world data · **Consumers:** networking (wire widths), scripting (handle semantics),
persistence (registry, player tables) · **Status:** FROZEN 2026-06-06
([ADR-0008](../adr/0008-contract-3-freeze.md)) — changes by RFC only

## Normative invariants

1. **Content:** `namespace:name` → **u32** global ID via a world-scoped, **append-only registry
   with tombstones**, persisted once in the world manifest — no per-block name-id maps. ID **0 is
   reserved** (`kContentUnknown`): the one-way converter maps foreign/unrecognized content there;
   a registry never allocates it. Names match `[a-z0-9_]{1,64}:[a-z0-9_]{1,64}` (ASCII). Sourcing
   (verified against 1.x code): the namespace part equals Luanti's `MODNAME_ALLOWED_CHARS`
   exactly (`src/content/mods.h`); the name part is deliberately **stricter** than 1.x, whose
   registration check (`builtin/game/register.lua`, Lua `%w_`) also admits uppercase — v2 chooses
   lowercase-only canonical identity, and the one-way converter case-folds/disambiguates 1.x
   names that fall outside it (porting is the converter's guarantee, not charset identity).
2. **Entities:** **u64** runtime handles, `{generation:32 | index:32}` with the index in the LOW
   half — byte-for-byte the EnTT 64-bit entity convention the ECS substrate uses, so handle
   values never need translation at the engine/ECS boundary. The **all-ones pattern is the
   canonical invalid value** (EnTT null), and the **index field's domain is `[0, 2^32 − 2]`**:
   because EnTT's null comparison masks to the index field, a handle with index `0xFFFFFFFF`
   collides with null semantics at *every* generation — a schema fact about the field's domain
   (pinned by `handleValid()` and its tests), not an allocation policy. Anything saved or
   referenced across sessions uses a **persistent entity UUID** (UUIDv7, the same codec as
   player keys), never a handle.
3. **Players:** **UUIDv7** (RFC 9562) primary keys; display name is a mutable attribute (renames
   and Unicode names are presentation, never identity). Lexicographic byte order of v7 equals
   creation-time order — the index-locality property that motivated v7 over v4.
4. **Validation semantics:** only generation-validated handles or persistent UUIDs cross the
   Lua/protocol boundary — never a raw recyclable index. Every API accepting a handle validates
   the generation and returns a typed "gone" result, preserving 1.x's null-on-removal guarantee.
5. **Resolve path:** live calls use runtime handles; persistence uses UUIDs; the engine provides
   exactly one documented resolve in each direction (UUID → current handle or "gone"; handle →
   UUID for persistable entities). No third identity form exists on any API.
6. **Version/variant bits are codec-supplied, never caller-supplied** — UUID field inputs are
   masked to their schema widths (48/12/62 bits) so malformed input cannot smear into the
   version or variant fields (pinned by test).

## Reference codecs (the compiled anchor)

`src/world/identity.hpp` implements the bit layouts above (handle pack/unpack, UUIDv7
make/inspect/format/parse, name validation), exercised by `tests/test_identity.cpp` in every CI
build — including the **RFC 9562 appendix A.6 golden vector** and the EnTT bit-pattern golden.
Generation-bump policy and entropy sourcing are engine code (Phase 1), deliberately outside the
schema: the contract freezes *layouts*, not *allocation policy*.

## Policy notes (engine implementation, recorded here for cross-reference)

The allocation policy lives in `src/world/registry.hpp` (issue #18), outside this freeze, with
two properties worth naming against the schema:

- **Revival:** re-registering a tombstoned name revives the SAME id (bindings are permanent by
  invariant 1; the tombstone is an active flag) — removing and re-adding a mod brings its world
  content back, the 1.x name-id-map semantics worlds rely on.
- **Generation wrap (ABA bound):** the u32 generation wraps after 2^32 release cycles *of one
  slot*; a handle from 2^32 incarnations ago could then read as live. At one reuse per second of
  a single slot that is ~136 years; the bump-on-release policy keeps the collision window to the
  exact wrapped generation, not a range. Accepted and documented, not silent.

## Decisions (previously open, resolved at freeze)

- **Registry compaction/GC:** rejected for v1 — append-only with tombstones stands. The u32
  space and realistic churn make exhaustion a non-problem (even one new name registered every
  second, sustained around the clock, takes ~136 years to exhaust u32 — and real registries see
  thousands of distinct names over a world's lifetime, not one per second); manifest growth is
  linear in *distinct names ever seen*. Falsifiable revisit trigger: a real-world manifest exceeding
  10 MB of registry entries, or any measured id-exhaustion projection inside a decade — either
  re-opens compaction (which then needs a remap epoch in the save format) by RFC.
- **UUIDv7 creation-time leakage:** accepted for v1, stated plainly: the 48-bit timestamp makes
  account-creation time readable by anyone holding the UUID. It is treated as non-sensitive
  metadata (a game server already logs first-join times), and masking it would forfeit exactly
  the time-ordering/index-locality property v7 was chosen for. UUIDs are identifiers, not
  secrets or capabilities (RFC 9562's own guidance); auth never derives from knowing one.
  Falsifiable revisit trigger: a privacy requirement (policy or legal) classifying creation
  time as sensitive — the RFC path is then v4 keys for *new* players plus a migration note,
  since nothing in the schema couples other systems to the timestamp bits.

## CI gates

- `tests/test_identity.cpp` runs in every build: layout goldens (EnTT pattern, RFC A.6 vector),
  boundary round-trips, width masking, ordering, parse/format, charset table.
- Changing any width, bit position, reserved value, or charset rule is an RFC that must update
  the contract text, the reference codec, and the tests in the same change.
