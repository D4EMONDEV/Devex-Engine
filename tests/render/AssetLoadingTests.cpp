#include <devex/asset/Artifact.hpp>
#include <devex/asset/AssetSource.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/runtime/AssetManager.hpp>
#include <devex/runtime/SceneExtraction.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SpriteComponents.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using devex::asset::AssetId;
using devex::asset::AssetInfo;
using devex::asset::AssetType;

namespace {

// Artifacts in memory, which a test may change while workers read them.
class MemorySource final : public devex::asset::AssetSource
{
public:
    void set(AssetId id, AssetType type, std::vector<std::byte> bytes)
    {
        const std::scoped_lock lock(m_mutex);
        m_infos.insert_or_assign(id, AssetInfo{.id = id, .type = type, .name = std::format("{}", id.uuid), .source = id});
        m_bytes.insert_or_assign(id, std::move(bytes));
    }

    void setBroken(AssetId id, AssetType type)
    {
        const std::scoped_lock lock(m_mutex);
        m_infos.insert_or_assign(id, AssetInfo{.id = id, .type = type, .name = "broken", .source = id});
        m_bytes.erase(id);
    }

    [[nodiscard]] const devex::asset::Project& project() const noexcept override
    {
        return m_project;
    }

    [[nodiscard]] const AssetInfo* find(AssetId id) const override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_infos.find(id);
        return found != m_infos.end() ? &found->second : nullptr;
    }

    [[nodiscard]] std::vector<AssetInfo> assets(std::optional<AssetType> /*type*/) const override
    {
        return {};
    }

    [[nodiscard]] std::optional<AssetId> findByPath(std::string_view /*resourcePath*/) const override
    {
        return std::nullopt;
    }

    [[nodiscard]] devex::core::Result<std::vector<std::byte>> loadArtifact(AssetId id) const override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_bytes.find(id);
        if (found == m_bytes.end())
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "no artifact for {}", id.uuid);
        }
        return found->second;
    }

    [[nodiscard]] devex::core::Result<std::string> sceneText(AssetId id) const override
    {
        return devex::core::makeError(devex::core::ErrorCode::NotFound, "no scene {}", id.uuid);
    }

private:
    mutable std::mutex m_mutex;
    devex::asset::Project m_project;
    std::unordered_map<AssetId, AssetInfo> m_infos;
    std::unordered_map<AssetId, std::vector<std::byte>> m_bytes;
};

[[nodiscard]] devex::asset::TextureData smallTexture()
{
    devex::asset::TextureData texture{.format = devex::asset::TextureFormat::Rgba8Srgb};
    texture.mips.push_back({.width = 4, .height = 4, .bytes = std::vector<std::byte>(4 * 4 * 4, std::byte{180})});
    return texture;
}

// Renders one frame drawing the mesh, which copies what waits to the GPU.
void renderFrame(devex::render::Renderer& renderer, devex::render::MeshHandle mesh)
{
    devex::render::RenderWorld& world = renderer.beginFrame();
    world.meshes.push_back({.mesh = mesh});
    const devex::core::Result<void> presented = renderer.endFrame();
    REQUIRE(presented.has_value());
}

} // namespace

