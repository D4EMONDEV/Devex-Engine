#include "ManagedGame.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/SharedLibrary.hpp>
#include <devex/runtime/ComponentViews.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/DynamicComponent.hpp>
#include <devex/scene/EntityRef.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <source_location>
#include <string_view>
#include <system_error>
#include <utility>

namespace devex::runtime::detail {
namespace {

using scene::Entity;

// A UUID as C# passes it: its 16 bytes.
struct UuidBytes
{
    std::uint8_t bytes[16];
};

// The functions the C# runtime calls, in the order of Devex.Managed's NativeApi.
struct NativeApi
{
    void (*log)(int level, const char* message);

    int (*componentEntities)(void* scene, std::size_t typeIndex, const Entity** entities);
    void* (*findComponent)(void* scene, std::size_t typeIndex, Entity entity);
    void* (*addComponent)(void* scene, std::size_t typeIndex, Entity entity);
    void (*removeComponent)(void* scene, std::size_t typeIndex, Entity entity);
    std::int64_t (*componentIndex)(const char* name);
    std::uint64_t (*registryGeneration)();
    std::uint64_t (*componentLayoutHash)(std::size_t typeIndex);
    const char* (*readString)(void* address);
    void (*writeString)(void* address, const char* value);
    std::size_t (*listSize)(std::size_t typeIndex, int field, void* component);
    void* (*listElement)(std::size_t typeIndex, int field, void* component, std::size_t index);
    void (*listResize)(std::size_t typeIndex, int field, void* component, std::size_t size);
    void (*listInsert)(std::size_t typeIndex, int field, void* component, std::size_t index);
    void (*listErase)(std::size_t typeIndex, int field, void* component, std::size_t index);
    Entity (*resolveEntity)(void* scene, const UuidBytes* uuid);
    void (*entityUuid)(void* scene, Entity entity, UuidBytes* uuid);

    Entity (*createEntity)(void* scene, const char* name);
    void (*destroyEntity)(void* scene, Entity entity);
    int (*isAlive)(void* scene, Entity entity);
    const char* (*entityName)(void* scene, Entity entity);
    void (*setEntityName)(void* scene, Entity entity, const char* name);
    Entity (*findEntity)(void* scene, const char* name);
    Entity (*parent)(void* scene, Entity entity);
    Entity (*firstChild)(void* scene, Entity entity);
    Entity (*nextSibling)(void* scene, Entity entity);
    void (*setParent)(void* scene, Entity child, Entity parent);
    void* (*transformOf)(void* scene, Entity entity);
    const float* (*worldPositionOf)(void* scene, Entity entity);

    int (*isKeyDown)(int key);
    int (*wasKeyPressed)(int key);
    int (*isMouseButtonDown)(int button);
    int (*wasMouseButtonPressed)(int button);
    void (*mouseDelta)(float* values);
    void (*mousePosition)(float* values);
    int (*isMouseCaptured)();
    void (*setMouseCaptured)(int captured);

    void (*requestQuit)();
    void (*loadScene)(const UuidBytes* scene);
    Entity (*instantiatePrefab)(void* scene, const UuidBytes* prefab, Entity parent);
    int (*findAsset)(const char* path, UuidBytes* asset);
    void (*windowSize)(float* values);

    int (*raycast)(const math::Vec3* origin, const math::Vec3* direction, float maxDistance, std::uint32_t layers,
                   Entity ignore, physics::RayHit* hit);
    int (*sphereCast)(const math::Vec3* origin, float radius, const math::Vec3* direction, float maxDistance,
                      std::uint32_t layers, Entity ignore, physics::RayHit* hit);
    int (*overlapSphere)(const math::Vec3* center, float radius, std::uint32_t layers, Entity ignore,
                         const Entity** entities);
    void (*addForce)(Entity entity, const math::Vec3* force);
    void (*addTorque)(Entity entity, const math::Vec3* torque);
    void (*addImpulse)(Entity entity, const math::Vec3* impulse);
    void (*addImpulseAt)(Entity entity, const math::Vec3* impulse, const math::Vec3* point);
    int (*contacts)(const physics::Contact** contacts);

    void (*playSound)(void* scene, Entity entity);
    void (*stopSound)(Entity entity);
    void (*pauseSound)(Entity entity);
    void (*resumeSound)(Entity entity);
    int (*isSoundPlaying)(Entity entity);
    void (*playOneShot)(const UuidBytes* clip, const math::Vec3* position, float volume, std::uint32_t group,
                        int spatial);
    float (*groupVolume)(const char* group);
    int (*setGroupVolume)(const char* group, float volume);

    void (*playAnimation)(void* scene, Entity entity, const UuidBytes* clip, float fade);
    void (*stopAnimation)(Entity entity);
    void (*pauseAnimation)(Entity entity);
    void (*resumeAnimation)(Entity entity);
    int (*isAnimationPlaying)(Entity entity);
    float (*animationTime)(Entity entity);
    void (*setAnimationTime)(void* scene, Entity entity, float seconds);

