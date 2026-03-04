#pragma once
#include <string>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────────
//  ScriptComponent
//  Attach a Lua script asset to an entity.
//  The LuaScriptSystem reads scriptGuid via AssetManager::GetBytesByGuid,
//  compiles and executes the chunk in its own sol::environment, then calls
//  OnStart()  once and  OnUpdate(dt) every frame.
//
//  Supported lifecycle callbacks (define any subset in the .lua file):
//    function OnStart()                        end
//    function OnUpdate(dt)                     end
//    function OnCollisionEnter(other,pt,n)     end
//    function OnCollisionStay (other,pt,n)     end
//    function OnCollisionExit (other,pt,n)     end
//    function OnTriggerEnter  (other)          end
//    function OnTriggerExit   (other)          end
// ─────────────────────────────────────────────────────────────────────────────
struct ScriptComponent
{
    uint64_t    scriptGuid = 0;        // AssetManager GUID of the .lua file
    std::string scriptPath;            // relative path (kept for display / reload)
    bool        started    = false;    // true once OnStart has been called
};
