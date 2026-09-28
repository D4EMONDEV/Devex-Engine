#include "tools/CreationCatalog.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/UiComponents.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>

using devex::scene::Entity;
using devex::scene::Scene;
using devex::tools::detail::CreationCategory;
using devex::tools::detail::CreationEntry;

namespace {

[[nodiscard]] const CreationEntry& entryOf(const std::vector<CreationEntry>& catalog, std::string_view key)
{
    const auto found = std::ranges::find(catalog, key, &CreationEntry::key);
    REQUIRE(found != catalog.end());
    return *found;
}

} // namespace

TEST_CASE("The creation catalog offers every component and the presets, once each", "[tools][creation]")
{
    const devex::scene::ComponentRegistry& registry = devex::scene::componentRegistry();
    const std::vector<CreationEntry> catalog = devex::tools::detail::creationCatalog(registry);

    std::set<std::string> keys;
    for (const CreationEntry& entry : catalog)
    {
        CHECK(keys.insert(entry.key).second);
        CHECK_FALSE(entry.name.empty());
        CHECK_FALSE(entry.description.empty());
    }
    // Every registered component, as each type of node is in Godot.
    for (const devex::scene::ComponentType& type : registry.types())
    {
        CHECK(keys.contains("component:" + std::string(type.name())));
    }
    CHECK(keys.contains("preset:Cube"));
    CHECK(keys.contains("preset:Rigid box"));

    // A component entry receives what it needs: a place in the world, or a rectangle of an
    // interface, and the component itself last.
    const CreationEntry& light = entryOf(catalog, "component:PointLight");
    CHECK(light.category == CreationCategory::ThreeD);
    CHECK(light.components == std::vector<std::string>{"Transform", "PointLight"});
    const CreationEntry& button = entryOf(catalog, "component:UiButton");
    CHECK(button.category == CreationCategory::Interface);
    CHECK(button.components == std::vector<std::string>{"UiRect", "UiImage", "UiButton"});
    CHECK(entryOf(catalog, "component:Canvas").components == std::vector<std::string>{"Canvas"});
}

TEST_CASE("A search ranks the names that start with it first and needs every word", "[tools][creation]")
{
    const std::vector<CreationEntry> catalog = devex::tools::detail::creationCatalog(devex::scene::componentRegistry());
    const auto score = [&](std::string_view key, std::string_view search) {
        return devex::tools::detail::matchScore(entryOf(catalog, key), search);
    };
    CHECK(score("component:PointLight", "point") > score("component:NavMeshAgent", "point"));
    CHECK(score("component:PointLight", "point") > score("component:DirectionalLight", "light"));
    // A word of the description is enough, but ranks after the names.
    CHECK(score("component:Environment", "light") > 0);
    CHECK(score("component:SpotLight", "light") > score("component:Environment", "light"));
    // Every word must be found.
    CHECK(score("component:RigidBody2D", "rigid 2d") > 0);
    CHECK(score("component:RigidBody", "rigid 2d") == 0);
    CHECK(score("component:RigidBody", "") > 0);
}

TEST_CASE("An entry builds its entity with what it lists", "[tools][creation]")
{
    const devex::scene::ComponentRegistry& registry = devex::scene::componentRegistry();
    const std::vector<CreationEntry> catalog = devex::tools::detail::creationCatalog(registry);
    Scene scene;
    const auto made = [&](std::string_view key) {
        const Entity entity = scene.createEntity("Made");
        scene.add<devex::scene::Transform>(entity);
        devex::tools::detail::buildEntry(entryOf(catalog, key), registry, scene, entity);
        return entity;
    };

    // An element of an interface stands in its rectangle, not in space.
    const Entity button = made("component:UiButton");
    CHECK_FALSE(scene.has<devex::scene::Transform>(button));
    CHECK(scene.has<devex::scene::UiRect>(button));
    CHECK(scene.has<devex::scene::UiImage>(button));
    CHECK(scene.has<devex::scene::UiButton>(button));

    // A sun comes down at an angle; a camera does not take over the game's.
    const Entity sun = made("component:DirectionalLight");
    CHECK(scene.has<devex::scene::DirectionalLight>(sun));
    CHECK(scene.get<devex::scene::Transform>(sun).rotation.w < 0.99f);
    CHECK_FALSE(scene.get<devex::scene::Camera>(made("component:Camera")).primary);

    const Entity cube = made("preset:Cube");
    CHECK(scene.get<devex::scene::MeshRenderer>(cube).mesh == devex::asset::builtin::cubeMesh);
}

TEST_CASE("The window remembers the favorites and the recent entries of a project", "[tools][creation]")
{
    using devex::tools::detail::CreationMemory;
    CreationMemory memory;
    devex::tools::detail::toggleFavorite(memory, "component:PointLight");
    devex::tools::detail::toggleFavorite(memory, "preset:Cube");
    devex::tools::detail::toggleFavorite(memory, "component:PointLight");
    CHECK(memory.favorites == std::vector<std::string>{"preset:Cube"});

    for (int index = 0; index < 12; ++index)
    {
        devex::tools::detail::rememberCreation(memory, "entry " + std::to_string(index));
    }
    devex::tools::detail::rememberCreation(memory, "entry 5");
    REQUIRE(memory.recent.size() == devex::tools::detail::maxRecentCreations);
    CHECK(memory.recent.front() == "entry 5");
    CHECK(memory.recent[1] == "entry 11");
    CHECK(std::ranges::count(memory.recent, "entry 5") == 1);

    const std::filesystem::path file =
        std::filesystem::temp_directory_path() / ("devex-creation-" + devex::core::Uuid::generate().toString() + ".dvx");
    REQUIRE(devex::tools::detail::saveCreationMemory(memory, file).has_value());
    const CreationMemory loaded = devex::tools::detail::loadCreationMemory(file);
    CHECK(loaded.favorites == memory.favorites);
    CHECK(loaded.recent == memory.recent);
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    // A project without the file starts empty.
    CHECK(devex::tools::detail::loadCreationMemory(file).favorites.empty());
}