TEST_CASE("Meshes and textures load on the workers, then reach the GPU", "[runtime][assets][gpu]")
{
    std::vector<std::string> errors;
    const devex::core::LogSinkId sink = devex::core::addLogSink([&errors](const devex::core::LogRecord& record) {
        if (record.level >= devex::core::LogLevel::Error)
        {
            errors.emplace_back(record.message);
        }
    });
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer.has_value());

        const AssetId mesh = AssetId::generate();
        const AssetId texture = AssetId::generate();
        const AssetId material = AssetId::generate();
        const AssetId other = AssetId::generate();
        MemorySource source;
        source.set(mesh, AssetType::Mesh, devex::asset::encodeMesh(devex::asset::makeCube()));
        source.set(texture, AssetType::Texture, devex::asset::encodeTexture(smallTexture()));
        source.set(other, AssetType::Texture, devex::asset::encodeTexture(smallTexture()));
        source.set(material, AssetType::Material, devex::asset::encodeMaterial({.baseColorTexture = texture}));

        devex::core::JobSystem jobs(2);
        devex::runtime::AssetManager assets(&*renderer, &source, &jobs);

        // Asking starts the load and answers nothing yet.
        CHECK(assets.mesh(mesh) == nullptr);
        CHECK(assets.pendingLoads() == 1);
        CHECK_FALSE(assets.isReady(mesh));
        // A material is there at once; its texture follows.
        CHECK(assets.material(material).isValid());
        CHECK_FALSE(assets.isReady(material));
        CHECK(assets.pendingLoads() == 2);

        assets.waitForLoads();
        CHECK(assets.pendingLoads() == 0);
        const devex::runtime::LoadedMesh* const loaded = assets.mesh(mesh);
        REQUIRE(loaded != nullptr);
        CHECK(assets.texture(texture).isValid());
        // Handed to the renderer, not yet copied.
        CHECK_FALSE(assets.isReady(mesh));
        renderFrame(*renderer, loaded->handle);
        CHECK(assets.isReady(mesh));
        CHECK(assets.isReady(texture));
        CHECK(assets.isReady(material));

        // A reimported mesh: the previous version stays until the new one is loaded.
        const devex::render::MeshHandle before = loaded->handle;
        source.set(mesh, AssetType::Mesh, devex::asset::encodeMesh(devex::asset::makeUvSphere()));
        const std::array events{devex::asset::AssetEvent{.id = mesh, .type = AssetType::Mesh}};
        assets.handleEvents(events);
        REQUIRE(assets.mesh(mesh) != nullptr);
        CHECK(assets.mesh(mesh)->handle == before);
        assets.waitForLoads();
        REQUIRE(assets.mesh(mesh) != nullptr);
        CHECK(assets.mesh(mesh)->handle != before);
        renderFrame(*renderer, assets.mesh(mesh)->handle);

        // A source that goes away while its assets load leaves nothing behind.
        assets.preload(other);
        CHECK(assets.pendingLoads() == 1);
        assets.setSource(nullptr);
        CHECK(assets.pendingLoads() == 0);
        assets.finishLoads();
        CHECK_FALSE(assets.texture(other).isValid());
        renderFrame(*renderer, devex::render::MeshHandle{});
    }
    devex::core::removeLogSink(sink);
    for (const std::string& error : errors)
    {
        UNSCOPED_INFO(error);
    }
    CHECK(errors.empty());
}

TEST_CASE("An asset that fails to load is ready at once and is not asked for again", "[runtime][assets][gpu]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer.has_value());

    const AssetId broken = AssetId::generate();
    const AssetId mesh = AssetId::generate();
    MemorySource source;
    source.setBroken(broken, AssetType::Mesh);
    source.set(mesh, AssetType::Mesh, devex::asset::encodeMesh(devex::asset::makeCube()));

    devex::core::JobSystem jobs(1);
    devex::runtime::AssetManager assets(&*renderer, &source, &jobs);
    CHECK(assets.mesh(broken) == nullptr);
    assets.waitForLoads();
    CHECK(assets.mesh(broken) == nullptr);
    CHECK(assets.pendingLoads() == 0);
    // Waiting for it would wait forever.
    CHECK(assets.isReady(broken));

    // Without a job system, everything loads when it is asked for.
    devex::runtime::AssetManager synchronous(&*renderer, &source);
    CHECK(synchronous.mesh(mesh) != nullptr);
    CHECK(synchronous.pendingLoads() == 0);
}

