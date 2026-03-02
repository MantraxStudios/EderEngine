#pragma once
#include "ECS/Registry.h"

class System
{
public:
    virtual ~System() = default;

    virtual void OnStart (Registry& registry)              {}
    virtual void OnUpdate(Registry& registry, float dt)    {}
    virtual void OnStop  (Registry& registry)              {}
};
