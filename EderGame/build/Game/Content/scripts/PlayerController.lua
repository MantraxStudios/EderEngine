-- PlayerController
-- Mueve el CharacterController con WASD y salta con Space.

local speed   = 5.0    -- m/s horizontal
local jumpVel = 7.0    -- m/s al saltar
local gravity = -20.0  -- m/s^2

local vy = 0.0         -- velocidad vertical acumulada

function OnStart()
    print("PlayerController listo")
end

function OnUpdate(dt)
    -- ── Gravedad ─────────────────────────────────────────────────────────
    if CharacterController.isGrounded(this_entity) then
        vy = 0.0
        if Input.getKeyDown(Input.KEY_SPACE) then
            vy = jumpVel
        end
    else
        vy = vy + gravity * dt
    end

    -- ── Movimiento horizontal ─────────────────────────────────────────────
    local h = Input.getAxis("Horizontal")   -- A/D  → -1..1
    local v = Input.getAxis("Vertical")     -- S/W  → -1..1

    -- ── Aplicar movimiento ────────────────────────────────────────────────
    CharacterController.move(this_entity,
        h * speed * dt,
        vy * dt,
        v * speed * dt)
end
