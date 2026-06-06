# ADR-0005 — GameNetworkingSockets and the operated-infrastructure posture

**Status:** Accepted (2026-06-06)
**Date:** 2026-06-05

## Context

The transport (§5) is GameNetworkingSockets: encrypted (AES-GCM-256), QUIC-style congestion
control, message lanes. Round-2 review pinned the honest scope: GNS ships a dependency-free
ICE/STUN hole-punch client, but **no signaling service and no relay fallback off-Steam** (Steam
Datagram Relay is Valve-proprietary). For player-hosted servers — this ecosystem's norm — P2P
session setup needs a rendezvous service, and symmetric-NAT clients need TURN relays or they
cannot hole-punch.

## Validation (2026-06-06 — ratification facts, verified)

- **Maintenance**: v1.5 shipped after a near-four-year tag hiatus and the repository is under
  active development with cross-platform CI, kept in sync with the Steam codebase — the round-2
  review's "tag cadence is not activity" verdict stands, now with a tagged release too.
- **The infrastructure gap is unchanged**: SDR remains Valve-proprietary; GNS still ships the
  ICE/STUN client without a signaling service or off-Steam relay — this ADR's
  rendezvous/TURN/direct-IP posture remains the honest scope.

## Decision

- **Direct IP / port-forward connection remains fully supported** (as in Luanti 1.x) — no
  operated infrastructure is *required* to play.
- A **lightweight self-hostable rendezvous/signaling service** is specified alongside the server
  list, for NAT-traversal session setup; community/project instances are optional conveniences.
- TURN relay support is optional and config-pointed (operators or communities run their own);
  the engine never depends on a single central relay.
- The optional WebRTC-ICE backend (which adds TURN at the cost of WebRTC+protobuf) is compile-out
  and not the default.

## Exit strategy

The transport sits behind the retained connection abstraction; GNS-specific code is one backend.
If GNS maintenance falters, candidates (yojimbo-class libraries, QUIC stacks) slot behind the same
abstraction; the rendezvous protocol is engine-owned and transport-agnostic.
