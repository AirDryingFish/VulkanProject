#ifndef PROJECT_TILE_LAYOUT_GLSL
#define PROJECT_TILE_LAYOUT_GLSL

const uint TILE_SIZE = 16u;
// 每 tile 最多包含多少 light
const uint TILE_LIGHT_CAPACITY = 128u;

layout(std140, set = 1, binding = 5) uniform DeferredFrameUniforms
{
    mat4 inverseViewProjection;
    mat4 inverseProjection;

    // width, height, tileCountX, tileCountY
    uvec4 extentAndTiles;

    // tileSize, maxLightPerTile, cullingMode, debugMode
    uvec4 tileParams;
} deferredFrame;


// 如果 compute shader 这样 include 文件：
// #define TILE_LIST_COMPUTE_WRITE
// #include "tile_layout.glsl
// 那么它会变成可写
#ifdef TILE_LIST_COMPUTE_WRITE
#define TILE_LIST_ACCESS writeonly

#else

#define TILE_LIST_ACCESS readonly

#endif

layout(std430, set = 1, binding = 6) TILE_LIST_ACCESS buffer TileHeaderBuffer
{
    // x: storedCount
    // y: overflow
    // z/w: reserved
    uvec4 headers[];
} tileHeaders;

layout(std430, set = 1, binding = 7) TILE_LIST_ACCESS buffer TileIndexBuffer
{
    uint indices[];
} tileIndices;

#undef TILE_LIST_ACCESS

#endif