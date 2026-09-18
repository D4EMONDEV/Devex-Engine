#include "ManagedGame.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/SharedLibrary.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/DynamicComponent.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <system_error>
#include <utility>

namespace devex::runtime::detail {
namespace {

using scene::Entity;

// The functions the C# runtime calls, in the order of Devex.Managed's NativeApi.
struct NativeApi
{
    void (*log)(int level, const char* message);

    int (*componentEntities)(void* scene, std::size_t typeIndex, const Entity** entities);
    void* (*findComponent)(void* scene, std::size_t typeIndex, Entity entity);
    void* (*addComponent)(void* scene, std::size_t typeIndex, Entity entity);
    void (*removeComponent)(void* scene, std::size_t typeIndex, Entity entity);
    const char* (*readStringField)(void* component, std::size_t offset);
    void (*writeStringField)(void* component, std::size_t offset, const char* value);

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
};

// The functions the engine calls, in the order of Devex.Managed's ManagedApi.
struct ManagedApi
{
    int (*loadGame)(const char* assemblyPath, const char** description);
    void (*unloadGame)();
    void (*setTypeLayout)(const char* typeName, std::size_t typeIndex, const std::size_t* offsets, int count);
    void (*runPhase)(void* scene, int phase, float delta);
    void (*applyDefaults)(const char* typeName, void* component);
};

struct BootstrapArguments
{
    const NativeApi* native;
    ManagedApi* managed;
};

// The engine services the C# API reaches, set while a managed game exists.
[[nodiscard]] ManagedGame::Services& services() noexcept
{
    static ManagedGame::Services current;
    return current;
}

[[nodiscard]] scene::Scene* toScene(void* scene) noexcept
{
    return static_cast<scene::Scene*>(scene);
}

// Layouts of the Transform and of the entity handle must match the C# structures.
static_assert(sizeof(scene::Transform) == 40);
static_assert(offsetof(scene::Transform, position) == 0);
static_assert(offsetof(scene::Transform, rotation) == 12);
static_assert(offsetof(scene::Transform, scale) == 28);
static_assert(sizeof(Entity) == 8);
static_assert(sizeof(math::Quat) == 16);

void apiLog(int level, const char* message)
{
    const auto logLevel = static_cast<core::LogLevel>(std::clamp(level, 0, 5));
    core::logMessage(logLevel, message != nullptr ? message : "");
}

int apiComponentEntities(void* scene, std::size_t typeIndex, const Entity** entities)
{
    const scene::ComponentPoolBase* const pool = toScene(scene)->componentPool(typeIndex);
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
    scene::DynamicComponentPool* const pool = toScene(scene)->dynamicPool(typeIndex);
    return pool != nullptr ? pool->find(entity) : nullptr;
}

void* apiAddComponent(void* scene, std::size_t typeIndex, Entity entity)
{
    const scene::ComponentType* const type = scene::componentRegistry().findByIndex(typeIndex);
    return type != nullptr && toScene(scene)->isAlive(entity) ? type->emplace(*toScene(scene), entity) : nullptr;
}

void apiRemoveComponent(void* scene, std::size_t typeIndex, Entity entity)
{
    if (const scene::ComponentType* const type = scene::componentRegistry().findByIndex(typeIndex))
    {
        type->remove(*toScene(scene), entity);
    }
}

const char* apiReadStringField(void* component, std::size_t offset)
{
    const auto* const field =
        static_cast<const std::string*>(static_cast<const void*>(static_cast<const std::byte*>(component) + offset));
    return field->c_str();
}

void apiWriteStringField(void* component, std::size_t offset, const char* value)
{
    auto* const field = static_cast<std::string*>(static_cast<void*>(static_cast<std::byte*>(component) + offset));
    *field = value != nullptr ? value : "";
}

Entity apiCreateEntity(void* scene, const char* name)
{
    return toScene(scene)->createEntity(name != nullptr ? name : "");
}

void apiDestroyEntity(void* scene, Entity entity)
{
    toScene(scene)->destroyEntity(entity);
}

int apiIsAlive(void* scene, Entity entity)
{
    return toScene(scene)->isAlive(entity) ? 1 : 0;
}

const char* apiEntityName(void* scene, Entity entity)
{
    const scene::Scene& current = *toScene(scene);
    return current.isAlive(entity) ? current.name(entity).c_str() : "";
}

void apiSetEntityName(void* scene, Entity entity, const char* name)
{
    if (toScene(scene)->isAlive(entity))
    {
        toScene(scene)->setName(entity, name != nullptr ? name : "");
    }
}

Entity apiFindEntity(void* scene, const char* name)
{
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
    return toScene(scene)->isAlive(entity) ? toScene(scene)->parent(entity) : Entity{};
}

Entity apiFirstChild(void* scene, Entity entity)
{
    return toScene(scene)->isAlive(entity) ? toScene(scene)->firstChild(entity) : Entity{};
}

Entity apiNextSibling(void* scene, Entity entity)
{
    return toScene(scene)->isAlive(entity) ? toScene(scene)->nextSibling(entity) : Entity{};
}

void apiSetParent(void* scene, Entity child, Entity parent)
{
    if (core::Result<void> moved = toScene(scene)->setParent(child, parent); !moved)
    {
        DEVEX_LOG_WARNING("C#: {}", moved.error());
    }
}

void* apiTransformOf(void* scene, Entity entity)
{
    scene::Scene& current = *toScene(scene);
    return current.isAlive(entity) ? current.tryGet<scene::Transform>(entity) : nullptr;
}

const float* apiWorldPositionOf(void* scene, Entity entity)
{
    scene::Scene& current = *toScene(scene);
    const scene::WorldTransform* const world =
        current.isAlive(entity) ? current.tryGet<scene::WorldTransform>(entity) : nullptr;
    return world != nullptr ? &world->matrix[3][0] : nullptr;
}

int apiIsKeyDown(int key)
{
    return services().input != nullptr && services().input->isKeyDown(static_cast<platform::Key>(key)) ? 1 : 0;
}

int apiWasKeyPressed(int key)
{
    return services().input != nullptr && services().input->wasKeyPressed(static_cast<platform::Key>(key)) ? 1 : 0;
}

int apiIsMouseButtonDown(int button)
{
    return services().input != nullptr && services().input->isMouseButtonDown(static_cast<platform::MouseButton>(button))
               ? 1
               : 0;
}

int apiWasMouseButtonPressed(int button)
{
    return services().input != nullptr &&
                   services().input->wasMouseButtonPressed(static_cast<platform::MouseButton>(button))
               ? 1
               : 0;
}

void apiMouseDelta(float* values)
{
    const math::Vec2 delta = services().input != nullptr ? services().input->mouseDelta() : math::Vec2{0.0f};
    values[0] = delta.x;
    values[1] = delta.y;
}

void apiMousePosition(float* values)
{
    const math::Vec2 position = services().input != nullptr ? services().input->mousePosition() : math::Vec2{0.0f};
    values[0] = position.x;
    values[1] = position.y;
}

int apiIsMouseCaptured()
{
    return services().window != nullptr && services().window->isMouseCaptured() ? 1 : 0;
}

void apiSetMouseCaptured(int captured)
{
    if (services().window != nullptr)
    {
        services().window->setMouseCaptured(captured != 0);
    }
}

void apiRequestQuit()
{
    if (services().quitRequested != nullptr)
    {
        *services().quitRequested = true;
    }
}

[[nodiscard]] NativeApi makeNativeApi() noexcept
{
    return NativeApi{
        .log = &apiLog,
        .componentEntities = &apiComponentEntities,
        .findComponent = &apiFindComponent,
        .addComponent = &apiAddComponent,
        .removeComponent = &apiRemoveComponent,
        .readStringField = &apiReadStringField,
        .writeStringField = &apiWriteStringField,
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

// Hosts .NET and keeps the two function tables.
class ManagedGame::Impl
{
public:
    std::optional<platform::SharedLibrary> hostfxr;
    void* hostContext = nullptr;
    int (*closeHost)(void*) = nullptr;
    NativeApi native{};
    ManagedApi managed{};
    std::vector<std::string> types;
    bool assemblyLoaded = false;
    // A component type outliving this host must not call into .NET any more.
    std::shared_ptr<const bool> alive = std::make_shared<const bool>(true);

    ~Impl()
    {
        if (closeHost != nullptr && hostContext != nullptr)
        {
            closeHost(hostContext);
        }
    }
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
using CloseHost = int (*)(void* context);
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

} // namespace

ManagedGame::ManagedGame(std::unique_ptr<Impl> impl) noexcept
    : m_impl(std::move(impl))
{
}

ManagedGame::~ManagedGame()
{
    unloadAssembly();
    services() = {};
}

core::Result<std::unique_ptr<ManagedGame>> ManagedGame::create(const std::filesystem::path& managedDirectory,
                                                               Services servicesToUse)
{
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
    const bool shippedRuntime = hostfxrPath && hostfxrPath->parent_path() == managedDirectory;
    if (!hostfxrPath)
    {
        return std::unexpected(hostfxrPath.error());
    }

    auto impl = std::make_unique<Impl>();
    core::Result<platform::SharedLibrary> library = platform::SharedLibrary::load(*hostfxrPath);
    if (!library)
    {
        return std::unexpected(library.error());
    }
    impl->hostfxr.emplace(std::move(*library));
    const auto initialize =
        reinterpret_cast<InitializeForConfig>(impl->hostfxr->function("hostfxr_initialize_for_runtime_config"));
    const auto initializeApp =
        reinterpret_cast<InitializeForCommandLine>(impl->hostfxr->function("hostfxr_initialize_for_dotnet_command_line"));
    const auto getDelegate = reinterpret_cast<GetRuntimeDelegate>(impl->hostfxr->function("hostfxr_get_runtime_delegate"));
    impl->closeHost = reinterpret_cast<CloseHost>(impl->hostfxr->function("hostfxr_close"));
    if (initialize == nullptr || getDelegate == nullptr || impl->closeHost == nullptr)
    {
        return core::makeError(core::ErrorCode::Unsupported, "'{}' is not a usable .NET host",
                               core::toUtf8(*hostfxrPath));
    }

    // A .NET shipped with a game holds the whole runtime, which starts from its application; the
    // .NET installed on the machine starts from the configuration of the engine's own assembly.
    int status = 0;
    if (shippedRuntime)
    {
        std::filesystem::path application = runtimeConfig;
        application.replace_extension();
        application.replace_extension(".dll");
        const auto path = hostString(application);
        const HostChar* argv[]{path.c_str()};
        status = initializeApp != nullptr ? initializeApp(1, argv, nullptr, &impl->hostContext) : -1;
    }
    else
    {
        const auto config = hostString(runtimeConfig);
        status = initialize(config.c_str(), nullptr, &impl->hostContext);
    }
    if (status != 0 || impl->hostContext == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot start .NET for '{}' (status 0x{:x})",
                               core::toUtf8(runtimeConfig), static_cast<unsigned>(status));
    }
    void* loader = nullptr;
    if (const int delegateStatus = getDelegate(impl->hostContext, loadAssemblyDelegate, &loader);
        delegateStatus != 0 || loader == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot reach the .NET loader (status 0x{:x})",
                               static_cast<unsigned>(delegateStatus));
    }

    const auto assembly = hostString(runtimeAssembly);
    const auto* const unmanagedOnly = reinterpret_cast<const HostChar*>(-1);
    void* entryPoint = nullptr;
#ifdef _WIN32
    const int loaded = reinterpret_cast<LoadAssemblyAndGetFunctionPointer>(loader)(
        assembly.c_str(), L"Devex.Bootstrap, Devex.Managed", L"Initialize", unmanagedOnly, nullptr, &entryPoint);
#else
    const int loaded = reinterpret_cast<LoadAssemblyAndGetFunctionPointer>(loader)(
        assembly.c_str(), "Devex.Bootstrap, Devex.Managed", "Initialize", unmanagedOnly, nullptr, &entryPoint);
#endif
    if (loaded != 0 || entryPoint == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot start the C# runtime (status 0x{:x})",
                               static_cast<unsigned>(loaded));
    }

