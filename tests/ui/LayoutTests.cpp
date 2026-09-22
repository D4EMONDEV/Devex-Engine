#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/Layout.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using devex::math::Vec2;
using devex::math::Vec4;
using devex::scene::Canvas;
using devex::scene::CanvasScaleMode;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::TextAlign;
using devex::scene::UiLayout;
using devex::scene::UiLayoutKind;
using devex::scene::UiRect;
using devex::ui::LaidOutRect;
using devex::ui::LayoutResult;

namespace {

constexpr Vec2 window{1920.0f, 1080.0f};

[[nodiscard]] Entity addCanvas(Scene& scene, Canvas canvas = {})
{
    const Entity entity = scene.createEntity("Canvas");
    scene.add<Canvas>(entity, canvas);
    return entity;
}

[[nodiscard]] Entity addRect(Scene& scene, Entity parent, UiRect rect)
{
    const Entity entity = scene.createEntity("Element");
    REQUIRE(scene.setParent(entity, parent).has_value());
    scene.add<UiRect>(entity, rect);
    return entity;
}

[[nodiscard]] const LaidOutRect& placed(const LayoutResult& result, Entity entity)
{
    const LaidOutRect* const rect = result.find(entity);
    REQUIRE(rect != nullptr);
    return *rect;
}

} // namespace

TEST_CASE("Anchors that meet give an element its own size, wherever the parent puts it",
          "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    // A button of 200 by 60, centred.
    const Entity button = addRect(scene, canvas,
                                  UiRect{.anchorMin = {0.5f, 0.5f},
                                         .anchorMax = {0.5f, 0.5f},
                                         .offsetMin = {-100.0f, -30.0f},
                                         .offsetMax = {100.0f, 30.0f}});

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);

    const LaidOutRect& rect = placed(result, button);
    CHECK(rect.min.x == Catch::Approx(860.0f));
    CHECK(rect.min.y == Catch::Approx(510.0f));
    CHECK(rect.size().x == Catch::Approx(200.0f));
    CHECK(rect.size().y == Catch::Approx(60.0f));
    // The pivot sits in the middle by default, which is what it turns and grows around.
    CHECK(rect.pivot.x == Catch::Approx(960.0f));
    CHECK(rect.pivot.y == Catch::Approx(540.0f));
}

TEST_CASE("Anchors apart stretch an element with its parent", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    // A bar along the top, 24 units away from each side and 80 tall.
    const Entity bar = addRect(scene, canvas,
                               UiRect{.anchorMin = {0.0f, 0.0f},
                                      .anchorMax = {1.0f, 0.0f},
                                      .offsetMin = {24.0f, 0.0f},
                                      .offsetMax = {-24.0f, 80.0f}});

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);

    const LaidOutRect& rect = placed(result, bar);
    CHECK(rect.min.x == Catch::Approx(24.0f));
    CHECK(rect.max.x == Catch::Approx(1896.0f));
    CHECK(rect.size().y == Catch::Approx(80.0f));

    // A narrower window shortens the bar and leaves its margins alone.
    devex::ui::layoutCanvas(scene, canvas, Vec2{800.0f, 600.0f}, result);
    CHECK(placed(result, bar).max.x == Catch::Approx(776.0f));
    CHECK(placed(result, bar).size().y == Catch::Approx(80.0f));
}

TEST_CASE("A canvas that scales with the screen keeps its reference size", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.referenceResolution = {1920.0f, 1080.0f}});
    const Entity button = addRect(scene, canvas,
                                  UiRect{.offsetMin = {-100.0f, -30.0f}, .offsetMax = {100.0f, 30.0f}});

    LayoutResult result;
    // The window is half the reference on both axes: everything is laid out at the reference size
    // and drawn half as large.
    devex::ui::layoutCanvas(scene, canvas, Vec2{960.0f, 540.0f}, result);
    CHECK(result.scale == Catch::Approx(0.5f));
    CHECK(result.canvasSize.x == Catch::Approx(1920.0f));
    CHECK(placed(result, button).size().x == Catch::Approx(200.0f));

    // A window wider than the reference, matching both axes evenly, sits between the two ratios.
    devex::ui::layoutCanvas(scene, canvas, Vec2{3840.0f, 1080.0f}, result);
    CHECK(result.scale == Catch::Approx(std::sqrt(2.0f)));
}

