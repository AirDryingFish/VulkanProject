#version 450

layout(location = 0) out vec2 fragUv;

// 覆盖 viewport 的大三角形
void main()
{
    // NDC / clip space 附近的坐标
    const vec2 positions[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2(3.0, -1.0),
        vec2(-1.0, 3.0)
    );
    // gl_VertexIndex 是 vulkan/glsl 内建变量
    // vkCmdDraw(cmd, 3, 1, 0, 0); 会执行 3 次: gl_VertexIndex = 0 1 2
    vec2 position = positions[gl_VertexIndex];

    // 把 ndc [-1, 1] 映射到 uv [0, 1]
    fragUv = position * 0.5 + 0.5;
    gl_Position = vec4(position, 0.0, 1.0);
}