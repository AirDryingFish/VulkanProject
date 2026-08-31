# Stage 5 glTF Test Assets

## Reproducibility baseline

- Upstream: https://github.com/KhronosGroup/glTF-Sample-Assets
- Upstream revision: `9429648735279342b4c32b8745f7904196607379`
- vcpkg builtin baseline: `8e8dfb4ba483886936ded5ca201b500b8d8b0096`
- fastgltf port version: `0.9.0`
- Asset storage: small `.gltf`, `.glb`, and `.bin` fixtures are stored in ordinary Git, not Git LFS.

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

## Remaining accessor fixtures

Still required before completing the Stage 5 accessor matrix:

- unsigned byte indices
- unsigned int indices
- normalized integer `COLOR_0`
- normalized integer `TEXCOORD_0/1`
- sparse accessor without a base bufferView