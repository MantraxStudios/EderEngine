#pragma once 


class Renderer
{
public:
    virtual void Init()         = 0;
    virtual void BeginFrame()   = 0;
    virtual void EndFrame()     = 0;
    virtual void Shutdown()     = 0;
};