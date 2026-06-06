# ADR-0003 — Persistence: SQLite-class default, RocksDB opt-in, license election

**Status:** Accepted (2026-06-06)
**Date:** 2026-06-05

## Context

The architecture (§3) originally made RocksDB the sole embedded default. Round-2 review confirmed
two problems: (a) RocksDB's LSM write-stalls and compaction threads are a poor fit for casual
single-player worlds on one disk — the dominant use case — versus zero-config public-domain
SQLite; (b) RocksDB is dual-licensed GPLv2 / Apache-2.0, and Apache-2.0 is GPLv2-incompatible,
which requires a deliberate license election against the engine's LGPL-2.1-or-later core.

## Validation (2026-06-06 — ratification facts, verified)

- **Dual license confirmed current**: facebook/rocksdb carries `COPYING` (GPLv2) and
  `LICENSE.Apache` side by side (dual-licensed since July 2017); the Apache-2.0 election this
  ADR makes remains available.
- **Maintenance posture healthy**: releases 11.1.1 (2026-04-29), 11.0.4 (2026-04-07), 11.1.0
  (2026-03-23) — an active monthly-class cadence, not a single-maintainer trickle.
- **Compatibility chain unchanged**: Apache-2.0 → (L)GPL-3.0 one-way compatibility is settled
  license mechanics; the engine's LGPL-2.1-**or-later** core provides the upgrade path. The
  GPLv2 arm stays un-elected (Apache-2.0 ↔ GPLv2 incompatibility is exactly why the election
  must be deliberate and recorded — this document is that record).

## Decision

- **Default backend: zero-config single-file embedded store (SQLite-class)** for casual and
  single-player worlds.
- **RocksDB: opt-in** backend for large worlds and dedicated servers, using the column-family
  layout in Contract 1/2 (voxels, metadata, timers, static-objects, light-heightmap,
  lod-aggregate).
- **License election:** consume RocksDB under its **Apache-2.0** arm, compatible with the engine
  via the LGPL-2.1-**or-later** upgrade path (LGPL-3.0 ↔ Apache-2.0 compatibility) — not the
  GPLv2 arm.
- Thin persistence interface retained (also serves Postgres operators).

## Exit strategy

The thin interface bounds the blast radius: backends are swappable per-world. If RocksDB's
maintenance or footprint becomes a liability, large-world deployments fall back to the embedded
default plus Postgres; the column-family schema maps onto any KV store with prefix iteration.
