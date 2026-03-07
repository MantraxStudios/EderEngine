#version 450

layout(location = 0) out vec2 inUVs;

void main()
{
    
    const vec2 pos[3] = vec2[](
        vec2(-1.0, -1.0),
        vec2( 3.0, -1.0),
        vec2(-1.0,  3.0)
    );
    vec2 p = pos[gl_VertexIndex];
    inUVs  = p * 0.5 + 0.5;   

    
    
    gl_Position = vec4(p, 1.0, 1.0);
}
