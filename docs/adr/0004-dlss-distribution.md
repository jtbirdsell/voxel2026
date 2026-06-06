# ADR-0004 — DLSS/Streamline distribution policy

**Status:** Proposed
**Date:** 2026-06-05

## Context

The temporal stack (§1) integrates upscalers through Streamline, with FSR as the cross-vendor
default and DLSS as an optional plugin. Round-2 review established the precise legal shape: the
LGPL "separable plugin" framing answers the *linkage* question but not the *redistribution*
question. NVIDIA's SDK EULA permits distributing DLSS **incorporated in object-code form into an
application with material additional functionality**, but forbids standalone redistribution and
any use that would subject the SDK to an open-source license. The DLSS-G (frame generation)
plugin ships as a prebuilt closed DLL only.

## Decision (proposed)

- DLSS/Streamline prebuilt blobs are **never vendored into this source repository**.
- The plugin is **separable, dynamically loaded, off-by-default**; the engine core remains pure
  LGPL + permissive.
- Per-channel distribution: **bundled** into Steam/own-installer builds (EULA-permitted in-app
  object-code distribution); **runtime-fetched via the extra-data mechanism on Flathub** (keeping
  the Flathub repo blob-free, following the established OpenMW/nvidia-flatpak pattern); distro
  packages ship without it.
- FSR (and XeSS where its SDK terms allow) are the engine-shipped upscalers.

## Exit strategy

The Streamline hook surface is vendor-agnostic; removing the DLSS plugin removes a directory and
a download endpoint, not engine code. If NVIDIA's terms tighten, the FSR default is unaffected.
