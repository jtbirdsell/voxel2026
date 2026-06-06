# Contract 6 — Capability taxonomy

**Owner:** scripting · **Consumers:** UI (SSCSM-hosted), audio (voice grant), networking
(http/connect grants), engine ops (review tooling) · **Status:** FROZEN 2026-06-06
([ADR-0011](../adr/0011-contract-6-freeze.md)) — changes by RFC with security re-review

## Normative invariants

1. **Deny-by-default.** No grant, no access; grants are additive; there are no negative grants
   in v1. Per-mod isolation (Luau per-script environments, never a shared `_G`) is the
   enforcement substrate the grants attach to.
2. **Grammar (frozen; ASCII-only by design):** a capability is 1–8 colon-separated segments,
   each 1–63 chars of `[a-z0-9_.*-]` with sound dot-label structure (no leading/trailing/empty
   labels), total ≤ 255 chars. **Non-ASCII is rejected in v1** — no IDN, hence no homograph
   space to defend (falsifiable revisit trigger: a real need for IDN hosts, which then enters
   only as punycode with its own security review). Hierarchy is expressed in segments — fs
   paths map path components to segments (`fs:read:worlds:save_1`); `/` is not in the grammar.
3. **Wildcards have exactly two shapes**, legal in grants only (requests name concrete things,
   wildcard-free, pinned by test): a whole segment `*` (any single segment) or a leading
   `*.`-label (`net:http:*.example.com`). Never in the namespace segment — a bare `*` grant
   does not exist. Never mid-label (`ap*.x` is malformed).
4. **Subsumption (THE security check, frozen):** grant covers request iff the grant's segments
   match a *prefix* of the request's segments — a shorter grant is a broader grant
   (`fs:read` covers `fs:read:worlds:save_1`) — with `*.suffix` anchored at a **dot-label
   boundary with at least one extra label**: `*.example.com` matches `api.example.com` and
   `a.b.example.com` but NOT `example.com` (apex), NOT `evilexample.com` (no boundary), NOT
   `x.example.com.evil.net` (suffix-anchored, not substring). The adversarial truth table is
   compile-time fact (`STATIC_REQUIRE`) in `tests/test_capability.cpp`.
5. **Fail closed.** A malformed grant covers nothing; a malformed or wildcarded request is
   granted nothing — `capabilityGrantCovers` returns false on every grammar violation, never
   guessing (pinned by test).
6. **Initial vocabulary** (namespaces are append-only; new namespaces are additive, never
   reinterpretations): `fs:read:<path-segments>`, `fs:write:<path-segments>`,
   `net:http:<host>`, `net:connect:<host>`, `storage`, `voice`, `insecure` (trusted-native
   escape hatch, server-operator-approved only, never grantable from mod.conf alone).
7. **SSCSM tier rule:** server-sent client scripts get **no ambient-I/O capabilities ever** —
   no `fs:*`, no `net:*` (server-mediated requests route through the server's own grants);
   the client isolate's surface is UI/intent/`storage` (server-scoped). Anything needing more
   on a hostile-server threat model routes to the WASM hardened tier (architecture §4), not to
   a wider Luau grant.
8. **Review posture:** capabilities are reviewed like permissions, not config (CONTRIBUTING
   security discipline); ContentDB-2 surfaces declared grants pre-install — which is why
   grants are flat strings a human can read, not a policy language.

## Decisions (previously open, resolved at freeze)

- **Scoping granularity vs auditability:** segment-prefix subsumption with the two anchored
  wildcard shapes — expressive enough for directory-tree and subdomain scoping, small enough
  that the entire matching semantics fit in one screen of frozen code and a compile-time truth
  table. Full glob patterns rejected: un-auditable grants and a matcher too rich to freeze
  safely (every added wildcard form is attack surface).
- **1.x `secure.*` migration:** mechanical converter mapping — `secure.http_mods` listing →
  `net:http:*` for those mods (deliberately broad, **flagged for manual narrowing** in the
  converter report), `secure.trusted_mods` → `insecure` (same operator-approval bar as 1.x).
  The detailed table lands with the porting guide (tracked there, not silently dropped).

## Reference codec (the compiled anchor)

`src/script/capability.hpp`: grammar validation (grants and requests) and
`capabilityGrantCovers` — all constexpr. `tests/test_capability.cpp`, by invariant: grammar
accept/reject table including every wildcard-shape rejection (2, 3); prefix-subsumption truth
table (4); the host-wildcard attack rows — apex, boundary-less, suffix-substring, hyphen
confusion, short suffixes (4); length/depth bounds (2); fail-closed rows for malformed grants
and wildcarded requests (5).

## CI gates

- `tests/test_capability.cpp` runs in every build, all legs; the truth table is
  `STATIC_REQUIRE`d, so a semantics change cannot even compile without touching the pinned
  rows.
- Any change to the grammar, wildcard shapes, or subsumption semantics is an RFC with a
  security re-review (threat-model update mandatory); vocabulary additions are additive RFCs.
