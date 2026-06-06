# Frozen cross-cutting contracts

The six interfaces that must be agreed before parallel implementation could start
(architecture.md Part IV). Each contract is owned by one pillar, consumed by others, and changes
only by RFC once frozen.

| # | Contract | Owner | Consumers | Status |
|---|----------|-------|-----------|--------|
| 1 | [Packed voxel blob layout](contract-1-voxel-blob.md) | World data | Renderer, scripting buffers, persistence | **Frozen** ([ADR-0009](../adr/0009-contract-1-freeze.md)) |
| 2 | [LOD aggregate pyramid schema](contract-2-lod-pyramid.md) | World data | Renderer, networking, persistence | Draft |
| 3 | [Identity](contract-3-identity.md) | World data | Networking, scripting, persistence | **Frozen** ([ADR-0008](../adr/0008-contract-3-freeze.md)) |
| 4 | [Coordinates & origin rebasing](contract-4-coordinates.md) | Runtime | Everything | **Frozen** ([ADR-0006](../adr/0006-contract-4-freeze.md)) |
| 5 | [Shared collision step](contract-5-collision-step.md) | Runtime | Networking (prediction), scripting | **Frozen** ([ADR-0007](../adr/0007-contract-5-freeze.md)) |
| 6 | [Capability taxonomy](contract-6-capabilities.md) | Scripting | UI, audio (voice), networking | Draft |
