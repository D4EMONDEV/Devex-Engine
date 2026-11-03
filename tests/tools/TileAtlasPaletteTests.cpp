#include "tools/EditorDock.hpp"
#include "tools/Theme.hpp"
#include "tools/TileAtlasPalette.hpp"

#include <devex/asset/Project.hpp>
#include <devex/asset/TextureData.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/platform/Input.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/TilemapComponents.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <unordered_map>

using namespace devex;
using namespace devex::tools::detail;

TEST_CASE("Tile palettes retain source positions, gaps and nonsequential tile IDs",
          "[tools][tilepalette]")
{
    const auto texture = asset::AssetId::generate();
    const std::array sprites{asset::AssetId::generate(), asset::AssetId::generate(),
                             asset::AssetId::generate()};
    std::unordered_map<asset::AssetId, std::shared_ptr<const asset::SpriteData>> data;
    data[sprites[0]] = std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = texture,
                                                                             .x = 32,
                                                                             .y = 16,
                                                                             .width = 16,
                                                                             .height = 16,
                                                                             .textureWidth = 64,
                                                                             .textureHeight = 32});
    data[sprites[1]] = std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = texture,
                                                                             .x = 0,
                                                                             .y = 0,
                                                                             .width = 16,
                                                                             .height = 16,
                                                                             .textureWidth = 64,
                                                                             .textureHeight = 32});
    data[sprites[2]] = std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = texture,
                                                                             .x = 16,
                                                                             .y = 0,
                                                                             .width = 16,
                                                                             .height = 16,
                                                                             .textureWidth = 64,
                                                                             .textureHeight = 32});
    std::array tiles{asset::TileData{.id = 9, .sprite = sprites[0]},
                     asset::TileData{.id = 3, .sprite = sprites[1]},
                     asset::TileData{.id = 20, .sprite = sprites[2]}};
    const PaletteSpriteSource lookup = [&](asset::AssetId id) { return data.at(id); };
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto sources = makeTileAtlases(tiles, lookup);
        REQUIRE(sources.size() == 1);
        const auto& atlas = sources.front();
        CHECK(atlas.texture == texture);
        CHECK(atlas.pixels == math::Vec2{64, 32});
        CHECK(atlas.tileAt({8, 8}) == 3);
        CHECK(atlas.tileAt({16, 8}) == 20);
        CHECK(atlas.tileAt({40, 24}) == 9);
        CHECK(atlas.tileAt({8, 24}) == 0); // An unused cell stays empty.
        CHECK(atlas.tileAt({48, 24}) == 0);
        CHECK(atlas.tileAt({64, 32}) == 0);
        CHECK(atlas.tileAt({-1, 0}) == 0);
        std::ranges::reverse(tiles); // Changing list order never rearranges the sheet.
    }
}

TEST_CASE("Tile palettes separate images and retain missing or overlapping variants",
          "[tools][tilepalette]")
{
    const std::array textures{asset::AssetId::generate(), asset::AssetId::generate()};
    const std::array sprites{asset::AssetId::generate(), asset::AssetId::generate(),
                             asset::AssetId::generate(), asset::AssetId::generate()};
    std::unordered_map<asset::AssetId, std::shared_ptr<const asset::SpriteData>> data;
    data[sprites[0]] = std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = textures[0],
                                                                             .width = 32,
                                                                             .height = 32,
                                                                             .textureWidth = 32,
                                                                             .textureHeight = 32});
    data[sprites[1]] = std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = textures[0],
                                                                             .x = 16,
                                                                             .width = 16,
                                                                             .height = 16,
                                                                             .textureWidth = 32,
                                                                             .textureHeight = 32});
    data[sprites[2]] = std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = textures[1],
                                                                             .width = 16,
                                                                             .height = 16,
                                                                             .textureWidth = 32,
                                                                             .textureHeight = 32});
    const std::array tiles{
        asset::TileData{.id = 1, .sprite = sprites[0]},
        asset::TileData{.id = 2, .sprite = sprites[1]},
        asset::TileData{.id = 3, .sprite = sprites[1]}, // Same region, another variant.
        asset::TileData{.id = 4, .frames = {sprites[2]}, .fps = 8},
        asset::TileData{.id = 5, .sprite = sprites[3]},
        asset::TileData{.id = 6}};
    const auto sources = makeTileAtlases(tiles,
                                         [&](asset::AssetId id)
                                         {
                                             const auto found = data.find(id);
                                             return found == data.end() ? nullptr : found->second;
                                         });
    REQUIRE(sources.size() == 3);
    CHECK(sources[0].regions.size() == 3);
    CHECK(sources[0].tileAt({20, 8}) == 2); // The smaller region remains reachable.
    CHECK(sources[0].tileAt({8, 24}) == 1);
    CHECK(sources[1].texture == textures[1]);
    CHECK(sources[1].tileAt({8, 8}) == 4); // An animation uses its first frame as its location.
    CHECK_FALSE(sources[2].texture.isValid());
    CHECK(sources[2].regions.size() == 2); // Missing sprites remain selectable in the list.
    CHECK(sources[2].tileAt({0, 0}) == 0);
    CHECK(makeTileAtlases({}, {}).empty());
}

