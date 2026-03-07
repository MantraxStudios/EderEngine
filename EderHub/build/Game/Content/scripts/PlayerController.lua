-- PlayerController
-- Mueve el CharacterController en la direccion de la camara activa.
-- WASD relativo al yaw de la camara, Space para saltar.
-- Raycast de debug hacia adelante cada frame.

local speed      = 5.0    -- m/s horizontal
local jumpVel    = 7.0    -- m/s al saltar
local gravity    = -20.0  -- m/s^2
local rayDist    = 5.0    -- distancia maxima del raycast

local vy = 0.0            -- velocidad vertical acumulada
local lastHitEntity = -1  -- ultimo entity detectado

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

    -- ── Direccion de la camara (solo yaw, ignoramos pitch para no volar) ──
    local camEntity = Camera.getActive()
    local yaw = 0.0
    if camEntity and camEntity ~= 0 then
        local camRot = Transform.getRotation(camEntity)
        yaw = math.rad(camRot.y)
    end

    -- Vectores horizontales derivados del yaw de la camara
    local fwdX  =  math.sin(yaw)   -- forward  (W/S)
    local fwdZ  =  math.cos(yaw)
    local rightX =  math.cos(yaw)  -- derecha  (A/D)
    local rightZ = -math.sin(yaw)

    -- ── Movimiento horizontal relativo a la camara ────────────────────────
    local h = Input.getAxis("Horizontal")   -- A/D  → -1..1
    local v = Input.getAxis("Vertical")     -- W/S  → -1..1

    local moveX = (v * fwdX + h * rightX) * speed * dt
    local moveZ = (v * fwdZ + h * rightZ) * speed * dt

    CharacterController.move(this_entity, moveX, vy * dt, moveZ)

    -- ── Raycast hacia adelante (forward de la camara) ─────────────────────
    local pos = Transform.getWorldPosition(this_entity)
    local ox = pos.x
    local oy = pos.y + 1.0   -- centro del torso
    local oz = pos.z

    local hit = Physics.raycast(ox, oy, oz, fwdX, 0, fwdZ, rayDist)

    if hit.hit then
        -- Ray rojo hasta el punto de impacto
        Debug.drawRay(ox, oy, oz, fwdX, 0, fwdZ, 1, 0, 0, 1, 0)
        -- Cruz amarilla en el punto de impacto
        Debug.drawLine(hit.x - 0.1, hit.y, hit.z,
                       hit.x + 0.1, hit.y, hit.z,
                       1, 1, 0, 1, 0)
        Debug.drawLine(hit.x, hit.y - 0.1, hit.z,
                       hit.x, hit.y + 0.1, hit.z,
                       1, 1, 0, 1, 0)

        if hit.entity ~= lastHitEntity then
            lastHitEntity = hit.entity
            print("Raycast -> entity: " .. tostring(hit.entity)
                  .. "  dist: " .. string.format("%.2f", hit.distance))
        end
    else
        -- Ray verde cuando no hay impacto
        Debug.drawRay(ox, oy, oz, fwdX, 0, fwdZ, 0, 1, 0, 1, 0)

        if lastHitEntity ~= -1 then
            lastHitEntity = -1
            print("Raycast -> sin impacto")
        end
    end
end
