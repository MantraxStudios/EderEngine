-- TriggerZone
-- Detecta qué entity entró o salió del trigger.
-- Requiere: ColliderComponent con isTrigger = true en esta entidad.

function OnStart()
    print("[TriggerZone] listo — entity: " .. tostring(this_entity))
end

function OnTriggerEnter(other)
    local name = Tag.getName(other)
    if name and name ~= "" then
        print("[TriggerZone] ENTER — entity: " .. tostring(other) .. "  nombre: " .. name)
    else
        print("[TriggerZone] ENTER — entity: " .. tostring(other))
    end
end

function OnTriggerExit(other)
    local name = Tag.getName(other)
    if name and name ~= "" then
        print("[TriggerZone] EXIT  — entity: " .. tostring(other) .. "  nombre: " .. name)
    else
        print("[TriggerZone] EXIT  — entity: " .. tostring(other))
    end
end
