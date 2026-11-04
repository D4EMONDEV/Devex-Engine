#pragma once

#include <devex/core/Export.hpp>

#include <devex/animation/AnimationWorld.hpp>
#include <devex/asset/Localization.hpp>
#include <devex/animation/TweenWorld.hpp>
#include <devex/audio/AudioWorld.hpp>
#include <devex/core/Time.hpp>
#include <devex/particles/ParticleWorld.hpp>
#include <devex/navigation/NavigationWorld.hpp>
#include <devex/net/Multiplayer.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/physics2d/Physics2DWorld.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Window.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/runtime/InputActions.hpp>
#include <devex/runtime/PlayerSettings.hpp>
#include <devex/runtime/SaveGames.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/ui/UiWorld.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

// Game code: components that hold the state of a game and systems that update it, compiled into a
// game module, a shared library that the editor and the player load and reload while they run.
// All the state lives in the scene, so that playing a copy of a scene or reloading the module
// loses nothing.
namespace devex::runtime {

class CoroutineScheduler;
class GameRegistry;

// Changes whenever game modules must be rebuilt to load: modules built for another version are
// refused. 9: ComponentType gained findMutable, which modules fill in when they register a type.
// 10: SystemContext gained the loading of scenes in the background.
// 11: SystemContext gained the input actions of the project.
// 12: SystemContext gained the saves and the settings of the player.
// 13: SystemContext gained the tweens and the coroutines.
// 14: SystemContext gained the particles.
// 15: Camera gained its orthographic projection, which moved its fields; sprites.
// 20: TilesetData gained its terrain sets, and its tiles their terrains.
// 21: the 2D lights: sprites, tiles and fields of reflection gained what they need.
// 22: the reflection probes and the irradiance volumes: RenderWorld, RenderView and CapturedImage
// gained what they need.
// 23: the interface: FontData keeps its file, UiImage gained its outline and its shadow, UiInput
// its focus ring, UiDropdown the shadow of its list. 24: RendererStats gained the shadow passes a
// frame kept. 25: UiToggle gained the colour of its tick. 26: the atmosphere and the fog:
// RenderWorld, RenderSun and DirectionalLight gained what they need. 30: RendererStats gained the
// main camera's GPU visibility counts; RendererConfig its occlusion.
// 31: RendererConfig and RendererStats gained compute skinning and its counters.
// 34: RenderWorld gained tool line width and the grid's depth plane.
// 35: TileData gained custom collision outlines.
// 36: UiWorld gained the appearance and measured layout of dropdown menus.
// 37: DynamicField gained inspector group metadata for scripted components.
// 38: Project gained ordered global scenes.
// 39: WindowSettings gained quitting on Android's Back button, RenderWorld its decals, and the
// scene the Decal component.
// 40: RenderWorld gained the volumetric fog and its volumes, and the scene the VolumetricFog and
// FogVolume components.
// 41: ApplicationConfig gained supervised Play. 42: Project gained addon activation settings.
// 43: RenderCamera, Camera and PostProcessVolume gained the reflections of the screen.
// 44: ExportSettings, in Project, gained the export for Android.
// 45: SystemContext gained the network of the game.
// 46: SystemContext gained the game code that runs, and GameRegistry its remote calls.
inline constexpr std::uint32_t gameApiVersion = 46;

enum class SystemPhase : std::uint8_t
{
    // Once when the game starts: when Play starts in the editor, or at launch in the player.
    Start,
    // At the fixed update rate, for simulation, before each physics step.
    FixedUpdate,
    // Once per frame, for input and game logic, before animation and interpolation.
    Update,
    // Once per played frame, after animation and physics interpolation, for cameras and visuals.
    LateUpdate,
};

// What a system works on during one call.
struct DEVEX_API SystemContext
{
    scene::Scene& scene;
    // The keys, buttons and sticks themselves. Actions are what games read rather than keys.
    const platform::Input& input;
    platform::Window& window;
    AssetManager& assets;
    // The actions of the project ("Jump", "Move"), their contexts and the bindings of the player.
    // Null when the application runs without them.
    InputActions* actions = nullptr;
    // The saves of the game, in the folder of the user; the slot the scene was restored from is
    // there while its Start systems run. Null when the application runs without them.
    SaveGames* saves = nullptr;
    // What the player chose: volumes, window, and the values the game keeps. Null likewise.
    PlayerSettings* settings = nullptr;
    // The translations of the game and the language it shows: translate gives the text of a key,
    // and a language set here is kept as the player's choice. Null outside a game.
    asset::Localization* localization = nullptr;
    // The simulation of the scene: queries, forces, and the contacts of the steps of this frame,
    // which Update systems see once. Null when the application runs without physics.
    physics::PhysicsWorld* physics = nullptr;
    // The 2D simulation of the scene, beside the 3D one: queries, forces and contacts likewise.
    physics2d::Physics2DWorld* physics2d = nullptr;
    // The navigation of the scene: its agents, their destinations, and paths along its navigation
    // mesh. Null when the application runs without it.
    navigation::NavigationWorld* navigation = nullptr;
    // The sounds of the scene: its AudioSource components, one-shot sounds and the volumes of the
    // groups. Null when the application runs without audio.
    audio::AudioWorld* audio = nullptr;
    // The network of the game: hosting or joining a session, and who is in it. Null outside a game.
    net::Multiplayer* multiplayer = nullptr;
    // What the game code of this module declared, which remote calls run. Null outside a game.
    const GameRegistry* game = nullptr;
    // The animations of the scene: the clips its Animator components play. Null when the
    // application runs without animation.
    animation::AnimationWorld* animation = nullptr;
    // The tweens of the scene: those code plays and those of its Tweener components. Null likewise.
    animation::TweenWorld* tweens = nullptr;
    // The coroutines of the game code, which runtime/Coroutine.hpp describes. Null outside a game.
    CoroutineScheduler* coroutines = nullptr;
    // The particles of the scene: its emitters, which code plays, stops and bursts. Null likewise.
    particles::ParticleWorld* particles = nullptr;
    // The interface of the scene: its canvases, laid out every frame, and the buttons they
    // answered this frame. Null when the application runs without an interface.
    ui::UiWorld* ui = nullptr;
    // The fixed step during FixedUpdate, the time since the previous frame during Update, zero
    // during Start.
    core::Duration delta{0.0};
    // Progress towards the next fixed update in [0, 1), during Update.
    double interpolationAlpha = 0.0;
    // Set to end the game: the player quits, and the editor stops playing.
    bool quitRequested = false;
    // Set to replace the scene with a scene asset once the updates of the frame are done. The
    // physics starts over and the Start systems run for the new scene.
    asset::AssetId sceneToLoad;
    // Set to load a scene in the background: the assets it shows load while this scene goes on,
    // then it replaces this one as sceneToLoad does. Asking for another one gives up the first.
    asset::AssetId sceneToLoadInBackground;
    // The scene loading in the background, invalid when none, and how much of what it shows is
    // ready, from 0 to 1: what a loading screen shows.
    asset::AssetId loadingScene;
    float loadingProgress = 0.0f;
};

using SystemFunction = void (*)(SystemContext& context);

struct DEVEX_API SystemInfo
{
    std::string name;
    SystemPhase phase = SystemPhase::Update;
    // Systems of a phase run by increasing order, then in the order they were registered.
    int order = 0;
    SystemFunction function = nullptr;
};

// A method other players can call. It says false when the arguments are not those it takes.
struct DEVEX_API RpcInfo
{
    std::string name;
    net::RpcPolicy policy;
    std::function<bool(SystemContext& context, scene::Entity entity, std::span<const net::RpcValue> arguments)> function;
};

namespace detail {

// What a call carries for a value of the game: an entity goes as its identifier, the same everywhere.
template <typename T>
[[nodiscard]] net::RpcValue rpcArgument(SystemContext& context, const T& value)
{
    if constexpr (std::is_same_v<T, scene::Entity>)
    {
        return net::RpcValue(context.scene.uuid(value));
    }
    else
    {
        return net::toRpcValue(value);
    }
}

template <typename T>
[[nodiscard]] std::optional<T> rpcParameter(SystemContext& context, const net::RpcValue& value)
{
    if constexpr (std::is_same_v<T, scene::Entity>)
    {
        const std::optional<core::Uuid> uuid = net::fromRpcValue<core::Uuid>(value);
        return uuid ? std::optional<T>(context.scene.findEntity(*uuid)) : std::nullopt;
    }
    else
    {
        return net::fromRpcValue<T>(value);
    }
}

template <typename... Args, std::size_t... Indices>
[[nodiscard]] bool runRpcFunction(void (*function)(SystemContext&, scene::Entity, Args...), SystemContext& context, scene::Entity entity,
                                  std::span<const net::RpcValue> arguments, std::index_sequence<Indices...>)
{
    if (arguments.size() != sizeof...(Args))
    {
        return false;
    }
    [[maybe_unused]] std::tuple<std::optional<std::decay_t<Args>>...> values{rpcParameter<std::decay_t<Args>>(context, arguments[Indices])...};
    if (!(std::get<Indices>(values).has_value() && ...))
    {
        return false;
    }
    function(context, entity, std::move(*std::get<Indices>(values))...);
    return true;
}

} // namespace detail

// What a game module declares when it is loaded: its components and its systems.
class DEVEX_API GameRegistry
{
public:
    // Registers a reflected component type, so that scenes save it and the inspector edits it.
    template <typename T>
    void component()
    {
        scene::registerComponent<T>();
        m_components.push_back(reflection::typeInfo<T>().name);
    }