    int (*uiClickedAction)(const char* action);
    int (*uiClickedEntity)(Entity entity);
    int (*uiCancelled)();
    Entity (*uiHovered)();
    Entity (*uiFocused)();
    void (*uiSetFocus)(void* scene, Entity entity);
    int (*uiPointerOverInterface)();
    int (*uiChangedAction)(const char* action);
    int (*uiSubmittedAction)(const char* action);
    Entity (*uiEditedField)();
    int (*profileEnabled)();
    void (*profileBegin)(const char* name);
    void (*profileEnd)();
};

// The functions the engine calls, in the order of Devex.Managed's ManagedApi.
struct ManagedApi
{
    int (*loadGame)(const char* assemblyPath, const char** description);
    void (*unloadGame)();
    void (*setTypeLayout)(const char* typeName, std::size_t typeIndex, const std::size_t* offsets, int count);
    void (*runPhase)(void* scene, int phase, float delta);
    void (*applyDefaults)(const char* typeName, void* component);
    int (*isDebuggerAttached)();
};

// Devex.Managed's Bootstrap.Version: both sides change it with the function tables.
constexpr int bootstrapVersion = 6;

struct BootstrapArguments
{
    int version;
    const NativeApi* native;
    ManagedApi* managed;
};

// The phase being run, which the C# API works on; null outside the phases.
[[nodiscard]] ManagedGame::Frame*& currentFrame() noexcept
{
    static ManagedGame::Frame* frame = nullptr;
    return frame;
}

[[nodiscard]] scene::Scene* toScene(void* scene) noexcept
{
    return static_cast<scene::Scene*>(scene);
}

// C#'s Entity.None is all zeros, which C++ reads as a stale handle rather than as no entity.
[[nodiscard]] Entity optionalEntity(void* scene, Entity entity) noexcept
{
    return scene != nullptr && static_cast<scene::Scene*>(scene)->isAlive(entity) ? entity : Entity{};
}

[[nodiscard]] Entity optionalEntity(Entity entity) noexcept
{
    return entity.generation != 0 ? entity : Entity{};
}

[[nodiscard]] core::Uuid toUuid(const UuidBytes* bytes) noexcept
{
    core::Uuid uuid;
    if (bytes != nullptr)
    {
        std::memcpy(&uuid, bytes, sizeof(uuid));
    }
    return uuid;
}

void writeUuid(core::Uuid uuid, UuidBytes* bytes) noexcept
{
    if (bytes != nullptr)
    {
        std::memcpy(bytes, &uuid, sizeof(uuid));
    }
}

// Layouts shared with the C# structures.
static_assert(sizeof(scene::Transform) == 40);
static_assert(offsetof(scene::Transform, position) == 0);
static_assert(offsetof(scene::Transform, rotation) == 12);
static_assert(offsetof(scene::Transform, scale) == 28);
static_assert(sizeof(Entity) == 8);
static_assert(sizeof(math::Quat) == 16);
static_assert(sizeof(core::Uuid) == 16 && std::is_trivially_copyable_v<core::Uuid>);
static_assert(sizeof(scene::EntityRef) == 16);
static_assert(sizeof(asset::AssetId) == 16);
static_assert(sizeof(physics::RayHit) == 36);
static_assert(offsetof(physics::RayHit, point) == 8 && offsetof(physics::RayHit, normal) == 20 &&
              offsetof(physics::RayHit, distance) == 32);
static_assert(sizeof(physics::Contact) == 24);
static_assert(offsetof(physics::Contact, first) == 4 && offsetof(physics::Contact, second) == 12 &&
              offsetof(physics::Contact, trigger) == 20);

void apiLog(int level, const char* message)
{
    const auto logLevel = static_cast<core::LogLevel>(std::clamp(level, 0, 5));
    // The C# side names its own place, in the stack of its exceptions.
    core::logMessage(logLevel, message != nullptr ? message : "", std::source_location{});
}

int apiComponentEntities(void* scene, std::size_t typeIndex, const Entity** entities)
{
    const scene::ComponentPoolBase* const pool = scene != nullptr ? toScene(scene)->componentPool(typeIndex) : nullptr;
    if (pool == nullptr || pool->entities().empty())
    {
        *entities = nullptr;
        return 0;
    }
    *entities = pool->entities().data();
    return static_cast<int>(pool->entities().size());
}

void* apiFindComponent(void* scene, std::size_t typeIndex, Entity entity)
{
    if (scene == nullptr || !toScene(scene)->isAlive(entity))
    {
        return nullptr;
    }
    // The pools of C# components first, which are reached most often.
    if (scene::DynamicComponentPool* const pool = toScene(scene)->dynamicPool(typeIndex))
    {
        return pool->find(entity);
    }
    const scene::ComponentType* const type = scene::componentRegistry().findByIndex(typeIndex);
    return type != nullptr ? const_cast<void*>(type->find(*toScene(scene), entity)) : nullptr;
}

void* apiAddComponent(void* scene, std::size_t typeIndex, Entity entity)
{
    const scene::ComponentType* const type = scene::componentRegistry().findByIndex(typeIndex);
    return type != nullptr && scene != nullptr && toScene(scene)->isAlive(entity) ? type->emplace(*toScene(scene), entity)
                                                                                    : nullptr;
}

void apiRemoveComponent(void* scene, std::size_t typeIndex, Entity entity)
{
    const scene::ComponentType* const type = scene::componentRegistry().findByIndex(typeIndex);
    if (type != nullptr && scene != nullptr && toScene(scene)->isAlive(entity))
    {
        type->remove(*toScene(scene), entity);
    }
}

std::int64_t apiComponentIndex(const char* name)
{
    const scene::ComponentType* const type = scene::componentRegistry().find(name != nullptr ? name : "");
    return type != nullptr ? static_cast<std::int64_t>(type->index) : -1;
}

std::uint64_t apiRegistryGeneration()
{
    return scene::componentRegistry().generation();
}

std::uint64_t apiComponentLayoutHash(std::size_t typeIndex)
{
    const scene::ComponentType* const type = scene::componentRegistry().findByIndex(typeIndex);
    return type != nullptr ? componentLayoutHash(*type->type) : 0;
}

const char* apiReadString(void* address)
{
    return static_cast<const std::string*>(address)->c_str();
}

void apiWriteString(void* address, const char* value)
{
    *static_cast<std::string*>(address) = value != nullptr ? value : "";
}

// The list field of a component type, or null.
[[nodiscard]] const reflection::FieldInfo* listField(std::size_t typeIndex, int field)
{
    const scene::ComponentType* const type = scene::componentRegistry().findByIndex(typeIndex);
    if (type == nullptr || field < 0 || static_cast<std::size_t>(field) >= type->type->fields.size())
    {
        return nullptr;
    }
    const reflection::FieldInfo& info = type->type->fields[static_cast<std::size_t>(field)];
    return info.list != nullptr ? &info : nullptr;
}

std::size_t apiListSize(std::size_t typeIndex, int field, void* component)
{
    const reflection::FieldInfo* const info = listField(typeIndex, field);
    return info != nullptr ? info->list->size(info->address(component)) : 0;
}

void* apiListElement(std::size_t typeIndex, int field, void* component, std::size_t index)
{
    const reflection::FieldInfo* const info = listField(typeIndex, field);
    return info != nullptr ? info->list->element(info->address(component), index) : nullptr;
}

void apiListResize(std::size_t typeIndex, int field, void* component, std::size_t size)
{
    if (const reflection::FieldInfo* const info = listField(typeIndex, field))
    {
        info->list->resize(info->address(component), size);
    }
}

void apiListInsert(std::size_t typeIndex, int field, void* component, std::size_t index)
{
    const reflection::FieldInfo* const info = listField(typeIndex, field);
    if (info != nullptr && index <= info->list->size(info->address(component)))
    {
        info->list->insert(info->address(component), index);
    }
}

void apiListErase(std::size_t typeIndex, int field, void* component, std::size_t index)
{
    const reflection::FieldInfo* const info = listField(typeIndex, field);
    if (info != nullptr && index < info->list->size(info->address(component)))
    {
        info->list->erase(info->address(component), index);
    }
}

Entity apiResolveEntity(void* scene, const UuidBytes* uuid)
{
    return scene != nullptr ? toScene(scene)->resolve(scene::EntityRef{toUuid(uuid)}) : Entity{};
}

void apiEntityUuid(void* scene, Entity entity, UuidBytes* uuid)
{
    writeUuid(scene != nullptr ? toScene(scene)->reference(entity).uuid : core::Uuid{}, uuid);
}

Entity apiCreateEntity(void* scene, const char* name)
{
    return scene != nullptr ? toScene(scene)->createEntity(name != nullptr ? name : "") : Entity{};
}

void apiDestroyEntity(void* scene, Entity entity)
{
    if (scene != nullptr && toScene(scene)->isAlive(entity))
    {
        toScene(scene)->destroyEntity(entity);
    }
}

int apiIsAlive(void* scene, Entity entity)
{
    return scene != nullptr && toScene(scene)->isAlive(entity) ? 1 : 0;
}

const char* apiEntityName(void* scene, Entity entity)
{
    if (scene == nullptr || !toScene(scene)->isAlive(entity))
    {
        return "";
    }
    return toScene(scene)->name(entity).c_str();
}

void apiSetEntityName(void* scene, Entity entity, const char* name)
{
    if (scene != nullptr && toScene(scene)->isAlive(entity))
    {
        toScene(scene)->setName(entity, name != nullptr ? name : "");
    }
}

Entity apiFindEntity(void* scene, const char* name)
{
    if (scene == nullptr)
    {
        return {};
    }
    const scene::Scene& current = *toScene(scene);
    const std::string_view wanted = name != nullptr ? name : "";
    for (Entity entity = current.firstRoot(); entity.isValid(); entity = current.nextSibling(entity))
    {
        // Depth-first, so that the search reaches children as well.
        std::vector<Entity> pending{entity};
        while (!pending.empty())
        {
            const Entity next = pending.back();
            pending.pop_back();
            if (current.name(next) == wanted)
            {
                return next;
            }
            for (Entity child = current.firstChild(next); child.isValid(); child = current.nextSibling(child))
            {
                pending.push_back(child);
            }
        }
    }
    return {};
}

Entity apiParent(void* scene, Entity entity)
{
    return scene != nullptr && toScene(scene)->isAlive(entity) ? toScene(scene)->parent(entity) : Entity{};
}

Entity apiFirstChild(void* scene, Entity entity)
{
    return scene != nullptr && toScene(scene)->isAlive(entity) ? toScene(scene)->firstChild(entity) : Entity{};
}

Entity apiNextSibling(void* scene, Entity entity)
{
    return scene != nullptr && toScene(scene)->isAlive(entity) ? toScene(scene)->nextSibling(entity) : Entity{};
}

void apiSetParent(void* scene, Entity child, Entity parent)
{
    if (scene == nullptr || !toScene(scene)->isAlive(child))
    {
        return;
    }
    if (core::Result<void> moved = toScene(scene)->setParent(child, optionalEntity(scene, parent)); !moved)
    {
        DEVEX_LOG_WARNING("C#: {}", moved.error());
    }
}

void* apiTransformOf(void* scene, Entity entity)
{
    if (scene == nullptr || !toScene(scene)->isAlive(entity))
    {
        return nullptr;
    }
    return toScene(scene)->tryGet<scene::Transform>(entity);
}

const float* apiWorldPositionOf(void* scene, Entity entity)
{
    if (scene == nullptr || !toScene(scene)->isAlive(entity))
    {
        return nullptr;
    }
    const scene::WorldTransform* const world = toScene(scene)->tryGet<scene::WorldTransform>(entity);
    return world != nullptr ? &world->matrix[3][0] : nullptr;
}

[[nodiscard]] const platform::Input* input() noexcept
{
    return currentFrame() != nullptr ? currentFrame()->input : nullptr;
}

int apiIsKeyDown(int key)
{
    return input() != nullptr && input()->isKeyDown(static_cast<platform::Key>(key)) ? 1 : 0;
}

int apiWasKeyPressed(int key)
{
    return input() != nullptr && input()->wasKeyPressed(static_cast<platform::Key>(key)) ? 1 : 0;
}

int apiIsMouseButtonDown(int button)
{
    return input() != nullptr && input()->isMouseButtonDown(static_cast<platform::MouseButton>(button)) ? 1 : 0;
}

int apiWasMouseButtonPressed(int button)
{
    return input() != nullptr && input()->wasMouseButtonPressed(static_cast<platform::MouseButton>(button)) ? 1 : 0;
}

void apiMouseDelta(float* values)
{
    const math::Vec2 delta = input() != nullptr ? input()->mouseDelta() : math::Vec2{0.0f};
    values[0] = delta.x;
    values[1] = delta.y;
}

void apiMousePosition(float* values)
{
    const math::Vec2 position = input() != nullptr ? input()->mousePosition() : math::Vec2{0.0f};
    values[0] = position.x;
    values[1] = position.y;
}

[[nodiscard]] platform::Window* window() noexcept
{
    return currentFrame() != nullptr ? currentFrame()->window : nullptr;
}

int apiIsMouseCaptured()
{
    return window() != nullptr && window()->isMouseCaptured() ? 1 : 0;
}

void apiSetMouseCaptured(int captured)
{
    if (window() != nullptr)
    {
        window()->setMouseCaptured(captured != 0);
    }
}

void apiRequestQuit()
{
    if (currentFrame() != nullptr)
    {
        currentFrame()->quitRequested = true;
    }
}

void apiLoadScene(const UuidBytes* scene)
{
    if (currentFrame() != nullptr)
    {
        currentFrame()->sceneToLoad = asset::AssetId{toUuid(scene)};
    }
}

Entity apiInstantiatePrefab(void* scene, const UuidBytes* prefab, Entity parent)
{
    if (scene == nullptr)
    {
        return {};
    }
    const asset::AssetId id{toUuid(prefab)};
    core::Result<Entity> instance = scene::instantiatePrefab(*toScene(scene), id, optionalEntity(scene, parent));
    if (!instance)
    {
        DEVEX_LOG_ERROR("C#: cannot instantiate prefab {}: {}", id.uuid, instance.error());
        return {};
    }
    return *instance;
}

int apiFindAsset(const char* path, UuidBytes* asset)
{
    const asset::AssetSource* const assets = currentFrame() != nullptr ? currentFrame()->assets : nullptr;
    const std::optional<asset::AssetId> found =
        assets != nullptr && path != nullptr ? assets->findByPath(path) : std::nullopt;
    writeUuid(found ? found->uuid : core::Uuid{}, asset);
    return found ? 1 : 0;
}

void apiWindowSize(float* values)
{
    const math::Extent2D size = window() != nullptr ? window()->pixelSize() : math::Extent2D{};
    values[0] = static_cast<float>(size.width);
    values[1] = static_cast<float>(size.height);
}

[[nodiscard]] physics::PhysicsWorld* physicsWorld() noexcept
{
    return currentFrame() != nullptr ? currentFrame()->physics : nullptr;
}

[[nodiscard]] std::uint16_t layerMask(std::uint32_t layers) noexcept
{
    return static_cast<std::uint16_t>(layers & physics::allLayers);
}

int apiRaycast(const math::Vec3* origin, const math::Vec3* direction, float maxDistance, std::uint32_t layers,
               Entity ignore, physics::RayHit* hit)
{
    const std::optional<physics::RayHit> found =
        physicsWorld() != nullptr
            ? physicsWorld()->raycast(*origin, *direction, maxDistance, layerMask(layers), optionalEntity(ignore))
            : std::nullopt;
    if (found)
    {
        *hit = *found;
    }
    return found ? 1 : 0;
}

int apiSphereCast(const math::Vec3* origin, float radius, const math::Vec3* direction, float maxDistance,
                  std::uint32_t layers, Entity ignore, physics::RayHit* hit)
{
    const std::optional<physics::RayHit> found =
        physicsWorld() != nullptr
            ? physicsWorld()->sphereCast(*origin, radius, *direction, maxDistance, layerMask(layers),
                                         optionalEntity(ignore))
            : std::nullopt;
    if (found)
    {
        *hit = *found;
    }
    return found ? 1 : 0;
}

int apiOverlapSphere(const math::Vec3* center, float radius, std::uint32_t layers, Entity ignore,
                     const Entity** entities)
{
    // Kept until the next query, while C# copies it.
    static std::vector<Entity> found;
    found = physicsWorld() != nullptr
                ? physicsWorld()->overlapSphere(*center, radius, layerMask(layers), optionalEntity(ignore))
                : std::vector<Entity>{};
    *entities = found.data();
    return static_cast<int>(found.size());
}

void apiAddForce(Entity entity, const math::Vec3* force)
{
    if (physicsWorld() != nullptr)
    {
        physicsWorld()->addForce(entity, *force);
    }
}

void apiAddTorque(Entity entity, const math::Vec3* torque)
{
    if (physicsWorld() != nullptr)
    {
        physicsWorld()->addTorque(entity, *torque);
    }
}

void apiAddImpulse(Entity entity, const math::Vec3* impulse)
{
    if (physicsWorld() != nullptr)
    {
        physicsWorld()->addImpulse(entity, *impulse);
    }
}

void apiAddImpulseAt(Entity entity, const math::Vec3* impulse, const math::Vec3* point)
{
    if (physicsWorld() != nullptr)
    {
        physicsWorld()->addImpulseAt(entity, *impulse, *point);
    }
}

int apiContacts(const physics::Contact** contacts)
{
    const std::span<const physics::Contact> all =
        physicsWorld() != nullptr ? physicsWorld()->contacts() : std::span<const physics::Contact>{};
    *contacts = all.data();
    return static_cast<int>(all.size());
}

[[nodiscard]] audio::AudioWorld* audioWorld() noexcept
{
    return currentFrame() != nullptr ? currentFrame()->audio : nullptr;
}

void apiPlaySound(void* scene, Entity entity)
{
    if (audioWorld() != nullptr && scene != nullptr)
    {
        audioWorld()->play(*toScene(scene), entity);
    }
}

void apiStopSound(Entity entity)
{
    if (audioWorld() != nullptr)
    {
        audioWorld()->stop(entity);
    }
}

void apiPauseSound(Entity entity)
{
    if (audioWorld() != nullptr)
    {
        audioWorld()->pause(entity);
    }
}

void apiResumeSound(Entity entity)
{
    if (audioWorld() != nullptr)
    {
        audioWorld()->resume(entity);
    }
}

int apiIsSoundPlaying(Entity entity)
{
    return audioWorld() != nullptr && audioWorld()->isPlaying(entity) ? 1 : 0;
}

void apiPlayOneShot(const UuidBytes* clip, const math::Vec3* position, float volume, std::uint32_t group, int spatial)
{
    if (audioWorld() != nullptr)
    {
        audioWorld()->playOneShot(asset::AssetId{toUuid(clip)}, *position, volume, group, spatial != 0);
    }
}

// "Master" is the volume of every group together.
[[nodiscard]] bool isMaster(std::string_view group) noexcept
{
    return group == "Master";
}

float apiGroupVolume(const char* group)
{
    if (audioWorld() == nullptr || group == nullptr)
    {
        return 1.0f;
    }
    audio::AudioEngine& engine = audioWorld()->engine();
    if (isMaster(group))
    {
        return engine.masterVolume();
    }
    const std::optional<std::uint32_t> index = engine.findGroup(group);
    return index ? engine.groupVolume(*index) : -1.0f;
}

int apiSetGroupVolume(const char* group, float volume)
{
    if (audioWorld() == nullptr || group == nullptr)
    {
        return 1;
    }
    audio::AudioEngine& engine = audioWorld()->engine();
    if (isMaster(group))
    {
        engine.setMasterVolume(volume);
        return 1;
    }
    const std::optional<std::uint32_t> index = engine.findGroup(group);
    if (index)
    {
        engine.setGroupVolume(*index, volume);
    }
    return index ? 1 : 0;
}

[[nodiscard]] animation::AnimationWorld* animationWorld() noexcept
{
    return currentFrame() != nullptr ? currentFrame()->animation : nullptr;
}

void apiPlayAnimation(void* scene, Entity entity, const UuidBytes* clip, float fade)
{
    if (animationWorld() != nullptr && scene != nullptr)
    {
        animationWorld()->play(*toScene(scene), entity, asset::AssetId{toUuid(clip)}, fade);
    }
}

void apiStopAnimation(Entity entity)
{
    if (animationWorld() != nullptr)
    {
        animationWorld()->stop(entity);
    }
}

void apiPauseAnimation(Entity entity)
{
    if (animationWorld() != nullptr)
    {
        animationWorld()->pause(entity);
    }
}

void apiResumeAnimation(Entity entity)
{
    if (animationWorld() != nullptr)
    {
        animationWorld()->resume(entity);
    }
}

int apiIsAnimationPlaying(Entity entity)
{
    return animationWorld() != nullptr && animationWorld()->isPlaying(entity) ? 1 : 0;
}

float apiAnimationTime(Entity entity)
{
    return animationWorld() != nullptr ? animationWorld()->time(entity) : 0.0f;
}

void apiSetAnimationTime(void* scene, Entity entity, float seconds)
{
    if (animationWorld() != nullptr && scene != nullptr)
    {
        animationWorld()->setTime(*toScene(scene), entity, seconds);
    }
}

[[nodiscard]] ui::UiWorld* uiWorld() noexcept
{
    return currentFrame() != nullptr ? currentFrame()->ui : nullptr;
}

int apiUiClickedAction(const char* action)
{
    return uiWorld() != nullptr && action != nullptr && uiWorld()->wasClicked(action) ? 1 : 0;
}

int apiUiClickedEntity(Entity entity)
{
    return uiWorld() != nullptr && uiWorld()->wasClicked(entity) ? 1 : 0;
}

int apiUiCancelled()
{
    return uiWorld() != nullptr && uiWorld()->wasCancelled() ? 1 : 0;
}

Entity apiUiHovered()
{
    return uiWorld() != nullptr ? uiWorld()->hovered() : Entity{};
}

Entity apiUiFocused()
{
    return uiWorld() != nullptr ? uiWorld()->focused() : Entity{};
}

void apiUiSetFocus(void* scene, Entity entity)
{
    if (uiWorld() != nullptr && scene != nullptr)
    {
        uiWorld()->setFocus(*toScene(scene), entity);
    }
}

int apiUiPointerOverInterface()
{
    return uiWorld() != nullptr && uiWorld()->pointerOverInterface() ? 1 : 0;
}

int apiUiChangedAction(const char* action)
{
    return uiWorld() != nullptr && action != nullptr && uiWorld()->wasChanged(action) ? 1 : 0;
}

int apiUiSubmittedAction(const char* action)
{
    return uiWorld() != nullptr && action != nullptr && uiWorld()->wasSubmitted(action) ? 1 : 0;
}

Entity apiUiEditedField()
{
    return uiWorld() != nullptr ? uiWorld()->editedField() : Entity{};
}

int apiProfileEnabled()
{
    return core::profiler::isEnabled() ? 1 : 0;
}

void apiProfileBegin(const char* name)
{
    // Names come from C# as text: the profiler keeps each one once.
    if (name != nullptr && core::profiler::isEnabled())
    {
        core::profiler::beginZone(core::profiler::intern(name));
    }
}

void apiProfileEnd()
{
    core::profiler::endZone();
}

[[nodiscard]] NativeApi makeNativeApi() noexcept
{
    return NativeApi{
        .log = &apiLog,
        .componentEntities = &apiComponentEntities,
        .findComponent = &apiFindComponent,
        .addComponent = &apiAddComponent,
        .removeComponent = &apiRemoveComponent,
        .componentIndex = &apiComponentIndex,
        .registryGeneration = &apiRegistryGeneration,
        .componentLayoutHash = &apiComponentLayoutHash,
        .readString = &apiReadString,
        .writeString = &apiWriteString,
        .listSize = &apiListSize,
        .listElement = &apiListElement,
        .listResize = &apiListResize,
        .listInsert = &apiListInsert,
        .listErase = &apiListErase,
        .resolveEntity = &apiResolveEntity,
        .entityUuid = &apiEntityUuid,
        .createEntity = &apiCreateEntity,
        .destroyEntity = &apiDestroyEntity,
        .isAlive = &apiIsAlive,
        .entityName = &apiEntityName,
        .setEntityName = &apiSetEntityName,
        .findEntity = &apiFindEntity,
        .parent = &apiParent,
        .firstChild = &apiFirstChild,
        .nextSibling = &apiNextSibling,
        .setParent = &apiSetParent,
        .transformOf = &apiTransformOf,
        .worldPositionOf = &apiWorldPositionOf,
        .isKeyDown = &apiIsKeyDown,
        .wasKeyPressed = &apiWasKeyPressed,
        .isMouseButtonDown = &apiIsMouseButtonDown,
        .wasMouseButtonPressed = &apiWasMouseButtonPressed,
        .mouseDelta = &apiMouseDelta,
        .mousePosition = &apiMousePosition,
        .isMouseCaptured = &apiIsMouseCaptured,
        .setMouseCaptured = &apiSetMouseCaptured,
        .requestQuit = &apiRequestQuit,
        .loadScene = &apiLoadScene,
        .instantiatePrefab = &apiInstantiatePrefab,
        .findAsset = &apiFindAsset,
        .windowSize = &apiWindowSize,
        .raycast = &apiRaycast,
        .sphereCast = &apiSphereCast,
        .overlapSphere = &apiOverlapSphere,
        .addForce = &apiAddForce,
        .addTorque = &apiAddTorque,
        .addImpulse = &apiAddImpulse,
        .addImpulseAt = &apiAddImpulseAt,
        .contacts = &apiContacts,
        .playSound = &apiPlaySound,
        .stopSound = &apiStopSound,
        .pauseSound = &apiPauseSound,
        .resumeSound = &apiResumeSound,
        .isSoundPlaying = &apiIsSoundPlaying,
        .playOneShot = &apiPlayOneShot,
        .groupVolume = &apiGroupVolume,
        .setGroupVolume = &apiSetGroupVolume,
        .playAnimation = &apiPlayAnimation,
        .stopAnimation = &apiStopAnimation,
        .pauseAnimation = &apiPauseAnimation,
        .resumeAnimation = &apiResumeAnimation,
        .isAnimationPlaying = &apiIsAnimationPlaying,
        .animationTime = &apiAnimationTime,
        .setAnimationTime = &apiSetAnimationTime,
        .uiClickedAction = &apiUiClickedAction,
        .uiClickedEntity = &apiUiClickedEntity,
        .uiCancelled = &apiUiCancelled,
        .uiHovered = &apiUiHovered,
        .uiFocused = &apiUiFocused,
        .uiSetFocus = &apiUiSetFocus,
        .uiPointerOverInterface = &apiUiPointerOverInterface,
        .uiChangedAction = &apiUiChangedAction,
        .uiSubmittedAction = &apiUiSubmittedAction,
        .uiEditedField = &apiUiEditedField,
        .profileEnabled = &apiProfileEnabled,
        .profileBegin = &apiProfileBegin,
        .profileEnd = &apiProfileEnd,
    };
}

// The kinds a C# field can have, as the runtime names them.
[[nodiscard]] std::optional<reflection::ValueKind> parseKind(std::string_view kind) noexcept
{
    using reflection::ValueKind;
    if (kind == "bool") return ValueKind::Bool;
    if (kind == "int") return ValueKind::Int32;
    if (kind == "uint") return ValueKind::UInt32;
    if (kind == "float") return ValueKind::Float;
    if (kind == "string") return ValueKind::String;
    if (kind == "vec2") return ValueKind::Vec2;
    if (kind == "vec3") return ValueKind::Vec3;
    if (kind == "vec4") return ValueKind::Vec4;
    if (kind == "quat") return ValueKind::Quat;
    if (kind == "uuid") return ValueKind::Uuid;
    if (kind == "asset") return ValueKind::AssetId;
    if (kind == "enum") return ValueKind::Enum;
    if (kind == "entity") return ValueKind::Entity;
    return std::nullopt;
}

[[nodiscard]] const std::string* stringAttribute(const serialization::TextSection& section, std::string_view key)
{
    const serialization::TextValue* const value = section.findAttribute(key);
    return value != nullptr ? serialization::asString(*value) : nullptr;
}

[[nodiscard]] bool boolAttribute(const serialization::TextSection& section, std::string_view key)
{
    const serialization::TextValue* const value = section.findAttribute(key);
    return value != nullptr && serialization::asBool(*value).value_or(false);
}

[[nodiscard]] std::vector<std::string> splitValues(std::string_view text)
{
    std::vector<std::string> values;
    while (!text.empty())
    {
        const std::size_t comma = text.find(',');
        values.emplace_back(text.substr(0, comma));
        if (comma == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return values;
}

} // namespace

// Keeps the two function tables of a runtime.
class ManagedGame::Impl
{
public:
    NativeApi native{};
    ManagedApi managed{};
    std::vector<std::string> types;
    bool assemblyLoaded = false;
    // A component type outliving this runtime must not call into .NET any more.
    std::shared_ptr<const bool> alive = std::make_shared<const bool>(true);
};

namespace {

#ifdef _WIN32
using HostChar = wchar_t;
[[nodiscard]] std::wstring hostString(const std::filesystem::path& path)
{
    return path.wstring();
}
#else
using HostChar = char;
[[nodiscard]] std::string hostString(const std::filesystem::path& path)
{
    return path.string();
}
#endif

using InitializeForConfig = int (*)(const HostChar* runtimeConfig, const void* parameters, void** context);
using InitializeForCommandLine = int (*)(int argc, const HostChar** argv, const void* parameters, void** context);
using GetRuntimeDelegate = int (*)(void* context, int type, void** result);
using LoadAssemblyAndGetFunctionPointer = int (*)(const HostChar* assemblyPath, const HostChar* typeName,
                                                  const HostChar* methodName, const HostChar* delegateType,
                                                  void* reserved, void** result);
// hostfxr's hdt_load_assembly_and_get_function_pointer.
constexpr int loadAssemblyDelegate = 5;

[[nodiscard]] std::optional<std::string> environmentVariable(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) != 0 || value == nullptr)
    {
        return std::nullopt;
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* const value = std::getenv(name);
    return value != nullptr ? std::optional(std::string(value)) : std::nullopt;
#endif
}

// The folder .NET is installed in, where hostfxr lives.
[[nodiscard]] std::optional<std::filesystem::path> dotnetRoot()
{
    std::error_code error;
    if (const std::optional<std::string> root = environmentVariable("DOTNET_ROOT"))
    {
        const std::filesystem::path path = core::pathFromUtf8(*root);
        if (std::filesystem::is_directory(path, error))
        {
            return path;
        }
    }
#ifdef _WIN32
    for (const char* const variable : {"ProgramFiles", "ProgramW6432"})
    {
        const std::optional<std::string> programFiles = environmentVariable(variable);
        if (!programFiles)
        {
            continue;
        }
        const std::filesystem::path path = core::pathFromUtf8(*programFiles) / "dotnet";
        if (std::filesystem::is_directory(path, error))
        {
            return path;
        }
    }
#else
    for (const char* const path : {"/usr/share/dotnet", "/usr/lib/dotnet"})
    {
        if (std::filesystem::is_directory(path, error))
        {
            return std::filesystem::path(path);
        }
    }
#endif
    return std::nullopt;
}

// The newest hostfxr next to the .NET installation, or the one an exported game ships.
[[nodiscard]] core::Result<std::filesystem::path> findHostfxr(const std::filesystem::path& managedDirectory)
{
    std::error_code shipped;
#ifdef _WIN32
    const std::filesystem::path host = managedDirectory / "hostfxr.dll";
#else
    const std::filesystem::path host = managedDirectory / "libhostfxr.so";
#endif
    if (std::filesystem::is_regular_file(host, shipped))
    {
        return host;
    }
    const std::optional<std::filesystem::path> root = dotnetRoot();
    if (!root)
    {
        return core::makeError(core::ErrorCode::NotFound, ".NET is not installed (no dotnet folder)");
    }
    std::error_code error;
    std::filesystem::path newest;
    std::string newestName;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(*root / "host" / "fxr", error))
    {
        const std::string name = core::toUtf8(entry.path().filename());
        if (entry.is_directory(error) && name > newestName)
        {
            newestName = name;
            newest = entry.path();
        }
    }
    if (newest.empty())
    {
        return core::makeError(core::ErrorCode::NotFound, "no .NET host in '{}'", core::toUtf8(*root));
    }
#ifdef _WIN32
    return newest / "hostfxr.dll";
#else
    return newest / "libhostfxr.so";
#endif
}

// .NET, which starts once per process and stays: the runtime cannot be unloaded. Returns the entry
// point of Devex.Managed, which each ManagedGame calls with its function tables.
[[nodiscard]] core::Result<void*> startDotnet(const std::filesystem::path& managedDirectory)
{
    static std::optional<platform::SharedLibrary> hostfxr;
    static void* entryPoint = nullptr;
    if (entryPoint != nullptr)
    {
        return entryPoint;
    }

    const std::filesystem::path runtimeAssembly = managedDirectory / "Devex.Managed.dll";
    std::error_code error;
    if (!std::filesystem::exists(runtimeAssembly, error))
    {
        return core::makeError(core::ErrorCode::NotFound, "'{}' is missing: C# is unavailable",
                               core::toUtf8(runtimeAssembly));
    }
    // An exported game ships .NET with its own configuration; a project uses the engine's.
    std::filesystem::path runtimeConfig = managedDirectory / "Devex.Managed.runtimeconfig.json";
    if (!std::filesystem::exists(runtimeConfig, error))
    {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(managedDirectory, error))
        {
            if (core::toUtf8(entry.path().filename()).ends_with(".runtimeconfig.json"))
            {
                runtimeConfig = entry.path();
                break;
            }
        }
    }
    if (!std::filesystem::exists(runtimeConfig, error))
    {
        return core::makeError(core::ErrorCode::NotFound, "no .NET configuration in '{}'", core::toUtf8(managedDirectory));
    }
    const core::Result<std::filesystem::path> hostfxrPath = findHostfxr(managedDirectory);
    if (!hostfxrPath)
    {
        return std::unexpected(hostfxrPath.error());
    }
    const bool shippedRuntime = hostfxrPath->parent_path() == managedDirectory;

