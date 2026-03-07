#version 450

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

layout(location = 0) out vec2  fragUV;
layout(location = 1) out vec4  fragColor;
layout(location = 2) out float fragMode;

void main()
{
    int vi = gl_VertexIndex;
    float cx = (vi == 1 || vi == 3 || vi == 4) ? 1.0 : 0.0;
    float cy = (vi == 2 || vi == 4 || vi == 5) ? 1.0 : 0.0;

    vec2 virtPos = vec2(pc.rectX + cx * pc.rectW, pc.rectY + cy * pc.rectH);
    vec2 screenPos = virtPos * pc.scale + vec2(pc.offsetX, pc.offsetY);
    vec2 ndc = screenPos / vec2(pc.screenW, pc.screenH) * 2.0 - 1.0;

    gl_Position = vec4(ndc, 0.0, 1.0);

    fragUV    = vec2(mix(pc.uvX0, pc.uvX1, cx), mix(pc.uvY0, pc.uvY1, cy));
    fragColor = vec4(pc.colorR, pc.colorG, pc.colorB, pc.colorA);
    fragMode  = pc.mode;
}
