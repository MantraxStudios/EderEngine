#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2DArray shadowTex;

layout(location = 0) out vec4 outColor;

void main()
{
    float depth = texture(shadowTex, vec3(fragUV, 0.0)).r;
    outColor = vec4(vec3(depth), 1.0);
}
