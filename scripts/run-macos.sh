#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_PRESET=${1:-macos-debug}
EXECUTABLE="$PROJECT_DIR/build/$BUILD_PRESET/vulkan"
HOMEBREW_PREFIX=${HOMEBREW_PREFIX:-$(brew --prefix)}

if [ ! -x "$EXECUTABLE" ]; then
    echo "Missing executable: $EXECUTABLE" >&2
    echo "Run: cmake --preset $BUILD_PRESET && cmake --build --preset $BUILD_PRESET" >&2
    exit 1
fi

export VK_DRIVER_FILES="${VK_DRIVER_FILES:-$HOMEBREW_PREFIX/opt/molten-vk/etc/vulkan/icd.d/MoltenVK_icd.json}"
export VK_LAYER_PATH="${VK_LAYER_PATH:-$HOMEBREW_PREFIX/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d}"
export DYLD_LIBRARY_PATH="$HOMEBREW_PREFIX/opt/vulkan-validationlayers/lib${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"

exec "$EXECUTABLE"
