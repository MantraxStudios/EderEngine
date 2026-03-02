#version 450

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform PC {
    mat4 viewProj;
    vec4 color;
} pc;

void main()
{
    gl_Position = pc.viewProj * vec4(inPos, 1.0);
}