    core::Result<platform::SharedLibrary> library = platform::SharedLibrary::load(*hostfxrPath);
    if (!library)
    {
        return std::unexpected(library.error());
    }
    hostfxr.emplace(std::move(*library));
    const auto initialize =
        reinterpret_cast<InitializeForConfig>(hostfxr->function("hostfxr_initialize_for_runtime_config"));
    const auto initializeApp =
        reinterpret_cast<InitializeForCommandLine>(hostfxr->function("hostfxr_initialize_for_dotnet_command_line"));
    const auto getDelegate = reinterpret_cast<GetRuntimeDelegate>(hostfxr->function("hostfxr_get_runtime_delegate"));
    if (initialize == nullptr || getDelegate == nullptr)
    {
        return core::makeError(core::ErrorCode::Unsupported, "'{}' is not a usable .NET host",
                               core::toUtf8(*hostfxrPath));
    }

    // A .NET shipped with a game holds the whole runtime, which starts from its application; the
    // .NET installed on the machine starts from the configuration of the engine's own assembly.
    void* context = nullptr;
    int status = 0;
    if (shippedRuntime)
    {
        std::filesystem::path application = runtimeConfig;
        application.replace_extension();
        application.replace_extension(".dll");
        const auto path = hostString(application);
        const HostChar* argv[]{path.c_str()};
        status = initializeApp != nullptr ? initializeApp(1, argv, nullptr, &context) : -1;
    }
    else
    {
        const auto config = hostString(runtimeConfig);
        status = initialize(config.c_str(), nullptr, &context);
    }
    if (status != 0 || context == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot start .NET for '{}' (status 0x{:x})",
                               core::toUtf8(runtimeConfig), static_cast<unsigned>(status));
    }
    // The context stays open with the runtime.
    void* loader = nullptr;
    if (const int delegateStatus = getDelegate(context, loadAssemblyDelegate, &loader);
        delegateStatus != 0 || loader == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot reach the .NET loader (status 0x{:x})",
                               static_cast<unsigned>(delegateStatus));
    }

    const auto assembly = hostString(runtimeAssembly);
    const auto* const unmanagedOnly = reinterpret_cast<const HostChar*>(-1);
    void* found = nullptr;
