# ADR-0013 — SQLite amalgamation as the zero-config default store

**Status:** Accepted
**Date:** 2026-06-06

## Context

ADR-0003 elected the persistence shape: a zero-config single-file embedded default for casual
and single-player worlds, RocksDB opt-in for large worlds/servers, both behind a thin
interface. Issue #19 lands the interface and the default backend. Per Part VII, the dependency
enters through the supply-chain gate.

## Options

1. **SQLite amalgamation, URL + hash pinned** — public domain (no license arm to elect, no
   SBOM complication), one C file, the most widely deployed storage engine on earth,
   bit-stable file format with a documented compatibility promise. Pinned: **3.53.2**,
   `sqlite-amalgamation-3530200.zip`, SHA3-256
   `81142986038e18f96c4a54e1a72562ae17e502a916f2a7701eff43388cbf1a40` (sqlite.org).
2. **RocksDB now** — the opt-in backend brought forward. Heavy build (minutes per CI leg ×9),
   LSM compaction machinery the default use case explicitly does not want (ADR-0003's whole
   point), and a license election already recorded but no current consumer needing it.
3. **Custom file format** — owning page management, crash atomicity, and corruption recovery
   by hand; the graveyard of world-eating bugs every voxel engine eventually visits. Rejected
   on the same grounds as the hand-rolled scheduler.

## Decision

Option 1. The `world::KvStore` interface is the load-bearing artifact — column-family-shaped
spaces, atomic batches, ordered prefix scans (lexicographic byte order, which is exactly what
Contract 2's big-endian sign-flipped storage keys were designed for) — and SQLite is one
implementation behind it, alongside an in-memory reference backend that doubles as the
conformance oracle (every backend must agree with it, test-enforced).

**RocksDB is deferred, not dropped:** the interface is column-family-shaped now precisely so
the opt-in backend slots in without reshaping consumers. Falsifiable trigger to build it: a
measured world whose write pattern or size degrades the SQLite default past the Part VII
budgets, or the first dedicated-server deployment profile.

**Compression at rest is deferred — stated as a divergence, not implied away** (review
finding): Contract 1's invariant 6 framed disk as "compressed at rest"; this implementation
stores blobs uncompressed, and the contract text now scopes the invariant accordingly (layout
identity is frozen; the at-rest codec is persistence policy). Named reason: Contract-1 blobs
are already palette-compressed (a uniform 16³ chunk is 32 bytes); the codec election (zstd is
the architecture's standing answer) is its own supply-chain event when measured world sizes
justify it — and adding it later is a per-value transform behind this interface, not a format
migration.

**Deliberate defaults worth naming** (review-requested):

- Journal mode stays SQLite's default (DELETE) — correct for the v1 single-writer contract;
  the async write-back layer (architecture §5, future work) owns `PRAGMA journal_mode=WAL`
  when it owns the store and its readers.
- The SHA3-256 `URL_HASH` requires CMake ≥ 3.24 (the project floor is 3.25); on older CMake
  the keyword would be silently ignored — the floor is therefore itself part of the
  supply-chain guarantee.
- The store verifies header-vs-runtime SQLite version agreement at open (a search-path
  override substituting another SQLite fails loudly).

## Exit strategy

The interface bounds the blast radius per ADR-0003: backends are swappable per-world; the
conformance suite transfers to any successor unchanged. SQLite itself carries the weakest
exit risk of any dependency in the program (public domain, 20+year format stability,
amalgamation vendorable in extremis).
