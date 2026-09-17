#pragma once

#include <devex/core/Error.hpp>
#include <devex/platform/SharedLibrary.hpp>
#include <devex/runtime/Game.hpp>
#include <devex/scene/Scene.hpp>

#include <filesystem>
#include <memory>

namespace devex::runtime {

// A loaded game module. The library is loaded from a copy, so that the original can be rebuilt while
// the module runs, and a new build loaded next to it.
//
// Unloading a module invalidates everything its code defines: before destroying it, release every
// scene, which preserves the components it defines as text (scene::PreservedComponents) and destroys
// the component pools its code created. Once a module is loaded again, scene::restorePreservedComponents
// brings the components back.
class GameModule
{
public:
    // Copies the library into copyDirectory under a new name, loads the copy, checks its API version,
    // then registers its components and systems. Copies left by earlier runs are removed. An empty
    // copyDirectory loads the library in place, as exported games do.
    [[nodiscard]] static core::Result<std::unique_ptr<GameModule>> load(const std::filesystem::path& library,
                                                                        const std::filesystem::path& copyDirectory);

    // Unregisters the module's component types and unloads it.
    ~GameModule();

    GameModule(const GameModule&) = delete;
    GameModule& operator=(const GameModule&) = delete;

    // The library the module was copied from.
    [[nodiscard]] const std::filesystem::path& source() const noexcept;
    [[nodiscard]] const GameRegistry& registry() const noexcept;

    // Preserves the components the module defines in the scene, and destroys the component pools its
    // code created. Components of other types in those pools, such as a Transform pool the module
    // created, are recreated by the engine right away. Returns the number of components preserved.
    std::size_t release(scene::Scene& scene) const;

private:
    GameModule(std::filesystem::path source, platform::SharedLibrary library) noexcept;
    // Loads file, the library itself or its copy, as the module of library.
    [[nodiscard]] static core::Result<std::unique_ptr<GameModule>> loadLibrary(const std::filesystem::path& library,
                                                                               const std::filesystem::path& file);

    std::filesystem::path m_source;
    // Unloaded last, once nothing refers to its code anymore.
    platform::SharedLibrary m_library;
    GameRegistry m_registry;
};

} // namespace devex::runtime