TEST_CASE("Extraction draws the sprites, with the frames of their animators and their layers", "[runtime][assets][gpu]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {});
    REQUIRE(renderer.has_value());

    // A texture of 32 by 16 pixels cut into two sprites, and the frames of a run.
    MemorySource source;
    const AssetId textureId = AssetId::generate();
    devex::asset::TextureData texture{.format = devex::asset::TextureFormat::Rgba8Srgb};
    texture.mips.push_back({.width = 32, .height = 16, .bytes = std::vector<std::byte>(32 * 16 * 4, std::byte{255})});
    source.set(textureId, AssetType::Texture, devex::asset::encodeTexture(texture));
    const devex::asset::SpriteData first{.texture = textureId, .width = 16, .height = 16, .textureWidth = 32,
                                         .textureHeight = 16, .pixelsPerUnit = 16.0f, .pivot = {0.5f, 0.0f}};
    devex::asset::SpriteData second = first;
    second.x = 16;
    second.border = {4.0f, 4.0f, 4.0f, 4.0f};
    const AssetId firstId = AssetId::generate();
    const AssetId secondId = AssetId::generate();
    source.set(firstId, AssetType::Sprite, devex::asset::encodeSprite(first));
    source.set(secondId, AssetType::Sprite, devex::asset::encodeSprite(second));
    const AssetId framesId = AssetId::generate();
    source.set(framesId, AssetType::SpriteFrames,
               devex::asset::encodeSpriteFrames({.animations = {{.name = "run", .frames = {firstId, secondId}}}}));
    devex::runtime::AssetManager assets(&*renderer, &source);

    devex::scene::Scene scene;
    const auto camera = scene.createEntity("Camera");
    scene.add<devex::scene::Transform>(camera, devex::scene::Transform{.position = {0.0f, 0.0f, 10.0f}});
    scene.add<devex::scene::Camera>(camera, devex::scene::Camera{.projection = devex::scene::Projection::Orthographic,
                                                                 .orthographicSize = 3.0f,
                                                                 .farPlane = 50.0f});
    // A hero on its second frame, in front; a platform sliced behind, twice as bright.
    const auto hero = scene.createEntity("Hero");
    scene.add<devex::scene::Transform>(hero, devex::scene::Transform{.position = {1.0f, 0.0f, 0.0f}});
    scene.add<devex::scene::SpriteRenderer>(hero, devex::scene::SpriteRenderer{.sprite = firstId,
                                                                               .flipX = true,
                                                                               .sortingLayer = "Front",
                                                                               .order = 3});
    scene.add<devex::scene::SpriteAnimator>(hero, devex::scene::SpriteAnimator{.frames = framesId, .animation = "run",
                                                                               .frame = 1});
    const auto platformEntity = scene.createEntity("Platform");
    scene.add<devex::scene::Transform>(platformEntity);
    scene.add<devex::scene::SpriteRenderer>(platformEntity,
                                            devex::scene::SpriteRenderer{.sprite = secondId,
                                                                         .color = {0.5f, 0.25f, 1.0f, 0.5f},
                                                                         .intensity = 2.0f,
                                                                         .drawMode = devex::scene::SpriteDrawMode::Sliced,
                                                                         .size = {3.0f, 1.0f},
                                                                         .sortingLayer = "Back"});
    // Without a sprite, or with one that does not exist, nothing is drawn.
    scene.add<devex::scene::SpriteRenderer>(scene.createEntity("Empty"));
    scene.add<devex::scene::SpriteRenderer>(scene.createEntity("Missing"),
                                            devex::scene::SpriteRenderer{.sprite = AssetId::generate()});
    scene.updateTransforms();

    devex::render::RenderWorld world;
    devex::runtime::extractScene(scene, assets, world);
    devex::runtime::extractSprites(scene, assets, {.layers = {"Back", "Default", "Front"}}, world);

    CHECK(world.camera.projection == devex::render::Projection::Orthographic);
    CHECK(world.camera.orthographicSize == 3.0f);
    CHECK(world.camera.farPlane == 50.0f);
    REQUIRE(world.sprites.size() == 2);
    const auto shown = [&](devex::scene::Entity entity) -> const devex::render::RenderSprite& {
        const auto found = std::ranges::find(world.sprites, entity.index + 1, &devex::render::RenderSprite::objectId);
        REQUIRE(found != world.sprites.end());
        return *found;
    };
    const devex::render::RenderSprite& shownHero = shown(hero);
    // The second frame, at its pixels.
    CHECK(shownHero.uvRect.x == 0.5f);
    CHECK(shownHero.size == devex::math::Vec2{1.0f, 1.0f});
    CHECK(shownHero.pivot.y == 0.0f);
    CHECK(shownHero.flipX);
    CHECK(shownHero.layer == 1);
    CHECK(shownHero.order == 3);
    CHECK(shownHero.texture.isValid());
    const devex::render::RenderSprite& shownPlatform = shown(platformEntity);
    CHECK(shownPlatform.mode == devex::render::SpriteMode::Sliced);
    CHECK(shownPlatform.size == devex::math::Vec2{3.0f, 1.0f});
    CHECK(shownPlatform.border.x == 0.25f);
    CHECK(shownPlatform.color == devex::math::Vec4{1.0f, 0.5f, 2.0f, 0.5f});
    CHECK(shownPlatform.layer == -1);
}

