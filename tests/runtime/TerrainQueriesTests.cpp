#include <devex/asset/Artifact.hpp>
#include <devex/asset/AssetSource.hpp>
#include <devex/runtime/TerrainQueries.hpp>
#include <devex/scene/Components.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <string>
#include <vector>

namespace {

// One terrain, without a project on disk.
class OneTerrain final : public devex::asset::AssetSource
{
public:
    explicit OneTerrain(const devex::asset::TerrainMaps& maps)
        : m_info{.id = id, .type = devex::asset::AssetType::Terrain, .name = "ground", .source = id}
        , m_bytes(devex::asset::encodeTerrain(maps))
    {
    }

    [[nodiscard]] const devex::asset::Project& project() const noexcept override
    {
        return m_project;
    }

    [[nodiscard]] const devex::asset::AssetInfo* find(devex::asset::AssetId asset) const override
    {
        return asset == id ? &m_info : nullptr;
    }

    [[nodiscard]] std::vector<devex::asset::AssetInfo> assets(std::optional<devex::asset::AssetType> /*type*/) const override
    {
        return {m_info};
    }

    [[nodiscard]] std::optional<devex::asset::AssetId> findByPath(std::string_view /*resourcePath*/) const override
    {
        return std::nullopt;
    }

    [[nodiscard]] devex::core::Result<std::vector<std::byte>> loadArtifact(devex::asset::AssetId asset) const override
    {
        if (asset != id)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "no such asset");
        }
        return m_bytes;
    }

    [[nodiscard]] devex::core::Result<std::string> sceneText(devex::asset::AssetId /*asset*/) const override
    {
        return devex::core::makeError(devex::core::ErrorCode::NotFound, "no scenes here");
    }

    const devex::asset::AssetId id = devex::asset::AssetId::generate();

private:
    devex::asset::Project m_project;
    devex::asset::AssetInfo m_info;
    std::vector<std::byte> m_bytes;
};

} // namespace

TEST_CASE("Game code reads the ground of terrains in the world", "[runtime][terrain]")
{
    // 32 m on 33 samples, 2 m up from its entity, a meter higher every two along X, with a hole.
    devex::asset::TerrainMaps maps = devex::asset::makeFlatTerrain(33);
    for (std::uint32_t z = 0; z < 33; ++z)
    {
        for (std::uint32_t x = 0; x < 33; ++x)
        {
            maps.heights[std::size_t{z} * 33 + x] = static_cast<float>(x) * 0.5f;
        }
    }
    maps.holes[std::size_t{4} * 33 + 4] = 255;
    OneTerrain source(maps);
    devex::runtime::AssetManager assets(nullptr, &source);
    devex::scene::Scene scene;
    const devex::scene::Entity ground = scene.createEntity("Ground");
    scene.add<devex::scene::Transform>(ground, devex::scene::Transform{.position = {-16.0f, 2.0f, -16.0f}});
    scene.add<devex::scene::Terrain>(ground, devex::scene::Terrain{.data = source.id, .size = 32.0f});
    scene.updateTransforms();

    const std::optional<float> height = devex::runtime::terrainHeightAt(scene, assets, ground, {4.0f, 100.0f, 0.0f});
    REQUIRE(height.has_value());
    CHECK(*height == Catch::Approx(12.0f));
    CHECK(devex::runtime::terrainHeightAt(scene, assets, {4.0f, -50.0f, 0.0f}) == height);
    const std::optional<devex::math::Vec3> normal = devex::runtime::terrainNormalAt(scene, assets, ground, {4.0f, 0.0f, 0.0f});
    REQUIRE(normal.has_value());
    CHECK(normal->y == Catch::Approx(1.0f / std::sqrt(1.25f)).margin(1e-3f));
    // Outside its square, and over its hole, there is no ground.
    CHECK_FALSE(devex::runtime::terrainHeightAt(scene, assets, {40.0f, 0.0f, 0.0f}).has_value());
    CHECK(devex::runtime::isTerrainHoleAt(scene, assets, ground, {-12.0f, 0.0f, -12.0f}));
    CHECK_FALSE(devex::runtime::terrainHeightAt(scene, assets, ground, {-12.0f, 0.0f, -12.0f}).has_value());

    const std::optional<devex::runtime::TerrainHit> hit =
        devex::runtime::raycastTerrain(scene, assets, {4.0f, 50.0f, 0.0f}, {0.0f, -2.0f, 0.0f}, 100.0f);
    REQUIRE(hit.has_value());
    CHECK(hit->distance == Catch::Approx(38.0f).margin(0.01f));
    CHECK(hit->point.y == Catch::Approx(12.0f).margin(0.01f));
    CHECK(hit->normal.y == Catch::Approx(normal->y).margin(1e-3f));
    CHECK_FALSE(devex::runtime::raycastTerrain(scene, assets, {4.0f, 50.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 100.0f).has_value());
}