#ifdef _WIN32
    const int loaded = reinterpret_cast<LoadAssemblyAndGetFunctionPointer>(loader)(
        assembly.c_str(), L"Devex.Bootstrap, Devex.Managed", L"Initialize", unmanagedOnly, nullptr, &found);
#else
    const int loaded = reinterpret_cast<LoadAssemblyAndGetFunctionPointer>(loader)(
        assembly.c_str(), "Devex.Bootstrap, Devex.Managed", "Initialize", unmanagedOnly, nullptr, &found);
#endif
    if (loaded != 0 || found == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot start the C# runtime (status 0x{:x})",
                               static_cast<unsigned>(loaded));
    }
    DEVEX_LOG_DEBUG("C# runtime started from {}", core::toUtf8(runtimeAssembly));
    entryPoint = found;
    return entryPoint;
}

} // namespace

ManagedGame::ManagedGame(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

ManagedGame::~ManagedGame()
{
    unloadAssembly();
}

core::Result<std::unique_ptr<ManagedGame>> ManagedGame::create(const std::filesystem::path& managedDirectory)
{
    const core::Result<void*> entryPoint = startDotnet(managedDirectory);
    if (!entryPoint)
    {
        return std::unexpected(entryPoint.error());
    }
    auto impl = std::make_unique<Impl>();
    impl->native = makeNativeApi();
    BootstrapArguments arguments{.version = bootstrapVersion, .native = &impl->native, .managed = &impl->managed};
    const auto bootstrap = reinterpret_cast<int (*)(void*, int)>(*entryPoint);
    if (const int started = bootstrap(&arguments, static_cast<int>(sizeof(arguments))); started != 0)
    {
        if (started == -2)
        {
            return core::makeError(core::ErrorCode::Platform, "Devex.Managed.dll belongs to another build of the engine");
        }
        return core::makeError(core::ErrorCode::Platform, "the C# runtime refused to start (status {})", started);
    }
    if (impl->managed.loadGame == nullptr || impl->managed.runPhase == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "the C# runtime did not fill its functions");
    }
    return std::unique_ptr<ManagedGame>(new ManagedGame(std::move(impl)));
}