TEST_CASE("A column places its children one under the other", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity menu = addRect(scene, canvas,
                                UiRect{.anchorMin = {0.5f, 0.5f},
                                       .anchorMax = {0.5f, 0.5f},
                                       .offsetMin = {-200.0f, -200.0f},
                                       .offsetMax = {200.0f, 200.0f}});
    scene.add<UiLayout>(menu, UiLayout{.kind = UiLayoutKind::Column,
                                       .spacing = 10.0f,
                                       .padding = Vec4{20.0f, 20.0f, 20.0f, 20.0f},
                                       .align = TextAlign::Left});
    const UiRect entry{.anchorMin = {0.0f, 0.0f},
                       .anchorMax = {1.0f, 0.0f},
                       .offsetMin = {0.0f, 0.0f},
                       .offsetMax = {0.0f, 60.0f}};
    const Entity play = addRect(scene, menu, entry);
    const Entity options = addRect(scene, menu, entry);
    const Entity quit = addRect(scene, menu, entry);

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);

    // The menu runs from 760 to 1160 across and 340 to 740 down; the padding takes 20 off each side.
    CHECK(placed(result, play).min.y == Catch::Approx(360.0f));
    CHECK(placed(result, play).size().y == Catch::Approx(60.0f));
    CHECK(placed(result, options).min.y == Catch::Approx(430.0f));
    CHECK(placed(result, quit).min.y == Catch::Approx(500.0f));
    // Across the column, the entries stretch between the paddings.
    CHECK(placed(result, quit).min.x == Catch::Approx(780.0f));
    CHECK(placed(result, quit).max.x == Catch::Approx(1140.0f));
}

TEST_CASE("A column centres its children and closes the gap a hidden one leaves", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity menu = addRect(scene, canvas,
                                UiRect{.anchorMin = {0.0f, 0.0f},
                                       .anchorMax = {0.0f, 0.0f},
                                       .offsetMin = {0.0f, 0.0f},
                                       .offsetMax = {400.0f, 400.0f}});
    scene.add<UiLayout>(menu, UiLayout{.kind = UiLayoutKind::Column, .spacing = 20.0f});
    const UiRect entry{.anchorMin = {0.0f, 0.0f},
                       .anchorMax = {1.0f, 0.0f},
                       .offsetMin = {0.0f, 0.0f},
                       .offsetMax = {0.0f, 100.0f}};
    const Entity first = addRect(scene, menu, entry);
    const Entity second = addRect(scene, menu, entry);

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);
    // Two entries of 100 and one gap of 20 take 220 of the 400, centred: 90 above and below.
    CHECK(placed(result, first).min.y == Catch::Approx(90.0f));
    CHECK(placed(result, second).min.y == Catch::Approx(210.0f));

    scene.get<UiRect>(second).visible = false;
    devex::ui::layoutCanvas(scene, canvas, window, result);
    // With one entry left, it takes the middle on its own.
    CHECK(placed(result, first).min.y == Catch::Approx(150.0f));
    CHECK_FALSE(placed(result, second).visible);
}

TEST_CASE("A row shares what is left over between the children that stretch", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity bar = addRect(scene, canvas,
                               UiRect{.anchorMin = {0.0f, 0.0f},
                                      .anchorMax = {0.0f, 0.0f},
                                      .offsetMin = {0.0f, 0.0f},
                                      .offsetMax = {500.0f, 100.0f}});
    scene.add<UiLayout>(bar, UiLayout{.kind = UiLayoutKind::Row, .spacing = 10.0f});
    // A fixed icon, then a label that takes the rest.
    const Entity icon = addRect(scene, bar,
                                UiRect{.anchorMin = {0.0f, 0.0f},
                                       .anchorMax = {0.0f, 1.0f},
                                       .offsetMin = {0.0f, 0.0f},
                                       .offsetMax = {80.0f, 0.0f}});
    const Entity label = addRect(scene, bar,
                                 UiRect{.anchorMin = {0.0f, 0.0f},
                                        .anchorMax = {1.0f, 1.0f},
                                        .offsetMin = {0.0f, 0.0f},
                                        .offsetMax = {0.0f, 0.0f}});

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);

    CHECK(placed(result, icon).min.x == Catch::Approx(0.0f));
    CHECK(placed(result, icon).size().x == Catch::Approx(80.0f));
    CHECK(placed(result, label).min.x == Catch::Approx(90.0f));
    CHECK(placed(result, label).size().x == Catch::Approx(410.0f));
    // Both fill the height of the bar, which they stretch across.
    CHECK(placed(result, icon).size().y == Catch::Approx(100.0f));
}

