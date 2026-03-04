# EderEngine — Lua Scripting API

Every entity with a `ScriptComponent` runs its script inside an isolated
`sol::environment`. The following global tables are available in every script.

---

## Script lifecycle

```lua
function OnStart()
    -- Called once the first frame the entity is active.
end

function OnUpdate(dt)
    -- Called every frame. dt = delta-time in seconds.
end

-- Collision callbacks (only if the entity has a Collider + Rigidbody)
function OnCollisionEnter(other, point, normal) end  -- other = entity id
function OnCollisionStay (other, point, normal) end
function OnCollisionExit (other, point, normal) end

function OnTriggerEnter(other) end   -- other = entity id
function OnTriggerExit (other) end
```

### Built-in globals

| Name | Description |
|------|-------------|
| `this_entity` | `int` — the entity id this script belongs to |
| `print(...)` | Prints to stderr tagged with the entity id |
| `log(msg)` | Alias for `print` |

---

## Transform

Local position / rotation / scale of an entity.
Rotation is in **degrees** (pitch, yaw, roll).

```lua
Transform.getPosition(e)            --> {x, y, z}
Transform.setPosition(e, x, y, z)

Transform.getRotation(e)            --> {x, y, z}  (degrees)
Transform.setRotation(e, x, y, z)

Transform.getScale(e)               --> {x, y, z}
Transform.setScale(e, x, y, z)

Transform.getWorldPosition(e)       --> {x, y, z}  (world-space)
```

---

## Tag

```lua
Tag.getName(e)           --> string
Tag.setName(e, name)
```

---

## Rigidbody

```lua
Rigidbody.hasRigidbody(e)           --> bool

Rigidbody.getMass(e)                --> float
Rigidbody.setMass(e, v)

Rigidbody.getVelocity(e)            --> {x, y, z}
Rigidbody.setVelocity(e, x, y, z)

Rigidbody.getAngularVelocity(e)     --> {x, y, z}

Rigidbody.isKinematic(e)            --> bool
Rigidbody.setKinematic(e, bool)

Rigidbody.useGravity(e)             --> bool
Rigidbody.setGravity(e, bool)

Rigidbody.getLinearDrag(e)          --> float
Rigidbody.setLinearDrag(e, v)

Rigidbody.getAngularDrag(e)         --> float
Rigidbody.setAngularDrag(e, v)
```

---

## Collider

Shape values: `"Box"` | `"Sphere"` | `"Capsule"`

```lua
Collider.hasCollider(e)             --> bool

Collider.isTrigger(e)               --> bool
Collider.setTrigger(e, bool)

Collider.getShape(e)                --> string
Collider.setShape(e, shape)

-- Box
Collider.getBoxHalfExtents(e)       --> {x, y, z}
Collider.setBoxHalfExtents(e, x, y, z)

-- Sphere / Capsule
Collider.getRadius(e)               --> float
Collider.setRadius(e, v)

-- Capsule
Collider.getCapsuleHalfHeight(e)    --> float
Collider.setCapsuleHalfHeight(e, v)

-- Offset
Collider.getCenter(e)               --> {x, y, z}
Collider.setCenter(e, x, y, z)

-- Physics material
Collider.getStaticFriction(e)       --> float
Collider.setStaticFriction(e, v)

Collider.getDynamicFriction(e)      --> float
Collider.setDynamicFriction(e, v)

Collider.getRestitution(e)          --> float
Collider.setRestitution(e, v)
```

---

## Hierarchy

```lua
Hierarchy.hasParent(e)              --> bool
Hierarchy.getParent(e)              --> entity id  (NULL_ENTITY = root)
Hierarchy.getChildCount(e)          --> int
Hierarchy.getChild(e, idx)          --> entity id  (0-based)
```

---

## MeshRenderer

```lua
MeshRenderer.hasMeshRenderer(e)     --> bool

MeshRenderer.isVisible(e)           --> bool
MeshRenderer.setVisible(e, bool)

MeshRenderer.castsShadow(e)         --> bool
MeshRenderer.setCastShadow(e, bool)

MeshRenderer.getMaterialName(e)     --> string
MeshRenderer.setMaterialName(e, name)
```

---

## Light

Type values: `"Directional"` | `"Point"` | `"Spot"`

