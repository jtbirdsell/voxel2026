# Pinned shader toolchain: Slang

The actionable form of ADR-0002's "version-pinned through the supply-chain gate"
(review-mandated — a version string without an obtainable artifact is not a pin).

| | |
|---|---|
| Project | [shader-slang/slang](https://github.com/shader-slang/slang) |
| Release tag | `v2026.10.2` (official release binaries) |
| Windows asset | `slang-2026.10.2-windows-x86_64.zip` |
| SHA-256 (zip) | `F21FCA4BA78BFB366EF3B282B4926E3EFCA61C2FFD72A482B024C9F8413331D0` |
| Vendored? | **No** — developer-machine tool only; the repo commits the *outputs* (`.spv` beside their `.slang`/`.comp` sources), never the compiler |
| Obtain | `gh release download v2026.10.2 -R shader-slang/slang --pattern "*<platform>*"`, verify the hash, extract anywhere, use `bin/slangc` |

## Usage (regeneration commands)

```sh
# Kernels (committed .spv consumed by the build via configure-time embed):
slangc parity.slang -target spirv -o parity.slang.spv -reflection-json parity.slang.reflection.json

# Stage-emission probes (committed .spv as ADR-0002 evidence; not embedded):
slangc meshprobe.slang  -target spirv -o meshprobe.spv
slangc stageprobe.slang -target spirv -o stageprobe.spv

# Mesh-pipeline EXECUTION kernel (issue #16; embedded; mesh + fragment entries):
slangc meshexec.slang -target spirv -o meshexec.spv
```

GLSL companion (parity.comp) uses glslang — obtained via `vcpkg install "glslang[tools]"`
(validated with glslang 16.3.0): `glslangValidator -V parity.comp -o parity.comp.spv`.

## Upgrade procedure

New Slang version = supply-chain event: update this table (tag/asset/hash), regenerate all
`.spv` + reflection artifacts, run the full suite on a GPU host (both parity kernels must still
MATCH), and note the bump in the commit message. ADR-0002's exit strategy (DXC/GLSL fallback)
applies if an upgrade breaks emission.
