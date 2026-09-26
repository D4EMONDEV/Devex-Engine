#include <devex/asset/AssetId.hpp>
#include <devex/scene/AssetReferences.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using devex::asset::AssetId;
using devex::scene::Entity;
using devex::scene::Scene;

namespace test {

// Plays one of its sounds.
struct Jukebox
{
    std::vector<AssetId> songs;
};
DEVEX_DECLARE_REFLECTION(Jukebox);

DEVEX_REFLECT(Jukebox)
{
    type.field("songs", &Jukebox::songs);
}

} // namespace test

TEST_CASE("The assets a scene names are found once each, in lists as well", "[scene][assets]")
{
    devex::scene::registerComponent<devex::scene::MeshRenderer>();
    devex::scene::registerComponent<test::Jukebox>();
    const AssetId cube = AssetId::generate();
    const AssetId wood = AssetId::generate();
    const AssetId song = AssetId::generate();
    const AssetId other = AssetId::generate();

    Scene scene;
    const Entity crate = scene.createEntity("Crate");
    scene.add<devex::scene::MeshRenderer>(crate, devex::scene::MeshRenderer{.mesh = cube, .material = wood});
    // Under another entity, with the same mesh: named once.
    const Entity shelf = scene.createEntity("Shelf");
    const Entity box = scene.createEntity("Box");
    REQUIRE(scene.setParent(box, shelf));
    scene.add<devex::scene::MeshRenderer>(box, devex::scene::MeshRenderer{.mesh = cube});
    scene.add<test::Jukebox>(shelf, test::Jukebox{.songs = {song, AssetId{}, other, song}});

    const std::vector<AssetId> assets = devex::scene::referencedAssets(scene);
    CHECK(assets.size() == 4);
    for (const AssetId id : {cube, wood, song, other})
    {
        CHECK(std::ranges::find(assets, id) != assets.end());
    }
    CHECK(devex::scene::referencedAssets(Scene{}).empty());
}
