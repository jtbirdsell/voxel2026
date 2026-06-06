# Contract 6 — Capability taxonomy

**Owner:** scripting · **Consumers:** UI (SSCSM-hosted), audio (voice grant), networking
(http/connect grants), engine ops (review tooling) · **Status:** Draft

## What this contract freezes

- The grant vocabulary mods declare in mod.conf — initial set: `fs:read:<path>`, `fs:write:<path>`,
  `net:http:<host>`, `net:connect:<host>`, `storage`, `voice`, `insecure` (trusted-native escape
  hatch, server-operator-approved only).
- Deny-by-default; per-mod isolation (Luau per-script environments, not a shared `_G`).
- SSCSM tier mapping: which grants are *ever* available to server-sent client scripts (strict
  subset; no fs, no arbitrary net), and what routes to the WASM hardened tier instead.
- Grant review policy: capabilities are reviewed like permissions, not config (CONTRIBUTING.md
  security discipline); ContentDB-2 surfaces declared grants to users pre-install.

## Open questions

- Granularity of fs/net scoping (glob paths? host wildcards?) vs auditability.
- Migration inference from 1.x `secure.*` settings for ported mods.
