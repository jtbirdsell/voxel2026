# ADR-0008 — Freeze Contract 3: identity widths and encodings

**Status:** Accepted
**Date:** 2026-06-06

## Context

Contract 3 ([docs/contracts/contract-3-identity.md](../contracts/contract-3-identity.md)) fixes
the identity widths every other pillar encodes: networking serializes them, persistence indexes
by them, scripting validates against them. Unlike Contracts 4/5 it needed no spike — the
decisions are schema choices — but a prose-only freeze would pin bit layouts nobody compiles.
The freeze therefore ships with reference codecs (`src/world/identity.hpp`) and layout tests,
including the RFC 9562 appendix vector, so the schema is CI-gated fact.

## Options

1. **Freeze now with compiled reference codecs** — widths and encodings become testable facts;
   allocation policy (generation bumps, entropy, registry implementation) stays explicitly
   outside the freeze as Phase-1 engine work. Risk: a layout choice proves awkward at engine
   scale — mitigated by the narrowness of what is frozen (pure bit layout, no behavior).
2. **Freeze prose only** — faster, but the first implementation becomes the de-facto spec and
   any divergence from the text is discovered at integration time instead of in CI.
3. **Defer until the ECS and registry exist** — blocks the network protocol and save format
   (both need widths) on Phase-1 runtime work; the contract system exists to prevent exactly
   this serialization of pillars.

Width choices considered inside option 1: entity handles as u64 {gen:32|idx:32} (EnTT's 64-bit
convention, zero-translation boundary) vs {gen:24|idx:40} (more indices, custom traits
everywhere) — EnTT alignment wins, 4.29B live indices is not the binding constraint; player
keys as UUIDv7 (time-ordered, index-local) vs UUIDv4 (opaque, no timestamp leakage) — v7 wins
with the leakage accepted and documented as non-sensitive metadata.

## Decision

Option 1. The contract text distinguishes frozen *layouts* from deliberately unfrozen
*allocation policy*, resolves both stub questions (compaction rejected for v1; timestamp
leakage accepted) with falsifiable revisit triggers, and binds the codecs + tests as the
freeze's enforcement mechanism.

## Exit strategy

Every layout change is an RFC that must update contract text, reference codec, and tests in one
change — drift between the three fails CI by construction. The two named revisit triggers
(registry-manifest growth past 10 MB or id-exhaustion projection; a privacy ruling on creation
time) each come with their sketched exit (remap epoch in the save format; v4 keys for new
players), so re-opening is costed, not open-ended.