    void system(std::string name, SystemPhase phase, SystemFunction function, int order = 0);

    // A function other players can call by this name for an entity, with rpc or rpcId below, "Type.Method"
    // being what C# names its own. It takes the context, the entity, then what the call carries:
    // booleans, numbers, enums, strings, vectors, quaternions, entities and identifiers, bytes.
    //
    //     void hit(SystemContext& context, scene::Entity entity, int damage);
    //     game.rpc("Player.Hit", &hit);
    //     game.rpc("Player.Say", &say, {.anyPeer = true});
    //
    // A name belongs to one language of the game: declared in both, C++ runs it on the game that calls.
    template <typename... Args>
    void rpc(std::string name, void (*function)(SystemContext&, scene::Entity, Args...), net::RpcPolicy policy = {})
    {
        addRpc({.name = std::move(name), .policy = policy, .function = [function](SystemContext& context, scene::Entity entity, std::span<const net::RpcValue> arguments) {
                    return detail::runRpcFunction<Args...>(function, context, entity, arguments, std::index_sequence_for<Args...>{});
                }});
    }

    void addRpc(RpcInfo rpc);
    [[nodiscard]] std::span<const RpcInfo> rpcs() const noexcept;
    [[nodiscard]] const RpcInfo* findRpc(std::string_view name) const noexcept;

