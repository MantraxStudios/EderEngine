#version 450

layout(location = 0) out vec2 fragNDC;

void main()
{
    // Fullscreen triangle that covers NDC [-1,1]x[-1,1]
    const vec2 pos[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 p  = pos[gl_VertexIndex];
    fragNDC = p;

    // z = w = 1.0 so depth after perspective divide = 1.0 (far plane)
    // Passes eLessOrEqual only where the scene left depth untouched
    gl_Position = vec4(p, 1.0, 1.0);
}
