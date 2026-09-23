# glTF Fixtures and Scene Assets

## Reproducibility baseline

- Upstream: https://github.com/KhronosGroup/glTF-Sample-Assets
- Upstream revision: `9429648735279342b4c32b8745f7904196607379`
- vcpkg builtin baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`
- fastgltf port version: `0.9.0`
- Asset storage: small `.gltf`, `.glb`, and `.bin` fixtures are stored in ordinary Git, not Git LFS. Scene images and the two large scene `.bin` files listed in `.gitattributes` use Git LFS.

## Verified Linux environment

These versions record the environment used for the current verification.
They are not additional minimum project requirements.

- OS: Ubuntu 24.04.4 LTS, x86_64
- Compiler: GCC 13.3.0
- CMake: 3.28.3
- Ninja: 1.11.1
- Presets: `linux-debug`, `linux-release`

## Triangle

- Upstream:
  `Models/Triangle/glTF`
- Format: external `.gltf` + `.bin`
- Coverage: minimal indexed triangle, external buffer, structural inspection
- Index input: unsigned short
- License: CC0-1.0, see `Triangle/LICENSE.md`
- Git storage: ordinary Git

## Box

- Upstream:
  `Models/Box/glTF-Binary`
- Format: `.glb`
- Coverage: GLB container, mesh, material, node structure
- License: CC-BY-4.0, see `Box/LICENSE.md`
- Git storage: ordinary Git

## Triangle Without Indices

- Upstream:
  `Models/TriangleWithoutIndices/glTF`
- Format: external `.gltf` + `.bin`
- Coverage: non-indexed triangle and sequential index generation
- Expected source vertices: 3
- Expected generated indices: `0, 1, 2`
- Expected bounds: `(0, 0, 0)` to `(1, 1, 0)`
- Credit: Public / Marco Hutter
- License: CC0-1.0, see `TriangleWithoutIndices/LICENSE.md`
- Git storage: ordinary Git

## Box Interleaved

- Upstream:
  `Models/BoxInterleaved/glTF`
- Format: external `.gltf` + `.bin`
- Coverage: accessor byte offset, bufferView byte stride, and U16 indices
- Vertex layout: NORMAL at offset 0, POSITION at offset 12, stride 24
- Expected source vertices: 24
- Expected indices: 36
- Expected bounds: `(-0.5, -0.5, -0.5)` to `(0.5, 0.5, 0.5)`
- Credit: Cesium
- License: CC-BY-4.0, see `BoxInterleaved/LICENSE.md`
- Git storage: ordinary Git

## Simple Sparse Accessor

- Upstream:
  `Models/SimpleSparseAccessor/glTF`
- Format: external `.gltf` + `.bin`
- Coverage: sparse POSITION substitution and post-substitution bounds
- Source positions: 14
- Sparse replacements: 3
- Expected indices: 36
- Expected bounds after sparse substitution: `(0, 0, 0)` to `(6, 4, 0)`
- Credit: Public / Marco Hutter
- License: CC-BY-4.0, see `SimpleSparseAccessor/LICENSE.md`
- Git storage: ordinary Git

## Rendering scene assets

All files below are unchanged copies from the same pinned upstream revision
`9429648735279342b4c32b8745f7904196607379`. No material flags have been removed to
make an unsupported scene appear loadable. Import compatibility is separate from
resource download completeness.

| Asset | Entry point relative to this directory | Current CPU import | Local source size |
| --- | --- | --- | --- |
| MetalRoughSpheres | `MetalRoughSpheres/MetalRoughSpheres.gltf` | Supported | 11,231,050 bytes |
| DamagedHelmet | `DamagedHelmet/DamagedHelmet.glb` | Supported; existing GLB retained | 3,777,459 bytes |
| Sponza | `Sponza/Sponza.gltf` | Blocked: alpha mask and double-sided materials | 52,691,368 bytes |
| MetalRoughSpheresNoTextures | `MetalRoughSpheresNoTextures/MetalRoughSpheresNoTextures.gltf` | Blocked: double-sided materials | 373,757 bytes |

Sizes include each asset's upstream license, metadata, and `UPSTREAM.md` source
notes, but not GPU allocations. `UPSTREAM.md` preserves the upstream README
verbatim; its screenshots and repository-relative links refer to the upstream
repository, not additional local dependencies.

### MetalRoughSpheres (loadable material reference)

- [Pinned upstream model directory](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/9429648735279342b4c32b8745f7904196607379/Models/MetalRoughSpheres).
- [Exact glTF source](https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/9429648735279342b4c32b8745f7904196607379/Models/MetalRoughSpheres/glTF/MetalRoughSpheres.gltf).
- Contents: glTF, external binary, two 1024 x 1024 PNG textures; 6 nodes,
  5 meshes/primitives, 1 material, 255,914 vertices, 1,505,328 indices.
- Coverage: texture-driven metallic/roughness variation, multiple imported
  primitives, hierarchy transforms, material sharing, and PBR/IBL comparison.
- No extensions required; materials are opaque and single-sided. Tangents are
  absent, but no normal texture is used.
- Bounds after the asset's root transform, before an application transform:
  approximately `(-4.87464, -4.94378, -3.6)` to `(4.74989, 4.38798, 0.4)`.
  Do not apply the root's scale/axis conversion a second time.
- Credit: Ed Mackey, Analytical Graphics, Inc., 2017.
- License: CC-BY-4.0, see [license](MetalRoughSpheres/LICENSE.md) and
  [metadata](MetalRoughSpheres/metadata.json).

### DamagedHelmet (existing loadable textured model)

- [Pinned upstream model directory](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/9429648735279342b4c32b8745f7904196607379/Models/DamagedHelmet).
- [Exact GLB source](https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/9429648735279342b4c32b8745f7904196607379/Models/DamagedHelmet/glTF-Binary/DamagedHelmet.glb).
- Existing local GLB was verified against upstream Git blob
  `2cee76d76a752c92458a8a8294ff3658fdc9d6c7`; it was not replaced.
- Contents: 1 node/mesh/primitive/material, 5 embedded images, 14,556 vertices,
  46,356 indices.
- Coverage: a textured PBR model with base color, normal, metallic/roughness,
  occlusion, and emissive inputs.
- Upstream credits/licenses: ctxwing's rebuild/conversion is CC-BY-4.0;
  theblueturtle_'s earlier model is listed as CC-BY-NC-4.0. Preserve both notices;
  do not describe the whole asset as unconditionally commercial-use CC-BY.
  See [license](DamagedHelmet/LICENSE.md) and [metadata](DamagedHelmet/metadata.json).

### Sponza (downloaded, rendering support pending)

- [Pinned upstream model directory](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/9429648735279342b4c32b8745f7904196607379/Models/Sponza).
- [Exact glTF source](https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/9429648735279342b4c32b8745f7904196607379/Models/Sponza/glTF/Sponza.gltf).
- Contents: complete glTF, binary and 69 image files; 1 node, 1 mesh,
  103 primitives, 25 materials. Upstream screenshots are not downloaded.
- Intended coverage: architectural occlusion, multi-material geometry, alpha
  masking, double-sided surfaces, and later shadow/deferred-rendering tests.
- Materials 0, 3, and 20 use both `MASK` and `doubleSided`. Current `gltf_inspect`
  deliberately fails with `only OPAQUE materials are supported`.
- Remains unavailable as a runnable preset until the loader, material/pipeline
  selection, main pass and shadow pass implement these material semantics.
  Do not turn masks into opaque surfaces or silently drop their primitives.
- Lights seen in the upstream screenshot are not included in the model.
- Upstream assigns the model to Crytek under the **CRYENGINE Limited License
  Agreement**, not CC0 or CC-BY. Preserve [upstream license](Sponza/LICENSE.md),
  [credits/source notes](Sponza/UPSTREAM.md), and [metadata](Sponza/metadata.json).
  The unchanged [upstream license reference](LICENSES/LicenseRef-CRYENGINE-Agreement.txt)
  links to [Crytek's terms](https://www.cryengine.com/ce-terms); it is a reference,
  not the full agreement. Review the applicable asset terms before redistribution
  or commercial use. This import does not establish permission for either.

### MetalRoughSpheresNoTextures (downloaded compatibility reference)

- [Pinned upstream model directory](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/9429648735279342b4c32b8745f7904196607379/Models/MetalRoughSpheresNoTextures).
- [Exact glTF source](https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/9429648735279342b4c32b8745f7904196607379/Models/MetalRoughSpheresNoTextures/glTF/MetalRoughSpheresNoTextures.gltf).
- Downloaded while checking a texture-free alternative. Despite its name, all
  98 materials are double-sided, so the current importer correctly rejects it.
  It is not offered as a loadable scene and was not modified to bypass validation.
- Credit: Krill Gavrilov and Open CASCADE Technology / Draw Harness, 2019.
- Model license: CC0-1.0; metadata license: CC-BY-4.0. See
  [license](MetalRoughSpheresNoTextures/LICENSE.md) and
  [metadata](MetalRoughSpheresNoTextures/metadata.json).

### Re-download and integrity checks

[scene-assets.json](scene-assets.json) records the exact URL, byte length and
Git blob SHA-1 from the official GitHub tree for every imported file. It also
covers the unchanged existing DamagedHelmet GLB. Total: 91 files, 68,073,744 bytes;
new downloads excluding that existing GLB: 64,299,828 bytes (about 61.3 MiB).

From the repository root, using Python 3.9 or newer:

```bash
# Offline: check every file and every external glTF buffer/image dependency.
python3 scripts/fetch-scene-assets.py --verify-only

# Download only missing files from the pinned revision. Existing mismatched
# files cause an error instead of being overwritten.
python3 scripts/fetch-scene-assets.py

./build/linux-debug/gltf_inspect assets/models/gltf/MetalRoughSpheres/MetalRoughSpheres.gltf
./build/linux-debug/gltf_inspect assets/models/gltf/DamagedHelmet/DamagedHelmet.glb
```

Git LFS is used for the PNG/JPG files and the Sponza/MetalRoughSpheres binary
geometry. After cloning, run `git lfs pull` before verification if these files are
still LFS pointers. The script intentionally does not overwrite pointers or other
existing files, and does not stage, commit, or publish assets.

## Remaining accessor fixtures

Still required before completing the Stage 5 accessor matrix:

- unsigned byte indices
- unsigned int indices
- normalized integer `COLOR_0`
- normalized integer `TEXCOORD_0/1`
- sparse accessor without a base bufferView
