#ifndef PROJECT_TEMPORAL_FRAME_GLSL
#define PROJECT_TEMPORAL_FRAME_GLSL

// 与 TemporalFrameUniforms 的 std140 布局一致，总大小 384 字节。
layout(std140, set = 2, binding = 0) uniform TemporalFrame
{
    mat4 currentView;
    mat4 currentViewProjectionUnjittered;
    mat4 previousViewProjectionUnjittered;
    mat4 previousView;
    mat4 inverseCurrentViewProjectionJittered;

    vec4 cameraPosition;
    vec4 jitterUv;
    vec4 resolveParams;
    uvec4 frameInfo;
} temporalFrame;

#endif