```lua
Light.hasLight(e)                   --> bool

Light.getType(e)                    --> string
Light.setType(e, type)

Light.getColor(e)                   --> {r, g, b}
Light.setColor(e, r, g, b)

Light.getIntensity(e)               --> float
Light.setIntensity(e, v)

-- Point / Spot
Light.getRange(e)                   --> float
Light.setRange(e, v)

-- Spot only
Light.getInnerCone(e)               --> float  (degrees)
Light.setInnerCone(e, deg)

Light.getOuterCone(e)               --> float  (degrees)
Light.setOuterCone(e, deg)

Light.castsShadow(e)                --> bool
Light.setCastShadow(e, bool)

-- Volumetric (Directional only)
Light.isVolumetricEnabled(e)        --> bool
Light.setVolumetricEnabled(e, bool)
Light.getVolDensity(e)              --> float
Light.setVolDensity(e, v)
Light.getVolIntensity(e)            --> float
Light.setVolIntensity(e, v)
Light.setVolTint(e, r, g, b)

-- Sun Shafts (Directional only)
Light.isSunShaftsEnabled(e)         --> bool
Light.setSunShaftsEnabled(e, bool)
Light.getShaftsDensity(e)           --> float
Light.setShaftsDensity(e, v)
Light.getShaftsExposure(e)          --> float
Light.setShaftsExposure(e, v)
Light.setShaftsTint(e, r, g, b)
```

---

## Animation

Clip index is 0-based and maps to the clips embedded in the mesh asset.

```lua
Animation.hasAnimation(e)           --> bool

Animation.play(e)
Animation.stop(e)
Animation.isPlaying(e)              --> bool

Animation.getClip(e)                --> int
Animation.setClip(e, idx)

Animation.getSpeed(e)               --> float
Animation.setSpeed(e, v)

Animation.isLooping(e)              --> bool
Animation.setLoop(e, bool)

Animation.getTime(e)                --> float  (seconds into current clip)
Animation.setBlendDuration(e, sec)  -- crossfade time when changing clip
```

---

## VolumetricFog

```lua
VolumetricFog.hasFog(e)             --> bool

VolumetricFog.isEnabled(e)          --> bool
VolumetricFog.setEnabled(e, bool)

VolumetricFog.getDensity(e)         --> float
VolumetricFog.setDensity(e, v)

VolumetricFog.getFogStart(e)        --> float  (metres)
VolumetricFog.setFogStart(e, v)

VolumetricFog.getFogEnd(e)          --> float  (metres)
VolumetricFog.setFogEnd(e, v)

VolumetricFog.setColor(e, r, g, b)
VolumetricFog.setScatterStrength(e, v)
```

---

## Entity

```lua
Entity.create()                     --> entity id
Entity.destroy(e)
Entity.isValid(e)                   --> bool
```

---

## Script — comunicación entre scripts

Permite llamar funciones y leer/escribir variables del entorno Lua de **otro** entity.

```lua
-- Llama una función en el script del entity 'other' y recibe su retorno
local result = Script.call(other, "GetHealth")

-- Llamada con argumentos
Script.call(other, "TakeDamage", 25)

-- Lee una variable del entorno del otro script
local hp = Script.get(other, "health")

-- Escribe un valor en el entorno del otro script
Script.set(other, "health", 100)

-- Comprueba si el entity tiene un script cargado
if Script.has(other) then
    Script.call(other, "Alert")
end
```

> **Nota**: `Script.call` devuelve solo el **primer** valor de retorno de la función.
> Si la función no existe o el entity no tiene script, devuelve `nil` y escribe un mensaje en stderr.

---

## Vec3

Stateless math helpers that operate on plain `{x, y, z}` tables.

```lua
Vec3.new(x, y, z)                   --> {x, y, z}
Vec3.length(v)                      --> float
Vec3.dot(a, b)                      --> float
Vec3.normalize(v)                   --> {x, y, z}
```

---

## Examples

### Move an entity forward every frame
```lua
function OnUpdate(dt)
    local pos = Transform.getPosition(this_entity)
    Transform.setPosition(this_entity, pos.x, pos.y, pos.z + 5 * dt)
end
```

### Pulse a point light
```lua
local t = 0
function OnUpdate(dt)
    t = t + dt
    Light.setIntensity(this_entity, 1.0 + math.sin(t * 3) * 0.5)
end
```

### Play animation on collision
```lua
function OnCollisionEnter(other, point, normal)
    Animation.setClip(this_entity, 1)
    Animation.play(this_entity)
end
```

### Hide mesh after 3 seconds
```lua
local timer = 0
function OnUpdate(dt)
    timer = timer + dt
    if timer >= 3.0 then
        MeshRenderer.setVisible(this_entity, false)
    end
end
```

### Walk child hierarchy
```lua
function OnStart()
    local count = Hierarchy.getChildCount(this_entity)
    for i = 0, count - 1 do
        local child = Hierarchy.getChild(this_entity, i)
        print("child: " .. child)
    end
end
```

### Llamar una función en otro entity
```lua
-- entity_enemy.lua
health = 100

function TakeDamage(amount)
    health = health - amount
    print("Health: " .. health)
    return health
end

-- entity_player.lua
function OnCollisionEnter(other, point, normal)
    if Script.has(other) then
        local remaining = Script.call(other, "TakeDamage", 25)
        if remaining and remaining <= 0 then
            Entity.destroy(other)
        end
    end
end
```
