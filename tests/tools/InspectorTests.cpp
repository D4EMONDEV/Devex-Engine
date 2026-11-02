#include "tools/EditorDock.hpp"
#include "tools/Theme.hpp"

#include <devex/platform/Input.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <string_view>
#include <unordered_set>

TEST_CASE("All built-in inspector fields belong to explicit, uniquely named groups",
          "[tools][inspector][groups]")
{
    std::size_t checked = 0;
    for (const auto& component : devex::scene::componentRegistry().types())
    {
        if (devex::tools::detail::isGameComponent(component.name()))
            continue;
        CAPTURE(component.name());
        std::string current;
        std::unordered_set<std::string> names;
        bool hasProperties = false;
        for (const auto& field : component.type->fields)
        {
            if (field.hidden || field.runtime)
                continue;
            CAPTURE(field.name);
            hasProperties = true;
            if (!field.group.empty())
            {
                CHECK(names.insert(field.group).second);
                current = field.group;
            }
            CHECK(current.find_first_not_of(" \t\r\n") != std::string::npos);
        }
        checked += hasProperties ? 1 : 0;
    }
    CHECK(checked > 0);
}

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
    math::Vec2 firstDropdown{1.0e9f};
    const auto frame = [&](math::Vec2 mouse = {}, bool left = false, std::string_view typed = {},
                           platform::Key key = platform::Key::Unknown)
    {
        platform->pollEvents([](const auto&) {});
        auto& input = const_cast<platform::Input&>(platform->toolsInput());
        input.moveMouse(mouse, {});
        input.setMouseButtonDown(platform::MouseButton::Left, left);
        input.addTypedText(typed);
        input.setKeyDown(platform::Key::Escape, key == platform::Key::Escape);
        input.setKeyDown(platform::Key::Down, key == platform::Key::Down);
        input.setKeyDown(platform::Key::Enter, key == platform::Key::Enter);
        (*overlay)->update(scene, std::chrono::milliseconds(16));
        auto& world = renderer->beginFrame();
        (*overlay)->prepareRender(scene, world, tools::PlayState::Editing);
        std::size_t letters = 0;
        firstDropdown = math::Vec2{1.0e9f};
        for (const auto& surface : world.uiSurfaces)
            if (surface.id == 6)
                for (const auto& draw : surface.draws)
                {
                    if (draw.kind == render::UiDrawKind::Text ||
                        draw.kind == render::UiDrawKind::Glyph)
                        letters += draw.indexCount;
                    // The two short strokes at the right edge of a choice field. Other arrows
                    // are at the left; field backgrounds and scrollbars are taller or wider.
                    if (draw.kind == render::UiDrawKind::Quad && !draw.texture.isValid())
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
                            const auto size = high - low;
                            if (low.x > static_cast<float>(surface.size.width) - 36 && size.x > 2 &&
                                size.x < 18 && size.y > 2 && size.y < 18 && low.y < firstDropdown.y)
                                firstDropdown = (low + high) * 0.5f;
                        }
                }
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
    REQUIRE(firstDropdown.x < 1.0e9f);
    const float pixelsPerPoint =
        static_cast<float>(window->pixelSize().width) / static_cast<float>(window->size().width);
    const auto choice =
        inspector.min + math::Vec2{metrics.panelPadding} + firstDropdown / pixelsPerPoint;
    frame(choice, true);
    frame(choice);
    frame({}, false, {}, platform::Key::Down);
    frame();
    frame({}, false, {}, platform::Key::Enter);
    frame();
    // The inspector disables general focus navigation, but an open choice still owns arrows and
    // Enter.
    CHECK(scene.get<scene::Camera>(entity).projection == scene::Projection::Orthographic);
}
