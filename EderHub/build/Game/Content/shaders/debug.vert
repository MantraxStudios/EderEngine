#version 450

layout(location = 0) out vec2 fragUV;

void main()
{
    vec2 pos[6] = vec2[6](
        vec2(0.52, 0.52), vec2(0.98, 0.52), vec2(0.52, 0.98),
        vec2(0.98, 0.52), vec2(0.98, 0.98), vec2(0.52, 0.98)
    );
    vec2 uv[6] = vec2[6](
        vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
        vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 1.0)
    );
    gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
    fragUV      = uv[gl_VertexIndex];
}
