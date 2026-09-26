#pragma once

#include <devex/animation/AnimationWorld.hpp>
#include <devex/audio/AudioWorld.hpp>
#include <devex/core/Time.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Window.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/runtime/InputActions.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/ui/UiWorld.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Game code: components that hold the state of a game and systems that update it, compiled into a
// game module, a shared library that the editor and the player load and reload while they run.
// All the state lives in the scene, so that playing a copy of a scene or reloading the module
// loses nothing.
namespace devex::runtime {

// Changes whenever game modules must be rebuilt to load: modules built for another version are
// refused. 9: ComponentType gained findMutable, which modules fill in when they register a type.
// 10: SystemContext gained the loading of scenes in the background.
// 11: SystemContext gained the input actions of the project.
inline constexpr std::uint32_t gameApiVersion = 11;

enum class SystemPhase : std::uint8_t
{
    // Once when the game starts: when Play starts in the editor, or at launch in the player.
    Start,
    // At the fixed update rate, for simulation, before each physics step.
    FixedUpdate,
    // Once per frame, for input, cameras and anything that follows the display.
    Update,
};

// What a system works on during one call.
struct SystemContext
{
    scene::Scene& scene;
    // The keys, buttons and sticks themselves. Actions are what games read rather than keys.
    const platform::Input& input;
    platform::Window& window;
    AssetManager& assets;
    // The actions of the project ("Jump", "Move"), their contexts and the bindings of the player.
    // Null when the application runs without them.
    InputActions* actions = nullptr;
    // The simulation of the scene: queries, forces, and the contacts of the steps of this frame,
    // which Update systems see once. Null when the application runs without physics.
    physics::PhysicsWorld* physics = nullptr;
    // The sounds of the scene: its AudioSource components, one-shot sounds and the volumes of the
    // groups. Null when the application runs without audio.
    audio::AudioWorld* audio = nullptr;
    // The animations of the scene: the clips its Animator components play. Null when the
    // application runs without animation.
    animation::AnimationWorld* animation = nullptr;
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

struct SystemInfo
{
    std::string name;
    SystemPhase phase = SystemPhase::Update;
    // Systems of a phase run by increasing order, then in the order they were registered.
    int order = 0;
    SystemFunction function = nullptr;
};

// What a game module declares when it is loaded: its components and its systems.
class GameRegistry
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

    // The names of the registered component types.
    [[nodiscard]] std::span<const std::string> components() const noexcept;
    // By phase, then order, then registration.
    [[nodiscard]] std::span<const SystemInfo> systems() const noexcept;

    // Runs the systems of a phase in order. A system that requests to quit does not stop the others.
    void run(SystemPhase phase, SystemContext& context) const;

private:
    std::vector<std::string> m_components;
    std::vector<SystemInfo> m_systems;
};

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
