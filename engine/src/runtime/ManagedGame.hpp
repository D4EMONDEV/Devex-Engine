#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
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
// prefabs and undo treat them like the components written in C++. Before a behaviour runs, the
// runtime copies the values into its C# object, and copies them back afterwards.
class ManagedGame
{
public:
    // What the C# API reaches beyond the scene it is given.
    struct Services
    {
        const platform::Input* input = nullptr;
        platform::Window* window = nullptr;
        // Set when the game asks to quit.
        bool* quitRequested = nullptr;
    };

    // Starts .NET and the runtime assembly. Fails when .NET or the assembly is missing, and the
    // engine then runs without C#.
    [[nodiscard]] static core::Result<std::unique_ptr<ManagedGame>> create(
        const std::filesystem::path& managedDirectory, Services services);

    ~ManagedGame();

    ManagedGame(const ManagedGame&) = delete;
    ManagedGame& operator=(const ManagedGame&) = delete;

    // Loads the assembly of a game and registers its component types. The previous assembly, if
    // any, must have been unloaded first.
    [[nodiscard]] core::Result<void> loadAssembly(const std::filesystem::path& assembly);
    // Unregisters the component types and unloads the assembly.
    void unloadAssembly();
    [[nodiscard]] bool hasAssembly() const noexcept;

    // The names of the component types the assembly defines.
    [[nodiscard]] std::span<const std::string> componentTypes() const noexcept;

    // Runs Start, FixedUpdate or Update for every C# component of the scene.
    void runPhase(scene::Scene& scene, SystemPhase phase, core::Duration delta);

    // Keeps the components of the assembly in the scene as text and destroys their pools, before
    // the assembly is unloaded. Returns the number of components preserved.
    std::size_t release(scene::Scene& scene) const;

private:
    class Impl;

    explicit ManagedGame(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> m_impl;
};

} // namespace devex::runtime::detail
