#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in vec3 inTangent;
layout(location = 4) in vec3 inBitangent;
layout(location = 5) in vec4 inColor;

layout(location = 6) in vec4 instanceModelCol0;
layout(location = 7) in vec4 instanceModelCol1;
layout(location = 8) in vec4 instanceModelCol2;
layout(location = 9) in vec4 instanceModelCol3;

layout(push_constant) uniform PushConstants {
    mat4 lightViewProj;
} push;

void main()
{
    mat4 model  = mat4(instanceModelCol0, instanceModelCol1, instanceModelCol2, instanceModelCol3);
    gl_Position = push.lightViewProj * model * vec4(inPosition, 1.0);
}
