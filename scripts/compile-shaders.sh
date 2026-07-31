#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SHADER_DIR="$PROJECT_DIR/assets/shaders"

if ! command -v glslc >/dev/null 2>&1; then
    echo "glslc was not found. Install glslc (Linux) or shaderc (Homebrew), then add it to PATH." >&2
    exit 1
fi

compile() {
    glslc "$SHADER_DIR/$1" -o "$SHADER_DIR/$2"
}

compile shader.vert vert.spv
compile shader.frag frag.spv
compile skybox.vert skybox.vert.spv
compile skybox.frag skybox.frag.spv
compile irradiance.vert irradiance.vert.spv
compile irradiance.frag irradiance.frag.spv
compile prefilter.vert prefilter.vert.spv
compile prefilter.frag prefilter.frag.spv

echo "Shader compilation complete."
