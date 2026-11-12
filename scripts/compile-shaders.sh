#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SHADER_DIR="$PROJECT_DIR/assets/shaders"

if ! command -v glslc >/dev/null 2>&1; then
    echo "glslc was not found. Install glslc (Linux) or shaderc (Homebrew), then add it to PATH." >&2
    exit 1
fi

# 输出始终与源文件相邻；共享 include 从 shader 根目录解析。
find "$SHADER_DIR" -type f \
    \( -name '*.vert' -o -name '*.frag' -o -name '*.comp' \) \
    -exec sh -eu -c '
        shader_dir=$1
        shift
        for shader in "$@"; do
            glslc --target-env=vulkan1.0 -I "$shader_dir" "$shader" -o "$shader.spv"
        done
    ' sh "$SHADER_DIR" {} +

echo "Shader compilation complete."
