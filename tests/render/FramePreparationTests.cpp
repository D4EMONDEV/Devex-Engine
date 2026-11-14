#include "render/FramePreparation.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace math = devex::math;
using devex::render::RenderWorld;

namespace {

// A camera that sees the plane of the sprites from far enough to take every sprite of the tests.
[[nodiscard]] devex::render::Frustum seeing()
{
    const math::Mat4 view = math::lookAt(math::Vec3{0.0f, 0.0f, 50.0f}, math::Vec3{0.0f}, math::Vec3{0.0f, 1.0f, 0.0f});
    return devex::render::frustumOf(math::perspective(math::radians(90.0f), 1.0f, 0.1f, 500.0f) * view);
}

[[nodiscard]] devex::render::SpriteResources resources()
{
    return {.textureSlot = [](devex::render::TextureHandle) { return devex::render::noParticleTexture; },
            .materialIndex = [](devex::render::MaterialHandle) { return 0U; },
            .canvasShader = [](devex::render::MaterialHandle) { return devex::render::ShaderHandle{}; },
            .sortDistance = [](math::Vec3 point) { return 50.0f - point.z; }};
}

[[nodiscard]] devex::render::RenderSprite sprite(std::int32_t layer, std::int32_t order, float x, float z = 0.0f)
{
    devex::render::RenderSprite made;
    made.transform = math::translate(math::Mat4{1.0f}, math::Vec3{x, 0.0f, z});
    made.size = math::Vec2{1.0f};
    made.pivot = math::Vec2{0.5f};
    made.layer = layer;
    made.order = order;
    return made;
}

} // namespace

TEST_CASE("Sprites are drawn by layer, then by order, then the farthest first", "[render][sprites]")
{
    RenderWorld world;
    world.sprites = {sprite(1, 0, 0.0f), sprite(0, 5, 1.0f), sprite(0, 0, 2.0f, 1.0f), sprite(0, 0, 3.0f, -1.0f), sprite(0, 0, 9000.0f)};
    devex::render::SpriteFrame frame;
    devex::render::gatherSprites(world, seeing(), resources(), frame);
    // The last one is out of sight.
    REQUIRE(frame.batches.size() == 4);
    CHECK(frame.batches[0].source == 3);
    CHECK(frame.batches[1].source == 2);
    CHECK(frame.batches[2].source == 1);
    CHECK(frame.batches[3].source == 0);
    REQUIRE(frame.sprites.size() == 4);
    for (std::size_t index = 0; index < frame.batches.size(); ++index)
    {
        CHECK(frame.batches[index].first == index);
        CHECK(frame.batches[index].count == 1);
    }
    CHECK(frame.outlined.size() == 4);
    CHECK(frame.shaders.size() == 4);
}

TEST_CASE("A tilemap is one batch, with the tiles the camera sees", "[render][sprites]")
{
    RenderWorld world;
    devex::render::RenderTilemap tilemap;
    tilemap.cellSize = math::Vec2{1.0f};
    tilemap.firstTile = 0;
    tilemap.tileCount = 3;
    world.tilemaps.push_back(tilemap);
    world.tiles.push_back({.cell = {0, 0}});
    world.tiles.push_back({.cell = {1, 0}});
    world.tiles.push_back({.cell = {5000, 0}});
    devex::render::SpriteFrame frame;
    devex::render::gatherSprites(world, seeing(), resources(), frame);
    REQUIRE(frame.batches.size() == 1);
    CHECK(frame.batches[0].tilemap);
    CHECK(frame.batches[0].count == 2);
    CHECK(frame.sprites.size() == 2);
}

TEST_CASE("The interface of a frame counts each surface from its own vertices", "[render][ui]")
{
    RenderWorld world;
    world.uiVertices.resize(3);
    world.uiIndices = {0, 1, 2};
    devex::render::UiSurface surface;
    surface.vertices.resize(4);
    surface.indices = {0, 1, 2, 2, 3, 0};
    world.uiSurfaces.push_back(surface);
    world.toolsVertices.resize(3);
    world.toolsIndices = {0, 1, 2};

    devex::render::PackedUi packed;
    devex::render::packUi(world, true, packed);
    CHECK(packed.vertices.size() == 10);
    REQUIRE(packed.indices.size() == 12);
    REQUIRE(packed.surfaceStarts.size() == 1);
    CHECK(packed.surfaceStarts[0] == 3);
    CHECK(packed.indices[3] == 3);
    CHECK(packed.indices[7] == 6);
    CHECK(packed.toolsStart == 9);
    CHECK(packed.indices[9] == 7);

    // Without the tools, the interface of the scene alone.
    devex::render::packUi(world, false, packed);
    CHECK(packed.vertices.size() == 3);
    CHECK(packed.surfaceStarts.empty());
    CHECK(packed.toolsStart == 3);
}

TEST_CASE("The air keeps the camera inside it, a meter above the ground at least", "[render][atmosphere]")
{
    RenderWorld world;
    world.atmosphere.planetRadius = 6360.0f;
    world.atmosphere.height = 100.0f;
    world.sun.direction = math::Vec3{0.0f, -2.0f, 0.0f};
    CHECK(devex::render::packAtmosphere(world, -50.0f).altitude == 0.001f);
    const devex::render::PackedAtmosphere packed = devex::render::packAtmosphere(world, 2000.0f);
    CHECK(packed.altitude == 2.0f);
    CHECK(packed.gpu.cameraRadius == 6362.0f);
    // The shaders read the way to the sun, of length 1.
    CHECK(packed.gpu.sunDirection == math::Vec3{0.0f, 1.0f, 0.0f});
}