    // Runs a remote call of this game code on its entity, as called by a player.
    void runRpc(SystemContext& context, const net::RpcCall& call, net::PeerId sender) const;
    // Runs the remote calls that arrived for this game code; those of C# are for C#.
    void dispatchRpcs(SystemContext& context) const;

    // The names of the registered component types.
    [[nodiscard]] std::span<const std::string> components() const noexcept;
    // By phase, then order, then registration.
    [[nodiscard]] std::span<const SystemInfo> systems() const noexcept;

    // Runs the systems of a phase in order. A system that requests to quit does not stop the others.
    void run(SystemPhase phase, SystemContext& context) const;

private:
    std::vector<std::string> m_components;
    std::vector<SystemInfo> m_systems;
    std::vector<RpcInfo> m_rpcs;
};

// Calls a remote method of an entity for one player, or for everyone with net::everyone. Refused with a
// reason in the log when the policy of the method does not allow it. Out of a session, it only runs here,
// when it says callLocal.
DEVEX_API void sendRpc(SystemContext& context, net::PeerId to, scene::Entity target, std::string_view name, std::span<const net::RpcValue> arguments);

// As Godot's rpc_id and rpc: rpc(context, entity, "Player.Hit", 25).
template <typename... Args>
void rpcId(SystemContext& context, net::PeerId to, scene::Entity target, std::string_view name, const Args&... arguments)
{
    const std::array<net::RpcValue, sizeof...(Args)> values{detail::rpcArgument(context, arguments)...};
    sendRpc(context, to, target, name, values);
}

template <typename... Args>
void rpc(SystemContext& context, scene::Entity target, std::string_view name, const Args&... arguments)
{
    rpcId(context, net::everyone, target, name, arguments...);
}

} // namespace devex::runtime

#ifdef _WIN32
#define DEVEX_GAME_EXPORT __declspec(dllexport)
#else
#define DEVEX_GAME_EXPORT __attribute__((visibility("default")))
#endif

// Defines the entry point of a game module, which registers its components and systems:
//
//     DEVEX_GAME_MODULE(game)
//     {
//         game.component<Spinner>();
//         game.system("Spin", devex::runtime::SystemPhase::Update, &spin);
//     }
#define DEVEX_GAME_MODULE(registry)                                                                \
    static void devexRegisterGame(::devex::runtime::GameRegistry& registry);                       \
    extern "C" DEVEX_GAME_EXPORT std::uint32_t devexGameApiVersion()                              \
    {                                                                                              \
        return ::devex::runtime::gameApiVersion;                                                   \
    }                                                                                              \
    extern "C" DEVEX_GAME_EXPORT void devexRegisterGameModule(::devex::runtime::GameRegistry* game) \
    {                                                                                              \
        devexRegisterGame(*game);                                                                  \
    }                                                                                              \
    static void devexRegisterGame(::devex::runtime::GameRegistry& registry)
