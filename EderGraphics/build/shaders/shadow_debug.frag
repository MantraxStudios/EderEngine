#version 450

layout(location = 0) in vec2 fragUV;

layout(set = 0, binding = 0) uniform sampler2D shadowTex;

layout(location = 0) out vec4 outColor;

void main()
{
    float depth = texture(shadowTex, fragUV).r;
    outColor = vec4(vec3(depth), 1.0);
}
