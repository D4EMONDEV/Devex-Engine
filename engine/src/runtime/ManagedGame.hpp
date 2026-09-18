#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/AssetSource.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/physics/PhysicsWorld.hpp>
#include <devex/platform/Input.hpp>
#include <devex/platform/Window.hpp>
#include <devex/runtime/Game.hpp>
#include <devex/scene/Scene.hpp>

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace devex::runtime::detail {

// The game code written in C#, running on .NET hosted inside the engine.
//
// The engine starts one runtime for the process (Devex.Managed.dll next to the executable), then
// loads the assembly a project builds from its C# files. The components the assembly defines become
// component types described at runtime: the engine owns their memory, so scenes, the inspector,
// prefabs and undo treat them like the components written in C++. During each phase, the runtime
// copies the values into the C# objects, runs the game's code, and copies them back.
class ManagedGame
{
public:
    // What C# code reaches during one phase, and what it asks the engine to do afterwards.
    struct Frame
    {
        scene::Scene* scene = nullptr;
        core::Duration delta{0.0};
        const platform::Input* input = nullptr;
        platform::Window* window = nullptr;
        // Null without physics.
        physics::PhysicsWorld* physics = nullptr;
        // Where assets are found by path; null without a project or package.
        const asset::AssetSource* assets = nullptr;
        // Set by the game.
        bool quitRequested = false;
        asset::AssetId sceneToLoad;
    };

    // Starts .NET and the runtime assembly. Fails when .NET or the assembly is missing, and the
    // engine then runs without C#.
    [[nodiscard]] static core::Result<std::unique_ptr<ManagedGame>> create(const std::filesystem::path& managedDirectory);

    ~ManagedGame();

    ManagedGame(const ManagedGame&) = delete;
    ManagedGame& operator=(const ManagedGame&) = delete;

    // Loads the assembly of a game and registers its component types. The previous assembly, if
    // any, is unloaded first.
    [[nodiscard]] core::Result<void> loadAssembly(const std::filesystem::path& assembly);
    // Unregisters the component types and unloads the assembly. The runtime keeps the state of the
    // C# objects of the components, which the next assembly gets back.
    void unloadAssembly();
    [[nodiscard]] bool hasAssembly() const noexcept;

    // The names of the component types the assembly defines.
    [[nodiscard]] std::span<const std::string> componentTypes() const noexcept;

    // Runs the C# components and systems of the frame's scene for a phase.
    void runPhase(Frame& frame, SystemPhase phase);

    // Keeps the components of the assembly in the scene as text and destroys their pools, before
    // the assembly is unloaded. Returns the number of components preserved.
    std::size_t release(scene::Scene& scene) const;

    // Whether a .NET debugger is attached to the process.
    [[nodiscard]] bool isDebuggerAttached() const;

private:
    class Impl;

    explicit ManagedGame(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace devex::runtime::detail
