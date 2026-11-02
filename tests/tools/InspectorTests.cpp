#include "tools/EditorDock.hpp"
#include "tools/Theme.hpp"

#include <devex/platform/Input.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/Components.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <string_view>

TEST_CASE("Inspector search reveals properties and restores collapsed sections",
          "[tools][inspector][gpu]")
{
    using namespace devex;
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    auto window =
        platform->createWindow({.width = 1200, .height = 900, .vulkan = true, .hidden = true});
    REQUIRE(window);
    auto renderer = render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer);
    auto overlay = tools::ToolsOverlay::create(*platform, *window, *renderer);
    REQUIRE(overlay);
    (*overlay)->setVisible(true);
    scene::Scene scene;
    const auto entity = scene.createEntity("Camera");
    scene.add<scene::Transform>(entity);
    scene.add<scene::Camera>(entity);
    const std::array selected{scene.uuid(entity)};
    (*overlay)->select(selected);
    const auto frame = [&](math::Vec2 mouse = {}, bool left = false, std::string_view typed = {},
                           platform::Key key = platform::Key::Unknown)
    {
        platform->pollEvents([](const auto&) {});
        auto& input = const_cast<platform::Input&>(platform->toolsInput());
        input.moveMouse(mouse, {});
        input.setMouseButtonDown(platform::MouseButton::Left, left);
        input.addTypedText(typed);
        input.setKeyDown(platform::Key::Escape, key == platform::Key::Escape);
        (*overlay)->update(scene, std::chrono::milliseconds(16));
        auto& world = renderer->beginFrame();
        (*overlay)->prepareRender(scene, world, tools::PlayState::Editing);
        std::size_t letters = 0;
        for (const auto& surface : world.uiSurfaces)
            if (surface.id == 6)
                for (const auto& draw : surface.draws)
                    if (draw.kind == render::UiDrawKind::Text ||
                        draw.kind == render::UiDrawKind::Glyph)
                        letters += draw.indexCount;
        REQUIRE(renderer->endFrame());
        return letters;
    };
    frame();
    frame();
    const auto expanded = frame();
    REQUIRE(expanded > 0);
    const auto metrics = tools::detail::deriveThemeMetrics({}, window->displayScale());
    const float scale = tools::detail::effectiveInterfaceScale({}, window->displayScale());
    const auto dock = tools::detail::placeDock(
        tools::detail::DockLayout::defaults(false), [](std::string_view) { return true; },
        {0.0f, metrics.menuBarHeight}, {1200.0f, 900.0f}, metrics.dockGap, metrics.tabHeight);
    const auto& inspector =
        dock.contents[static_cast<std::size_t>(tools::detail::DockSlot::RightTop)];
    const float right = inspector.max.x - metrics.panelPadding;
    const float top = inspector.min.y + metrics.panelPadding;
    const math::Vec2 collapse{right - 45.5f * scale, top + 16.5f * scale};
    const math::Vec2 expand{right - 16.5f * scale, top + 16.5f * scale};
    const math::Vec2 filter{inspector.min.x + metrics.panelPadding + 65.0f * scale,
                            top + 16.5f * scale};
    frame(collapse, true);
    frame(collapse);
    const auto collapsed = frame();
    CHECK(collapsed < expanded);
    frame(filter, true);
    frame(filter);
    REQUIRE((*overlay)->wantsTextInput());
    frame(filter, false, "PoSiTiOn transform");
    const auto searched = frame();
    CHECK(searched > collapsed);
    CHECK(searched < expanded);
    // Escape clears the query, revealing the user's previous fold state rather than opening all.
    frame(filter, false, {}, platform::Key::Escape);
    frame();
    CHECK(frame() == collapsed);
    frame(expand, true);
    frame(expand);
    CHECK(frame() == expanded);
}
