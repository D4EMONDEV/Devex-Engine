#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Scene.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// What the Create Entity and Add Component window offers: every component, engine and game alike,
// as the types of nodes Godot offers, and the presets that put several together. Pure, so that the
// tests read it without a window.
namespace devex::tools::detail {

enum class CreationCategory : std::uint8_t
{
    General,
    ThreeD,
    TwoD,
    Interface,
    Physics,
    Physics2D,
    Audio,
    Animation,
    Effects,
    Navigation,
    GameCode,
    Count,
};

[[nodiscard]] DEVEX_API std::string_view categoryName(CreationCategory category) noexcept;

struct DEVEX_API CreationEntry
{
    // What favorites and recents remember: "component:PointLight", "preset:Cube".
    std::string key;
    // As the window shows it and as the new entity is named: "Point light".
    std::string name;
    // The component the entry stands for; empty for a preset.
    std::string component;
    CreationCategory category = CreationCategory::General;
    std::string description;
    // The components a new entity receives, in order, for the window to show.
    std::vector<std::string> components;
    // Sets the values that differ from those of the components, such as the angle of a light.
    std::function<void(scene::Scene& scene, scene::Entity entity)> adjust;

    [[nodiscard]] bool isPreset() const noexcept
    {
        return component.empty();
    }
};

// Every entry, the presets first, then the components of the engine by category, then those of the
// game in the registry.
[[nodiscard]] DEVEX_API std::vector<CreationEntry> creationCatalog(const scene::ComponentRegistry& registry);

// How well an entry answers a search, 0 when it does not: every word must be found, in its name
// first, then in its component, its category and its description.
[[nodiscard]] DEVEX_API int matchScore(const CreationEntry& entry, std::string_view search);

// Gives an entity of a scratch scene what the entry makes; the entity has a Transform, which the
// entries that do not stand in space remove.
DEVEX_API void buildEntry(const CreationEntry& entry, const scene::ComponentRegistry& registry, scene::Scene& scene,
                          scene::Entity entity);

// What a project remembers of the window: its favorites, and what was created last, newest first.
struct DEVEX_API CreationMemory
{
    std::vector<std::string> favorites;
    std::vector<std::string> recent;
};

inline constexpr std::size_t maxRecentCreations = 10;

[[nodiscard]] DEVEX_API CreationMemory loadCreationMemory(const std::filesystem::path& file);
[[nodiscard]] DEVEX_API core::Result<void> saveCreationMemory(const CreationMemory& memory, const std::filesystem::path& file);
// Puts an entry at the head of the recent ones.
DEVEX_API void rememberCreation(CreationMemory& memory, std::string_view key);
DEVEX_API void toggleFavorite(CreationMemory& memory, std::string_view key);

} // namespace devex::tools::detail