    services() = servicesToUse;
    impl->native = makeNativeApi();
    BootstrapArguments arguments{.native = &impl->native, .managed = &impl->managed};
    const auto bootstrap = reinterpret_cast<int (*)(void*, int)>(entryPoint);
    if (const int started = bootstrap(&arguments, static_cast<int>(sizeof(arguments))); started != 0)
    {
        return core::makeError(core::ErrorCode::Platform, "the C# runtime refused to start (status {})", started);
    }
    if (impl->managed.loadGame == nullptr || impl->managed.runPhase == nullptr)
    {
        return core::makeError(core::ErrorCode::Platform, "the C# runtime did not fill its functions");
    }
    DEVEX_LOG_DEBUG("C# runtime started from {}", core::toUtf8(runtimeAssembly));
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
            fields.push_back({
                .name = *name,
                .kind = *valueKind,
                .assetType = stringAttribute(section, "asset_type") != nullptr ? *stringAttribute(section, "asset_type")
                                                                              : std::string(),
                .color = boolAttribute(section, "color"),
                .angle = boolAttribute(section, "angle"),
                .physicsLayer = boolAttribute(section, "physics_layer"),
                .enumNames = values != nullptr ? splitValues(*values) : std::vector<std::string>{},
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

void ManagedGame::runPhase(scene::Scene& scene, SystemPhase phase, core::Duration delta)
{
    if (m_impl->assemblyLoaded)
    {
        m_impl->managed.runPhase(&scene, static_cast<int>(phase), static_cast<float>(delta.count()));
    }
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

} // namespace devex::runtime::detail