TEST_CASE("Atlas zoom and pan retain the pointer pixel and keep the image reachable",
          "[tools][tilepalette]")
{
    TileAtlasView view;
    view.pixels = {256, 128};
    view.resize({400, 300});
    CHECK(view.scale == Catch::Approx(400.0 / 256.0 * 0.95));
    CHECK(view.offset.x > 0);
    CHECK(view.offset.y > 0);
    view.zoom(4, view.viewport * 0.5f);
    const math::Vec2 at{160, 120};
    const auto pixel = view.pixelAt(at);
    view.zoom(1.25f, at);
    CHECK(view.pixelAt(at).x == Catch::Approx(pixel.x));
    CHECK(view.pixelAt(at).y == Catch::Approx(pixel.y));
    view.pan({1.0e6f, 1.0e6f});
    CHECK(view.offset == math::Vec2{0});
    view.pan({-1.0e6f, -1.0e6f});
    CHECK(view.offset.x + view.pixels.x * view.scale == Catch::Approx(view.viewport.x));
    CHECK(view.offset.y + view.pixels.y * view.scale == Catch::Approx(view.viewport.y));
    view.zoom(1.0e-6f, at);
    CHECK(view.scale > 0);
    CHECK(std::isfinite(view.scale));
    CHECK(view.offset.x >= 0);
    view.fit();
    view.resize({160, 120});
    CHECK(view.fitted);
    CHECK(view.scale == Catch::Approx(160.0 / 256.0 * 0.95));
    CHECK(view.pixelAt(view.viewport * 0.5f) == view.pixels * 0.5f);
}