core::Result<void> ManagedGame::loadAssembly(const std::filesystem::path& assembly)
{
    unloadAssembly();
    const std::string path = core::toUtf8(assembly);
    const char* description = nullptr;
    if (m_impl->managed.loadGame(path.c_str(), &description) != 0 || description == nullptr)
    {
        return core::makeError(core::ErrorCode::InvalidState, "cannot load '{}'", path);
    }
    m_impl->assemblyLoaded = true;

    const core::Result<serialization::TextDocument> document = serialization::parseText(description);
    if (!document)
    {
        unloadAssembly();
        return core::makeError(document.error().code, "the C# components cannot be read: {}", document.error().message);
    }

    // Each [component] section and the [field] sections that follow describe one type.
    std::string typeName;
    std::vector<scene::DynamicField> fields;
    const auto registerType = [&]() {
        if (typeName.empty())
        {
            return;
        }
        // A new component starts with the values the C# class gives its fields.
        auto initialize = [alive = std::weak_ptr<const bool>(m_impl->alive), managed = &m_impl->managed,
                           name = typeName](void* component) {
            if (!alive.expired())
            {
                managed->applyDefaults(name.c_str(), component);
            }
        };
        core::Result<std::shared_ptr<const scene::DynamicComponentLayout>> layout =
            scene::DynamicComponentLayout::create(typeName, fields, std::move(initialize));
        if (!layout)
        {
            DEVEX_LOG_ERROR("The C# component {} is ignored: {}", typeName, layout.error());
        }
        else if (!scene::componentRegistry().addDynamic(*layout))
        {
            DEVEX_LOG_ERROR("The C# component {} is ignored: a component already has this name", typeName);
        }
        else
        {
            const scene::ComponentType& type = *scene::componentRegistry().find(typeName);
            m_impl->managed.setTypeLayout(typeName.c_str(), type.index, (*layout)->offsets().data(),
                                          static_cast<int>((*layout)->offsets().size()));
            m_impl->types.push_back(typeName);
        }
        typeName.clear();
        fields.clear();
    };

    for (const serialization::TextSection& section : document->sections)
    {
        if (section.type == "component")
        {
            registerType();
            if (const std::string* const name = stringAttribute(section, "type"))
            {
                typeName = *name;
            }
        }
        else if (section.type == "field" && !typeName.empty())
        {
            const std::string* const name = stringAttribute(section, "name");
            const std::string* const kind = stringAttribute(section, "kind");
            const std::optional<reflection::ValueKind> valueKind = kind != nullptr ? parseKind(*kind) : std::nullopt;
            if (name == nullptr || !valueKind)
            {
                continue;
            }
            const std::string* const values = stringAttribute(section, "values");
            const std::string* const assetType = stringAttribute(section, "asset_type");
            fields.push_back({
                .name = *name,
                .kind = *valueKind,
                .assetType = assetType != nullptr ? *assetType : std::string(),
                .color = boolAttribute(section, "color"),
                .angle = boolAttribute(section, "angle"),
                .physicsLayer = boolAttribute(section, "physics_layer"),
                .audioGroup = boolAttribute(section, "audio_group"),
                .enumNames = values != nullptr ? splitValues(*values) : std::vector<std::string>{},
                .list = boolAttribute(section, "list"),
            });
        }
    }
    registerType();
    return {};
}

