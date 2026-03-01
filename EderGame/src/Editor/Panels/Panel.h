#pragma once

class Panel
{
public:
    virtual ~Panel() = default;
    virtual const char* Title() const = 0;
    virtual void        OnDraw()      = 0;

    bool open = true;
};