TEST_CASE("Extraction draws the tiles of tilemaps, animated and mirrored", "[runtime][assets][gpu]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {});
    REQUIRE(renderer.has_value());

    // Four sprites side by side on a texture: ground, then three frames of water.
    MemorySource source;
    const AssetId textureId = AssetId::generate();
    devex::asset::TextureData texture{.format = devex::asset::TextureFormat::Rgba8Srgb};
    texture.mips.push_back({.width = 64, .height = 16, .bytes = std::vector<std::byte>(64 * 16 * 4, std::byte{255})});
    source.set(textureId, AssetType::Texture, devex::asset::encodeTexture(texture));
    std::vector<AssetId> sprites;
    for (std::uint32_t index = 0; index < 4; ++index)
    {
        sprites.push_back(AssetId::generate());
        source.set(sprites.back(), AssetType::Sprite,
                   devex::asset::encodeSprite({.texture = textureId, .x = index * 16, .width = 16, .height = 16,
                                               .textureWidth = 64, .textureHeight = 16, .pixelsPerUnit = 16.0f}));
    }
    const AssetId tilesetId = AssetId::generate();
    source.set(tilesetId, AssetType::Tileset,
               devex::asset::encodeTileset({.tiles = {{.id = 1, .sprite = sprites[0]},
                                                      {.id = 2, .sprite = sprites[1],
                                                       .frames = {sprites[1], sprites[2], sprites[3]}, .fps = 2.0f}}}));
    devex::runtime::AssetManager assets(&*renderer, &source);

    devex::scene::Scene scene;
    const auto level = scene.createEntity("Level");
    scene.add<devex::scene::Transform>(level, devex::scene::Transform{.position = {1.0f, 2.0f, 0.0f}});
    devex::scene::Tilemap tilemap{.tileset = tilesetId, .cellSize = {0.5f, 0.5f}, .sortingLayer = "Back", .order = -1};
    devex::scene::setTile(tilemap, {0, 0}, 1);
    devex::scene::setTile(tilemap, {1, 0}, 1 | devex::scene::tileFlipX);
    devex::scene::setTile(tilemap, {2, 0}, 2);
    // A tile the tileset does not have is not drawn.
    devex::scene::setTile(tilemap, {3, 0}, 9);
    scene.add<devex::scene::Tilemap>(level, tilemap);
    // Without a tileset, nothing.
    scene.add<devex::scene::Transform>(scene.createEntity("Empty"));
    scene.updateTransforms();

    devex::render::RenderWorld world;
    // Water on its second frame: 2 frames a second, at 0.6 s.
    devex::runtime::extractTilemaps(scene, assets, {.layers = {"Back", "Default"}}, 0.6, world);
    REQUIRE(world.tilemaps.size() == 1);
    const devex::render::RenderTilemap& drawn = world.tilemaps.front();
    CHECK(drawn.objectId == level.index + 1);
    CHECK(drawn.layer == -1);
    CHECK(drawn.order == -1);
    CHECK(drawn.cellSize == devex::math::Vec2{0.5f, 0.5f});
    CHECK(drawn.transform[3].x == 1.0f);
    REQUIRE(drawn.tileCount == 3);
    const auto tileAt = [&](int x) {
        return *std::ranges::find(world.tiles, devex::math::IVec2{x, 0}, &devex::render::RenderTile::cell);
    };
    CHECK(tileAt(0).uvRect == devex::math::Vec4{0.0f, 0.0f, 0.25f, 1.0f});
    CHECK(tileAt(1).uvRect == devex::math::Vec4{0.25f, 0.0f, 0.0f, 1.0f});
    CHECK(tileAt(2).uvRect.x == 0.5f);
    CHECK(tileAt(0).texture.isValid());
}