void ManagedGame::unloadAssembly()
{
    if (!m_impl->assemblyLoaded)
    {
        return;
    }
    for (const std::string& name : m_impl->types)
    {
        static_cast<void>(scene::componentRegistry().remove(name));
    }
    m_impl->types.clear();
    m_impl->managed.unloadGame();
    m_impl->assemblyLoaded = false;
}

bool ManagedGame::hasAssembly() const noexcept
{
    return m_impl->assemblyLoaded;
}

std::span<const std::string> ManagedGame::componentTypes() const noexcept
{
    return m_impl->types;
}

void ManagedGame::runPhase(Frame& frame, SystemPhase phase)
{
    if (!m_impl->assemblyLoaded || frame.scene == nullptr)
    {
        return;
    }
    currentFrame() = &frame;
    m_impl->managed.runPhase(frame.scene, static_cast<int>(phase), static_cast<float>(frame.delta.count()));
    currentFrame() = nullptr;
}

std::size_t ManagedGame::release(scene::Scene& scene) const
{
    std::size_t preserved = 0;
    for (const std::string& name : m_impl->types)
    {
        if (const scene::ComponentType* const type = scene::componentRegistry().find(name))
        {
            preserved += scene::preserveComponentPool(scene, type->index);
        }
    }
    return preserved;
}

bool ManagedGame::isDebuggerAttached() const
{
    return m_impl->managed.isDebuggerAttached != nullptr && m_impl->managed.isDebuggerAttached() != 0;
}

} // namespace devex::runtime::detail
