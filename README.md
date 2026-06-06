# voxel2026

**A clean-break modernization architecture for a Luanti-class voxel engine, designed to June-2026 standards.**

[![Build](https://github.com/jtbirdsell/voxel2026/actions/workflows/build.yml/badge.svg)](https://github.com/jtbirdsell/voxel2026/actions/workflows/build.yml)
[![Docs](https://github.com/jtbirdsell/voxel2026/actions/workflows/docs.yml/badge.svg)](https://github.com/jtbirdsell/voxel2026/actions/workflows/docs.yml)

## Status: design phase

This repository is the **program home** for a from-first-principles rearchitecture study of
[Luanti](https://github.com/luanti-org/luanti) (formerly Minetest): what a voxel engine looks like
if rebuilt for 2026-era hardware — Vulkan-first GPU-driven rendering, a work-stealing job runtime,
palette-compressed GPU-shareable world data, Luau scripting with capability sandboxing, encrypted
predicted networking — with a clean break from legacy formats and APIs.

What exists today:

- **[The architecture](docs/architecture.md)** — the full design, hardened by two adversarial
  review rounds (66 findings raised, every confirmed finding folded in, refutations documented).
- **[Review logs](docs/reviews/)** — both rounds, with verdicts and rationale.
- **[Frozen contracts](docs/contracts/)** — the six cross-cutting interface contracts that must be
  agreed before parallel implementation could start.
- **[ADRs](docs/adr/)** — architecture decision records for the load-bearing dependency and
  licensing decisions.
- **A compiling C++23 skeleton** with tests and CI — the quality bar exists from commit 1, not
  retrofitted. It is a skeleton, not a pretend engine.

## Honest framing

The architecture document is explicit about this (see its Part VI): full delivery is a
15–40 person-year program. This repo is the design artifact and program scaffolding — a fork
blueprint plus an upstream cherry-pick list — not a promise of a shipping engine. Implementation,
if and when it starts, follows the phase gates and quality standards in
[CONTRIBUTING.md](CONTRIBUTING.md), and quarried Luanti subsystems are imported under their
LGPL-2.1-or-later license with attribution (see [ADR-0001](docs/adr/0001-license.md)).

`voxel2026` is a neutral codename. This project is not affiliated with or endorsed by the upstream
Luanti project, and makes no claim on its name.

## Layout

```text
docs/architecture.md   The design: current-state survey, June-2026 state of the art,
                       the eight architecture pillars, performance targets, phasing, risks
docs/reviews/          Adversarial review rounds 1 and 2
docs/contracts/        The six frozen cross-cutting contracts (specs in progress)
docs/adr/              Architecture decision records
src/, tests/           C++23 skeleton + Catch2 tests (Morton codec — Contract 1 groundwork)
.github/workflows/     Build matrix (MSVC/GCC/Clang) and docs lint CI
```

## Building the skeleton

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Requires a C++23 compiler (MSVC 19.36+ / GCC 12+ / Clang 16+) and CMake 3.25+.

## Roadmap

Tracked as [milestones](https://github.com/jtbirdsell/voxel2026/milestones) (Phases 0–4) and
[issues](https://github.com/jtbirdsell/voxel2026/issues): the four Phase-0 spikes, the six
contracts, and the seeded ADRs.

## License

[LGPL-2.1-or-later](LICENSE) — see [ADR-0001](docs/adr/0001-license.md) for why.
