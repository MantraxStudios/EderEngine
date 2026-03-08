#version 450

layout(set = 0, binding = 0) uniform sampler2D uTexture;

layout(push_constant) uniform PC {
    float rectX, rectY, rectW, rectH;
    float uvX0,  uvY0,  uvX1,  uvY1;
    float colorR, colorG, colorB, colorA;
    float scale;
    float offsetX;
    float offsetY;
    float screenW;
    float screenH;
    float mode;
    float pad0;
    float pad1;
} pc;

layout(location = 0) in vec2  fragUV;
layout(location = 1) in vec4  fragColor;
layout(location = 2) in float fragMode;

layout(location = 0) out vec4 outColor;

void main()
{
    int mode = int(fragMode + 0.5);

    if (mode == 1) {
        vec4 tex = texture(uTexture, fragUV);
        outColor = fragColor * tex;
    } else if (mode == 2) {
        float alpha = texture(uTexture, fragUV).a;
        outColor = vec4(fragColor.rgb, fragColor.a * alpha);
    } else {
        outColor = fragColor;
    }

    if (outColor.a < 0.004) discard;
}