TEST_CASE("The inspector paints the tile clicked in a continuous atlas and preserves undo",
          "[tools][tilepalette][gpu]")
{
    struct Directory
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("devex-tile-palette-" + core::Uuid::generate().toString());
        ~Directory()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } directory;
    auto project = asset::createProject(directory.path, "Tile Palette");
    REQUIRE(project);
    core::JobSystem jobs(1);
    auto database = asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database);
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window =
        platform->createWindow({.width = 1600, .height = 1400, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    const auto imageId = asset::AssetId::generate();
    const auto tilesetId = asset::AssetId::generate();
    const std::array spriteIds{asset::AssetId::generate(), asset::AssetId::generate()};
    auto tileset = std::make_shared<asset::TilesetData>();
    tileset->tiles = {{.id = 9, .sprite = spriteIds[0]}, {.id = 37, .sprite = spriteIds[1]}};
    const std::array sprites{
        std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = imageId,
                                                              .width = 16,
                                                              .height = 16,
                                                              .textureWidth = 64,
                                                              .textureHeight = 32}),
        std::make_shared<asset::SpriteData>(asset::SpriteData{.texture = imageId,
                                                              .x = 32,
                                                              .y = 16,
                                                              .width = 16,
                                                              .height = 16,
                                                              .textureWidth = 64,
                                                              .textureHeight = 32})};
    auto texture = renderer->createTexture(asset::TextureData{
        .filter = asset::TextureFilter::Nearest,
        .mips = {{.width = 64,
                  .height = 32,
                  .bytes = std::vector<std::byte>(64 * 32 * 4, std::byte{255})}}});
    REQUIRE(texture);
    auto overlay = tools::ToolsOverlay::create(
        *platform, *window, *renderer, tools::ToolsMode::Editor, directory.path / "User.dvx");
    REQUIRE(overlay);
    (*overlay)->setAssetDatabase(database->get());
    (*overlay)->setSpriteSources(
        [&](asset::AssetId id) { return id == imageId ? *texture : render::TextureHandle{}; },
        [&](asset::AssetId id)
        { return id == imageId ? math::Extent2D{64, 32} : math::Extent2D{}; },
        [&](asset::AssetId id) -> std::shared_ptr<const asset::SpriteData>
        {
            for (std::size_t i = 0; i < spriteIds.size(); ++i)
                if (id == spriteIds[i])
                    return sprites[i];
            return nullptr;
        },
        [&](asset::AssetId id) -> std::shared_ptr<const asset::TilesetData>
        { return id == tilesetId ? tileset : nullptr; });
    scene::Scene scene;
    // Let the editor open its initial project document before selecting our test entity.
    (*overlay)->update(scene, std::chrono::milliseconds(16));
    scene.setKind(scene::SceneKind::TwoD);
    const auto entity = scene.createEntity("Ground");
    scene.add<scene::Transform>(entity);
    scene.add<scene::Tilemap>(entity, scene::Tilemap{.tileset = tilesetId});
    scene.updateTransforms();
    const std::array selected{scene.uuid(entity)};
    (*overlay)->select(selected);
    const auto metrics = deriveThemeMetrics({}, window->displayScale());
    const auto dock = placeDock(
        DockLayout::defaults(true), [](std::string_view) { return true; },
        {0, metrics.menuBarHeight}, {1600, 1400}, metrics.dockGap, metrics.tabHeight);
    const auto inspector = dock.contents[static_cast<std::size_t>(DockSlot::RightTop)];
    const auto origin = inspector.min + math::Vec2{metrics.panelPadding};
    const float pixelsPerPoint =
        static_cast<float>(window->pixelSize().width) / static_cast<float>(window->size().width);
    math::Vec2 imageMin{}, imageMax{};
    std::size_t images = 0;
    std::size_t selectionVertices = 0;
    std::optional<math::Vec4> paletteBackground;
    const auto frame = [&](math::Vec2 mouse = {}, bool left = false, float wheel = 0.0f)
    {
        platform->pollEvents([](const auto&) {});
        auto& input = const_cast<platform::Input&>(platform->toolsInput());
        input.moveMouse(mouse, {});
        input.setMouseButtonDown(platform::MouseButton::Left, left);
        input.scrollMouse({0, wheel});
        (*overlay)->update(scene, std::chrono::milliseconds(16));
        auto& world = renderer->beginFrame();
        (*overlay)->prepareRender(scene, world, tools::PlayState::Editing);
        images = 0;
        for (const auto& surface : world.uiSurfaces)
            if (surface.id == 6)
                for (const auto& draw : surface.draws)
                    if (draw.texture == *texture)
                        for (std::uint32_t offset = 0; offset + 6 <= draw.indexCount; offset += 6)
                        {
                            math::Vec2 low{1.0e9f}, high{-1.0e9f};
                            for (std::uint32_t i = 0; i < 6; ++i)
                            {
                                const auto p =
                                    surface.vertices[surface.indices[draw.firstIndex + offset + i]]
                                        .position;
                                low = math::min(low, p);
                                high = math::max(high, p);
                            }
                            imageMin = origin + low / pixelsPerPoint;
                            imageMax = origin + high / pixelsPerPoint;
                            ++images;
                        }
        selectionVertices = 0;
        paletteBackground.reset();
        float backgroundArea = 1.0e9f;
        for (const auto& surface : world.uiSurfaces)
            if (surface.id == 6)
                for (const auto& draw : surface.draws)
                    if (!draw.texture.isValid() && (draw.kind == render::UiDrawKind::Quad ||
                                                    draw.kind == render::UiDrawKind::RoundedQuad))
                        for (std::uint32_t offset = 0; offset + 6 <= draw.indexCount; offset += 6)
                        {
                            math::Vec2 low{1.0e9f}, high{-1.0e9f};
                            for (std::uint32_t i = 0; i < 6; ++i)
                            {
                                const auto p =
                                    origin +
                                    surface.vertices[surface.indices[draw.firstIndex + offset + i]]
                                            .position /
                                        pixelsPerPoint;
                                low = math::min(low, p);
                                high = math::max(high, p);
                            }
                            const float area = (high.x - low.x) * (high.y - low.y);
                            if (low.x <= imageMin.x && low.y < imageMin.y && high.x >= imageMax.x &&
                                high.y > imageMax.y && area < backgroundArea)
                            {
                                backgroundArea = area;
                                paletteBackground =
                                    surface.vertices[surface.indices[draw.firstIndex + offset]]
                                        .color;
                            }
                        }
        for (const auto& surface : world.uiSurfaces)
            if (surface.id == 6)
                for (const auto& vertex : surface.vertices)
                {
                    const auto position = origin + vertex.position / pixelsPerPoint;
                    if (vertex.color.b > 0.8f && vertex.color.r < 0.3f && vertex.color.a > 0.9f &&
                        position.x >= imageMin.x - 2 && position.x <= imageMax.x + 2 &&
                        position.y >= imageMin.y - 2 && position.y <= imageMax.y + 2)
                        ++selectionVertices;
                }
        REQUIRE(renderer->endFrame());
    };
    for (int i = 0; i < 4; ++i)
        frame();
    REQUIRE(images == 1); // One full sheet, rather than one image per tile.
    CHECK(selectionVertices >= 8);
    CHECK((imageMax.x - imageMin.x) / (imageMax.y - imageMin.y) == Catch::Approx(2.0));
    const auto pick = imageMin + (imageMax - imageMin) * math::Vec2{40.0f / 64, 24.0f / 32};
    frame(pick);
    REQUIRE(paletteBackground);
    const auto background = paletteBackground;
    frame(pick, true);
    CHECK(paletteBackground == background);
    frame(pick);
    CHECK(paletteBackground == background);
    frame(pick, true);
    CHECK(paletteBackground == background);
    frame(pick);
    CHECK(paletteBackground == background);
    const auto viewport = (dock.center.min + dock.center.max) * 0.5f;
    frame(viewport, true);
    frame(viewport);
    frame();
    const auto cells = scene::TileGrid::read(scene.get<scene::Tilemap>(entity)).cells();
    REQUIRE(cells.size() == 1);
    CHECK(cells.front().value == 37);
    REQUIRE((*overlay)->history().undo(scene));
    CHECK(scene::TileGrid::read(scene.get<scene::Tilemap>(entity)).count() == 0);
    const auto before = imageMax - imageMin;
    frame((imageMin + imageMax) * 0.5f, false, 1);
    frame();
    CHECK(imageMax.x - imageMin.x > before.x);
    CHECK(imageMax.y - imageMin.y > before.y);
}
