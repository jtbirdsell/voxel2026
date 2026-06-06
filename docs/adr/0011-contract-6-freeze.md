# ADR-0011 — Freeze Contract 6: capability grammar and subsumption

**Status:** Accepted
**Date:** 2026-06-06

## Context

Contract 6 ([docs/contracts/contract-6-capabilities.md](../contracts/contract-6-capabilities.md))
is the security boundary every sandboxed surface shares: mod.conf grants, SSCSM isolates, UI
intents, voice. The check that matters is subsumption — "does grant G cover request R" — and
its failure modes are the classic permission-matcher vulnerability class (suffix-substring
confusion, apex leakage, homograph hosts, fail-open on malformed input). A prose taxonomy
without a frozen matcher would leave each consumer to reimplement exactly those bugs.

## Options

1. **Segment-prefix model with two anchored wildcard shapes, frozen as constexpr code** —
   small enough to audit in one sitting, expressive enough for directory-tree and subdomain
   scoping, attack table pinned at compile time. Costs expressiveness: no mid-string globs,
   no regex, no negative grants.
2. **Glob/regex patterns** — expressive, familiar, and the standard source of permission
   bypasses; grants become un-auditable on the ContentDB surface ("`net:http:*example*`" reads
   as safe to a user and is not), and the matcher is too rich to freeze with a finite truth
   table.
3. **OAuth-style flat scope strings (no hierarchy)** — trivially auditable but forces either
   thousand-entry vocabularies or over-broad grants; host scoping (the one place users most
   need narrowness) becomes all-or-nothing.

Within option 1: ASCII-only v1 (reject, don't fold, non-ASCII — no IDN means no homograph
space; punycode enters later only with its own review) vs accept-and-normalize (imports the
entire IDN confusable problem on day one) — reject wins. Wildcards in grants only (requests
name concrete things) — a wildcard request is a category error the API should refuse, not
resolve.

## Decision

Option 1, with the matcher itself part of the frozen contract: `capabilityGrantCovers` in
`src/script/capability.hpp` is the single arbiter every consumer calls — there is deliberately
nothing to reimplement, which is how permission matchers stay bug-for-bug identical across
surfaces. The adversarial truth table is `STATIC_REQUIRE`d, so semantic drift cannot compile.

## Exit strategy

Vocabulary growth is additive (append-only namespaces, plain RFCs). Semantics changes — new
wildcard shapes, IDN, negative grants — are RFCs with mandatory threat-model updates and
security re-review, extending the compile-time truth table first (the new rows are the
review artifact). If the model proves too narrow, the documented fallback is scoped extension
of the segment grammar, never a swap to globs/regex; the truth-table discipline transfers to
whatever extends it.
