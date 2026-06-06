# Contributing

This project holds itself to the engineering quality standards defined in the architecture
document (Part VII). They are non-negotiable and apply from the first commit — to docs, to the
skeleton, and to any future engine code. Summary:

## Definition of done (every change)

- Reviewed before merge.
- Tests included in the same change — no "tests later".
- Docs updated in the same change.
- No `TODO` as a substitute for a tracked issue.
- Performance-relevant changes ship with before/after measurements.

## Testing requirements

- **Unit tests** (Catch2) accompany every new component.
- **Property-based / round-trip invariants** for every codec or format
  (serialization, palette pack/unpack, coordinate rebasing, identity registry):
  encode→decode equality is the property.
- **Determinism harnesses** where the design demands them (mapgen output identical across
  1/4/32 workers; prediction replay bit-identical across client/server builds).
- **Fuzzing** is mandatory for every parser and untrusted-input surface before it merges.

## CI gates (blocking, not advisory)

- ASan + UBSan on every PR; TSan on anything touching threading or locks.
  *Status: live — `linux-gcc-asan` and `linux-clang-tsan` jobs in build.yml run the full suite
  with `VOXEL2026_SANITIZE=address|thread` (UBSan non-recoverable, so findings fail the build).*
- Determinism harnesses block changes touching the shared collision step, FP flags, mapgen,
  or the job system.
- Performance-regression benchmarks with explicit budgets fail the build on regression.
- License/SBOM gate: every new dependency needs a pinned version, a license check, and an ADR
  documenting why it was chosen, its maintenance posture, and the exit strategy.

## Design discipline

- An **ADR** (`docs/adr/`) for every dependency and every architecturally significant decision.
- An **RFC** (issue with the RFC template) with a review window for public API, protocol, or
  format changes.
- The six frozen contracts (`docs/contracts/`) change only by RFC.

## No prototype laundering

Experimental paths live behind dev flags and cannot graduate to default without meeting the full
bar above — same tests, same docs, same review. *"It works on the 4090" is not a definition of
done.*

## Commit messages

Present tense, capitalized, ≤ 70 characters, no trailing period, second line blank
(matching upstream Luanti convention).
