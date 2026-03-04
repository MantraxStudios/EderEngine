#version 450

layout(location = 0) in vec3 fragWorldPos;

layout(push_constant) uniform Push {
    mat4 lightViewProj;
    vec4 lightPosAndFar;  // xyz = light position, w = far plane
};

void main()
{
    float dist   = length(fragWorldPos - lightPosAndFar.xyz);
    gl_FragDepth = dist / lightPosAndFar.w;  // linear depth [0, 1]
}
