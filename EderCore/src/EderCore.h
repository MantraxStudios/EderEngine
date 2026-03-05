#pragma once

// ── ECS core ─────────────────────────────────────────────────────────────────
#include "ECS/Entity.h"
#include "ECS/Registry.h"

// ── Components ───────────────────────────────────────────────────────────────
#include "ECS/Components/TagComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "ECS/Components/HierarchyComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/LightComponent.h"
#include "ECS/Components/AnimationComponent.h"
#include "ECS/Components/CollisionCallbackComponent.h"
#include "ECS/Components/ScriptComponent.h"

// ── Systems ──────────────────────────────────────────────────────────────────
#include "ECS/Systems/System.h"
#include "ECS/Systems/TransformSystem.h"

// ── Events ───────────────────────────────────────────────────────────────────
#include "Events/EventBus.h"
#include "Events/Events.h"

// ── Scene ─────────────────────────────────────────────────────────────────────
#include "Scene/Scene.h"
#include "Scene/SceneManager.h"

// ── IO / Asset system ────────────────────────────────────────────────────────
#include "IO/KRCommon.h"
#include "IO/KRCompiler.h"
#include "IO/PakFile.h"
#include "IO/AssetManager.h"
#include "IO/SceneSerializer.h"

// ── Extra components ─────────────────────────────────────────────────────
#include "ECS/Components/VolumetricFogComponent.h"