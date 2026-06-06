# ADR-0001 — License: LGPL-2.1-or-later

**Status:** Accepted
**Date:** 2026-06-05

## Context

voxel2026 is a clean-break architecture program for a Luanti-class engine. The architecture's
phasing assumes subsystems may be quarried from the Luanti source tree (LGPL-2.1-or-later) when
implementation starts — importing that code is only possible into a compatible license. The
repo is public from day one.

## Options

1. **LGPL-2.1-or-later** — identical to upstream Luanti. Any Luanti code can be imported with
   attribution, zero relicensing questions; the "or-later" clause additionally permits electing
   LGPL-3.0 terms, which restores compatibility with Apache-2.0 dependencies (relevant to
   RocksDB — see ADR-0003).
2. **MIT/Apache-2.0** — maximally permissive, but forecloses importing any upstream Luanti code,
   contradicting the program's own phasing (fork blueprint + cherry-pick list).
3. **GPL-3.0** — copyleft-stronger, but needlessly restricts embedding and diverges from the
   ecosystem this program is designed to serve.

## Decision

**LGPL-2.1-or-later**, matching upstream. Quarried Luanti subsystems are imported under their
existing license with attribution preserved. Original code in this repo is contributed under the
same terms.

## Exit strategy

License changes to *new* original code remain possible while contributor count is small; imported
LGPL code is permanent. The decision is deliberately aligned with the most-likely code source, so
exit pressure is minimal.