TEST_CASE("A grid lines its children up in cells of the same size", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity grid = addRect(scene, canvas,
                                UiRect{.anchorMin = {0.0f, 0.0f},
                                       .anchorMax = {0.0f, 0.0f},
                                       .offsetMin = {0.0f, 0.0f},
                                       .offsetMax = {320.0f, 320.0f}});
    scene.add<UiLayout>(grid,
                        UiLayout{.kind = UiLayoutKind::Grid, .spacing = 10.0f, .columns = 3});
    const UiRect slot{.anchorMin = {0.0f, 0.0f},
                      .anchorMax = {0.0f, 0.0f},
                      .offsetMin = {0.0f, 0.0f},
                      .offsetMax = {64.0f, 64.0f}};
    std::vector<Entity> slots;
    for (int index = 0; index < 5; ++index)
    {
        slots.push_back(addRect(scene, grid, slot));
    }

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);

    // Three columns of (320 - 20) / 3 = 100, and rows as tall as the slots ask for.
    CHECK(placed(result, slots[0]).size().x == Catch::Approx(100.0f));
    CHECK(placed(result, slots[0]).size().y == Catch::Approx(64.0f));
    CHECK(placed(result, slots[2]).min.x == Catch::Approx(220.0f));
    CHECK(placed(result, slots[2]).min.y == Catch::Approx(0.0f));
    // The fourth slot starts the second row.
    CHECK(placed(result, slots[3]).min.x == Catch::Approx(0.0f));
    CHECK(placed(result, slots[3]).min.y == Catch::Approx(74.0f));
}

TEST_CASE("Children follow their parent, and hiding a parent hides them", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity panel = addRect(scene, canvas,
                                 UiRect{.anchorMin = {0.0f, 0.0f},
                                        .anchorMax = {0.0f, 0.0f},
                                        .offsetMin = {100.0f, 100.0f},
                                        .offsetMax = {400.0f, 300.0f},
                                        .opacity = 0.5f});
    // A label filling its panel.
    const Entity label = addRect(scene, panel,
                                 UiRect{.anchorMin = {0.0f, 0.0f},
                                        .anchorMax = {1.0f, 1.0f},
                                        .offsetMin = {10.0f, 10.0f},
                                        .offsetMax = {-10.0f, -10.0f},
                                        .opacity = 0.5f});

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);

    CHECK(placed(result, label).min.x == Catch::Approx(110.0f));
    CHECK(placed(result, label).max.x == Catch::Approx(390.0f));
    // Opacity multiplies down the tree, so that one fade hides a whole menu.
    CHECK(placed(result, label).opacity == Catch::Approx(0.25f));
    // Parents come before their children, which is the order they are drawn in.
    CHECK(result.rects.front().entity == panel);

    scene.get<UiRect>(panel).visible = false;
    devex::ui::layoutCanvas(scene, canvas, window, result);
    CHECK_FALSE(placed(result, label).visible);
}

TEST_CASE("A point is inside an element once it is turned and scaled", "[ui][layout]")
{
    Scene scene;
    const Entity canvas = addCanvas(scene, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    const Entity button = addRect(scene, canvas,
                                  UiRect{.offsetMin = {-100.0f, -20.0f},
                                         .offsetMax = {100.0f, 20.0f},
                                         .rotation = devex::math::radians(90.0f)});

    LayoutResult result;
    devex::ui::layoutCanvas(scene, canvas, window, result);
    const LaidOutRect& rect = placed(result, button);

    // Turned by a quarter, the button is 40 wide and 200 tall around the middle of the window.
    CHECK(devex::ui::contains(rect, Vec2{960.0f, 640.0f}));
    CHECK_FALSE(devex::ui::contains(rect, Vec2{1040.0f, 540.0f}));
    // Its corners turn with it.
    const std::array<Vec2, 4> points = devex::ui::corners(rect);
    CHECK(points[0].x == Catch::Approx(980.0f).margin(0.001));
    CHECK(points[0].y == Catch::Approx(440.0f).margin(0.001));

    scene.get<UiRect>(button).rotation = 0.0f;
    scene.get<UiRect>(button).scale = Vec2{2.0f, 1.0f};
    devex::ui::layoutCanvas(scene, canvas, window, result);
    // Twice as wide around its pivot: 200 units to each side.
    CHECK(devex::ui::contains(placed(result, button), Vec2{1150.0f, 540.0f}));
    CHECK_FALSE(devex::ui::contains(placed(result, button), Vec2{1170.0f, 540.0f}));
}
