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

-- Base (principal) material
MeshRenderer.getMaterialName(e)     --> string
MeshRenderer.setMaterialName(e, name)

-- Material by index: idx=0 → base material; idx=1,2,… → sub-mesh overrides
MeshRenderer.getSubMeshCount(e)             --> int
MeshRenderer.getMaterialByIndex(e, idx)     --> string
MeshRenderer.setMaterialByIndex(e, idx, name)
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
Light.getVolTint(e)                 --> {r, g, b}
Light.setVolTint(e, r, g, b)
Light.getVolNumSteps(e)             --> int    (ray-march quality, default 64)
Light.setVolNumSteps(e, n)
Light.getVolAbsorption(e)           --> float
Light.setVolAbsorption(e, v)
Light.getVolG(e)                    --> float  (Henyey-Greenstein anisotropy -1..1)
Light.setVolG(e, v)
Light.getVolMaxDistance(e)          --> float  (world units)
Light.setVolMaxDistance(e, v)
Light.getVolJitter(e)               --> float  (banding reduction, 0=off)
Light.setVolJitter(e, v)

-- Sun Shafts (Directional only)
Light.isSunShaftsEnabled(e)         --> bool
Light.setSunShaftsEnabled(e, bool)
Light.getShaftsDensity(e)           --> float
Light.setShaftsDensity(e, v)
Light.getShaftsExposure(e)          --> float
Light.setShaftsExposure(e, v)
Light.getShaftsTint(e)              --> {r, g, b}
Light.setShaftsTint(e, r, g, b)
Light.getShaftsBloomScale(e)        --> float
Light.setShaftsBloomScale(e, v)
Light.getShaftsDecay(e)             --> float  (per-step falloff, 0.9–0.99)
Light.setShaftsDecay(e, v)
Light.getShaftsWeight(e)            --> float
Light.setShaftsWeight(e, v)
Light.getShaftsSunRadius(e)         --> float  (angular radius of sun disk)
Light.setShaftsSunRadius(e, v)
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

VolumetricFog.getColor(e)           --> {r, g, b}
VolumetricFog.setColor(e, r, g, b)

VolumetricFog.setHorizonColor(e, r, g, b)    -- warm tint near horizon
VolumetricFog.setSunScatterColor(e, r, g, b) -- forward-scatter glow toward sun

VolumetricFog.getHeightFalloff(e)   --> float  (fog band thinning with altitude)
VolumetricFog.setHeightFalloff(e, v)

VolumetricFog.getHeightOffset(e)    --> float  (world-Y of full-density base)
VolumetricFog.setHeightOffset(e, v)

VolumetricFog.getScatterStrength(e) --> float  (mie forward-scatter 0–1)
VolumetricFog.setScatterStrength(e, v)

VolumetricFog.getMaxFogAmount(e)    --> float  (opacity ceiling, prevents whiteout)
VolumetricFog.setMaxFogAmount(e, v)
```

---

## Entity

```lua
Entity.create()                     --> entity id
Entity.destroy(e)
Entity.isValid(e)                   --> bool
```

---

## System — utilidades del sistema operativo

### Tiempo

```lua
System.getTime()            --> float  segundos desde epoch (unix timestamp)
System.getDate()            --> string "2026-03-04 14:30:00"
System.getDate("%d/%m/%Y")  --> string con formato strftime personalizado
System.getClock()           --> float  segundos de CPU desde que inició el programa
```

Códigos de formato comunes para `getDate`:

| Código | Resultado |
|--------|-----------|
| `%Y` | Año 4 dígitos |
| `%m` | Mes 01-12 |
| `%d` | Día 01-31 |
| `%H` | Hora 00-23 |
| `%M` | Minuto 00-59 |
| `%S` | Segundo 00-59 |

### Máquina / entorno

```lua
System.getComputerName()    --> string  nombre del PC
System.getUserName()        --> string  usuario de sesión
System.getEnv("USERPROFILE")-> string  variable de entorno (vacío si no existe)
System.getCwd()             --> string  directorio de trabajo actual
```

### Archivos

```lua
-- Leer un archivo completo como string (nil si no existe)
local texto = System.readFile("saves/save1.json")

-- Escribir (sobreescribe el archivo)
System.writeFile("logs/log.txt", "hola mundo\n")   --> bool

-- Añadir al final sin borrar el contenido anterior
System.appendFile("logs/log.txt", "otra linea\n")  --> bool

-- Borrar un archivo
System.deleteFile("tmp/temp.dat")                  --> bool

-- Tamaño en bytes (-1 si error)
System.fileSize("saves/save1.json")                --> int
```

### Directorios y consultas

```lua
-- Comprobar existencia
System.exists("saves/save1.json")   --> bool
System.isFile("saves/save1.json")   --> bool
System.isDir("saves/")              --> bool

-- Crear directorio (crea padres si es necesario)
System.createDir("saves/slot1")     --> bool

-- Listar archivos en un directorio (solo nombres, no rutas completas)
local files = System.listFiles("saves/")
for i = 1, #files do print(files[i]) end

-- Listar recursivamente
local all = System.listFiles("Content/", true)

-- Listar subdirectorios
local dirs = System.listDirs("Content/")
```

---



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

### Sistema: guardar un archivo de log
```lua
function OnStart()
    local pc   = System.getComputerName()
    local user = System.getUserName()
    local date = System.getDate()
    System.createDir("logs")
    System.appendFile("logs/session.log",
        date .. " | " .. user .. "@" .. pc .. " | session started\n")
end
```

### Sistema: cargar config JSON sencillo
```lua
function OnStart()
    local raw = System.readFile("saves/config.json")
    if raw then
        -- parsear con un mínimo splitter artesanal o una librería JSON Lua
        print("Config loaded: " .. #raw .. " bytes")
    else
        -- primera vez — crear config por defecto
        System.createDir("saves")
        System.writeFile("saves/config.json", '{"volume":1.0,"fullscreen":false}')
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