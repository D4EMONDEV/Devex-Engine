#include <devex/render/RenderWorld.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/UiWorld.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>

using devex::core::Duration;
using devex::math::Vec2;
using devex::scene::Canvas;
using devex::scene::CanvasScaleMode;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::scene::UiButton;
using devex::scene::UiImage;
using devex::scene::UiRect;
using devex::scene::UiText;
using devex::ui::UiInput;
using devex::ui::UiWorld;

namespace {

constexpr Vec2 window{1920.0f, 1080.0f};
constexpr Duration frame{std::chrono::milliseconds(16)};

// A canvas one unit per pixel, so that the tests speak in pixels.
struct Screen
{
    Scene scene;
    Entity canvas;
    UiWorld world;

    Screen()
    {
        canvas = scene.createEntity("Canvas");
        scene.add<Canvas>(canvas, Canvas{.scaleMode = CanvasScaleMode::ConstantPixels});
    }

    // An element of a fixed rectangle, from its top left corner.
    Entity element(Entity parent, Vec2 min, Vec2 max, const char* name = "Element")
    {
        const Entity entity = scene.createEntity(name);
        REQUIRE(scene.setParent(entity, parent.isValid() ? parent : canvas).has_value());
        scene.add<UiRect>(entity, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}, .offsetMin = min, .offsetMax = max});
        return entity;
    }

    Entity button(Entity parent, Vec2 min, Vec2 max, const char* action)
    {
        const Entity entity = element(parent, min, max, action);
        scene.add<UiImage>(entity);
        scene.add<UiButton>(entity, UiButton{.action = action, .fadeTime = 0.0f});
        return entity;
    }

    void update(UiInput input = {}, Duration delta = frame)
    {
        world.update(scene, window, input, delta);
    }

    void press(Vec2 point)
    {
        update(UiInput{.pointer = point, .pointerDown = true, .pointerPressed = true});
    }

    void release(Vec2 point)
    {
        update(UiInput{.pointer = point, .pointerReleased = true});
    }

    void click(Vec2 point)
    {
        press(point);
        release(point);
    }

    [[nodiscard]] const devex::ui::LaidOutRect* rectOf(Entity entity) const
    {
        for (const UiWorld::CanvasLayout& layout : world.canvases())
        {
            if (const devex::ui::LaidOutRect* const rect = layout.layout.find(entity))
            {
                return rect;
            }
        }
        return nullptr;
    }
};

} // namespace

TEST_CASE("A context menu opens under the pointer and closes once used", "[ui][controls]")
{
    Screen screen;
    const Entity row = screen.button({}, {100.0f, 100.0f}, {500.0f, 140.0f}, "row");
    const Entity other = screen.button({}, {100.0f, 400.0f}, {500.0f, 440.0f}, "other");
    const Entity menu = screen.element({}, {0.0f, 0.0f}, {200.0f, 60.0f}, "Menu");
    screen.scene.add<devex::scene::UiPopup>(menu);
    screen.scene.get<UiRect>(menu).visible = false;
    const Entity rename = screen.button(menu, {0.0f, 0.0f}, {200.0f, 30.0f}, "rename");
    screen.scene.add<devex::scene::UiContextMenu>(row, devex::scene::UiContextMenu{.popup = screen.scene.reference(menu)});
    screen.update();

    // The second button on the row opens the menu there, over the rest.
    screen.update(UiInput{.pointer = {300.0f, 120.0f}, .secondaryPressed = true});
    screen.update(UiInput{.pointer = {300.0f, 120.0f}});
    CHECK(screen.world.isPopupOpen(screen.scene, menu));
    CHECK(screen.world.contextTarget() == row);
    const devex::ui::LaidOutRect* const placed = screen.rectOf(rename);
    REQUIRE(placed != nullptr);
    CHECK(placed->min.x == Catch::Approx(300.0f));
    CHECK(placed->min.y == Catch::Approx(120.0f));
    CHECK(&screen.world.canvases().front().layout.rects.back() == placed);

    // Choosing an entry reports it and closes the menu.
    screen.click({350.0f, 135.0f});
    CHECK(screen.world.wasClicked("rename"));
    CHECK_FALSE(screen.world.isPopupOpen(screen.scene, menu));

    // Pressing outside closes it and does nothing else; Escape closes it without going back.
    screen.world.openPopup(screen.scene, menu, Vec2{300.0f, 120.0f});
    screen.update();
    screen.click({300.0f, 420.0f});
    CHECK_FALSE(screen.world.isPopupOpen(screen.scene, menu));
    CHECK_FALSE(screen.world.wasClicked(other));
    screen.world.openPopup(screen.scene, menu);
    screen.update();
    screen.update(UiInput{.cancelPressed = true});
    CHECK_FALSE(screen.world.isPopupOpen(screen.scene, menu));
    CHECK_FALSE(screen.world.wasCancelled());
}

TEST_CASE("A modal keeps the rest of the interface from the pointer and the keys", "[ui][controls]")
{
    Screen screen;
    const Entity behind = screen.button({}, {100.0f, 100.0f}, {300.0f, 140.0f}, "behind");
    const Entity dialog = screen.element({}, {700.0f, 400.0f}, {1200.0f, 700.0f}, "Dialog");
    screen.scene.add<devex::scene::UiPopup>(dialog, devex::scene::UiPopup{.kind = devex::scene::UiPopupKind::Modal});
    const Entity confirm = screen.button(dialog, {50.0f, 200.0f}, {250.0f, 250.0f}, "confirm");
    screen.world.openPopup(screen.scene, dialog);
    screen.update();

    screen.click({200.0f, 120.0f});
    CHECK_FALSE(screen.world.wasClicked(behind));
    screen.click({850.0f, 620.0f});
    CHECK(screen.world.wasClicked(confirm));
    // A modal stays open until it is closed, and the focus stays inside it.
    CHECK(screen.world.isPopupOpen(screen.scene, dialog));
    screen.update(UiInput{.moveY = 1});
    CHECK(screen.world.focused() == confirm);

    // Its veil covers the canvas, drawn before it.
    devex::render::RenderWorld drawn;
    screen.world.build(screen.scene, devex::ui::DrawContext{}, drawn);
    bool veiled = false;
    for (const devex::render::UiVertex& vertex : drawn.uiVertices)
    {
        veiled = veiled || (vertex.position.x == Catch::Approx(window.x) && vertex.position.y == Catch::Approx(window.y));
    }
    CHECK(veiled);

    screen.world.closePopup(screen.scene, dialog);
    screen.update();
    screen.click({200.0f, 120.0f});
    CHECK(screen.world.wasClicked(behind));
}

TEST_CASE("A dropdown lists its options and writes the chosen one", "[ui][controls]")
{
    Screen screen;
    const Entity quality = screen.element({}, {100.0f, 100.0f}, {400.0f, 140.0f}, "Quality");
    screen.scene.add<UiText>(quality);
    screen.scene.add<devex::scene::UiDropdown>(quality, devex::scene::UiDropdown{.options = {"Low", "Medium", "High"},
                                                                               .selected = 1,
                                                                               .action = "quality"});
    screen.update();
    CHECK(screen.scene.get<UiText>(quality).text == "Medium");

    // The list opens under it, one option as tall as the element.
    screen.click({250.0f, 120.0f});
    screen.update(UiInput{.pointer = {250.0f, 142.0f + 40.0f * 2.5f}, .pointerMoved = true});
    screen.press({250.0f, 142.0f + 40.0f * 2.5f});
    CHECK(screen.world.wasChanged("quality"));
    screen.release({250.0f, 142.0f + 40.0f * 2.5f});
    screen.update();
    CHECK(screen.scene.get<devex::scene::UiDropdown>(quality).selected == 2);
    CHECK(screen.scene.get<UiText>(quality).text == "High");

    // The keys walk it too, and Escape leaves it as it was.
    screen.click({250.0f, 120.0f});
    screen.update(UiInput{.moveY = -1});
    screen.update(UiInput{.cancelPressed = true});
    CHECK_FALSE(screen.world.wasCancelled());
    CHECK(screen.scene.get<devex::scene::UiDropdown>(quality).selected == 2);
    screen.click({250.0f, 120.0f});
    screen.update(UiInput{.moveY = -1});
    screen.update(UiInput{.submitPressed = true});
    CHECK(screen.scene.get<devex::scene::UiDropdown>(quality).selected == 1);
}

TEST_CASE("Two quick clicks on a button are a double click", "[ui][controls]")
{
    Screen screen;
    const Entity row = screen.button({}, {100.0f, 100.0f}, {500.0f, 140.0f}, "open");
    screen.update();
    screen.click({200.0f, 120.0f});
    CHECK_FALSE(screen.world.wasDoubleClicked(row));
    screen.click({200.0f, 120.0f});
    CHECK(screen.world.wasDoubleClicked(row));
    CHECK(screen.world.wasDoubleClicked("open"));
    CHECK(screen.world.wasClicked(row));

    // Slower, they are two clicks.
    screen.update(UiInput{}, std::chrono::milliseconds(600));
    screen.click({200.0f, 120.0f});
    screen.update(UiInput{}, std::chrono::milliseconds(600));
    screen.click({200.0f, 120.0f});
    CHECK_FALSE(screen.world.wasDoubleClicked(row));
}

TEST_CASE("A tooltip shows once the pointer rests on its element", "[ui][controls]")
{
    Screen screen;
    const Entity run = screen.button({}, {100.0f, 100.0f}, {300.0f, 140.0f}, "run");
    screen.scene.get<UiButton>(run).interactable = false;
    screen.scene.add<devex::scene::UiTooltip>(run, devex::scene::UiTooltip{.text = "Build first", .delay = 0.5f});
    const auto tooltipDrawn = [&] {
        devex::render::RenderWorld drawn;
        screen.world.build(screen.scene, devex::ui::DrawContext{}, drawn);
        for (const devex::render::UiDraw& draw : drawn.uiDraws)
        {
            if (draw.kind == devex::render::UiDrawKind::RoundedQuad && draw.rect.x > 200.0f)
            {
                return true;
            }
        }
        return false;
    };
    screen.update(UiInput{.pointer = {200.0f, 120.0f}});
    screen.update(UiInput{.pointer = {200.0f, 120.0f}}, std::chrono::milliseconds(300));
    CHECK_FALSE(tooltipDrawn());
    screen.update(UiInput{.pointer = {200.0f, 120.0f}}, std::chrono::milliseconds(300));
    CHECK(tooltipDrawn());
    // A press hides it.
    screen.press({200.0f, 120.0f});
    CHECK_FALSE(tooltipDrawn());
}

TEST_CASE("A scrollbar shows what scrolls and moves it", "[ui][controls]")
{
    Screen screen;
    const Entity list = screen.element({}, {100.0f, 100.0f}, {500.0f, 300.0f}, "List");
    screen.scene.add<devex::scene::UiScroll>(list);
    screen.element(list, {0.0f, 0.0f}, {380.0f, 800.0f}, "Content");
    screen.update();
    const devex::ui::LaidOutRect* const rect = screen.rectOf(list);
    REQUIRE(rect != nullptr);
    CHECK(rect->content.y == Catch::Approx(800.0f));

    // The thumb is a quarter of the track: dragging it down its whole room reaches the end.
    screen.press({496.0f, 110.0f});
    screen.update(UiInput{.pointer = {496.0f, 110.0f + 150.0f}, .pointerDown = true, .pointerMoved = true});
    CHECK(screen.scene.get<devex::scene::UiScroll>(list).offset.y == Catch::Approx(600.0f));
    screen.release({496.0f, 260.0f});

    // The track moves a page.
    screen.click({496.0f, 120.0f});
    CHECK(screen.scene.get<devex::scene::UiScroll>(list).offset.y == Catch::Approx(400.0f));
}

TEST_CASE("A splitter shares its room between two children and its bar moves", "[ui][controls]")
{
    Screen screen;
    const Entity split = screen.element({}, {0.0f, 0.0f}, {1000.0f, 500.0f}, "Split");
    screen.scene.add<devex::scene::UiSplitter>(split, devex::scene::UiSplitter{.position = 300.0f, .minSize = 100.0f, .barSize = 6.0f});
    const Entity left = screen.element(split, {}, {}, "Left");
    const Entity right = screen.element(split, {}, {}, "Right");
    screen.update();
    CHECK(screen.rectOf(left)->max.x == Catch::Approx(300.0f));
    CHECK(screen.rectOf(right)->min.x == Catch::Approx(306.0f));
    CHECK(screen.rectOf(right)->max.y == Catch::Approx(500.0f));

    screen.press({303.0f, 250.0f});
    screen.update(UiInput{.pointer = {603.0f, 250.0f}, .pointerDown = true, .pointerMoved = true});
    screen.release({603.0f, 250.0f});
    CHECK(screen.scene.get<devex::scene::UiSplitter>(split).position == Catch::Approx(600.0f));
    // Neither side gets smaller than its minimum.
    screen.press({603.0f, 250.0f});
    screen.update(UiInput{.pointer = {990.0f, 250.0f}, .pointerDown = true, .pointerMoved = true});
    CHECK(screen.scene.get<devex::scene::UiSplitter>(split).position == Catch::Approx(894.0f));
}

TEST_CASE("A foldout shows and hides what it holds", "[ui][controls]")
{
    Screen screen;
    const Entity header = screen.element({}, {100.0f, 100.0f}, {500.0f, 130.0f}, "Header");
    const Entity body = screen.element({}, {100.0f, 130.0f}, {500.0f, 300.0f}, "Body");
    screen.scene.add<devex::scene::UiFoldout>(header, devex::scene::UiFoldout{.expanded = false,
                                                                            .content = screen.scene.reference(body),
                                                                            .action = "details"});
    screen.update();
    CHECK_FALSE(screen.scene.get<UiRect>(body).visible);
    screen.click({300.0f, 115.0f});
    CHECK(screen.world.wasChanged("details"));
    CHECK(screen.scene.get<devex::scene::UiFoldout>(header).expanded);
    CHECK(screen.scene.get<UiRect>(body).visible);
}

TEST_CASE("A virtual list places its few rows at the items in view", "[ui][controls]")
{
    Screen screen;
    const Entity list = screen.element({}, {0.0f, 0.0f}, {400.0f, 320.0f}, "Scroll");
    screen.scene.add<devex::scene::UiScroll>(list, devex::scene::UiScroll{.offset = {0.0f, 3200.0f}});
    const Entity items = screen.element(list, {0.0f, 0.0f}, {400.0f, 0.0f}, "Items");
    screen.scene.add<devex::scene::UiVirtualList>(items, devex::scene::UiVirtualList{.itemCount = 1000, .itemSize = 32.0f});
    std::vector<Entity> rows;
    for (int row = 0; row < 11; ++row)
    {
        rows.push_back(screen.element(items, {}, {}, "Row"));
    }
    screen.update();
    CHECK(screen.scene.get<devex::scene::UiVirtualList>(items).first == 100);
    CHECK(screen.rectOf(list)->content.y == Catch::Approx(32000.0f));
    // The first row shows item 100, where the scroll has brought it: at the top of the list.
    CHECK(screen.rectOf(rows[0])->min.y == Catch::Approx(0.0f));
    CHECK(screen.rectOf(rows[1])->min.y == Catch::Approx(32.0f));

    // At the end, the rows past the last item hide.
    screen.scene.get<devex::scene::UiScroll>(list).offset.y = 32000.0f - 320.0f;
    screen.update();
    CHECK(screen.scene.get<devex::scene::UiVirtualList>(items).first == 990);
    CHECK(screen.rectOf(rows[9])->visible);
    CHECK_FALSE(screen.rectOf(rows[10])->visible);
}

TEST_CASE("A table lines its cells up in columns, resized and sorted from its header", "[ui][controls]")
{
    Screen screen;
    const Entity table = screen.element({}, {0.0f, 0.0f}, {600.0f, 400.0f}, "Table");
    screen.scene.add<devex::scene::UiTable>(table, devex::scene::UiTable{.columns = {200.0f, 100.0f}, .action = "sort"});
    const Entity header = screen.element(table, {0.0f, 0.0f}, {600.0f, 30.0f}, "Header");
    screen.scene.add<devex::scene::UiTableRow>(header, devex::scene::UiTableRow{.header = true});
    const Entity name = screen.element(header, {}, {}, "Name");
    const Entity size = screen.element(header, {}, {}, "Size");
    const Entity row = screen.element(table, {0.0f, 30.0f}, {600.0f, 60.0f}, "Row");
    screen.scene.add<devex::scene::UiTableRow>(row);
    screen.element(row, {}, {}, "Cell");
    const Entity cell = screen.element(row, {}, {}, "Cell");
    screen.update();
    CHECK(screen.rectOf(size)->min.x == Catch::Approx(200.0f));
    CHECK(screen.rectOf(cell)->min.x == Catch::Approx(200.0f));
    CHECK(screen.rectOf(cell)->max.x == Catch::Approx(300.0f));

    // The edge of the first header cell resizes its column, and the rows follow.
    screen.press({199.0f, 15.0f});
    screen.update(UiInput{.pointer = {249.0f, 15.0f}, .pointerDown = true, .pointerMoved = true});
    screen.release({249.0f, 15.0f});
    CHECK(screen.scene.get<devex::scene::UiTable>(table).columns[0] == Catch::Approx(250.0f));
    screen.update();
    CHECK(screen.rectOf(cell)->min.x == Catch::Approx(250.0f));

    // A click on a header cell sorts by its column, then the other way.
    screen.click({100.0f, 15.0f});
    CHECK(screen.world.wasChanged("sort"));
    CHECK(screen.scene.get<devex::scene::UiTable>(table).sortColumn == 0);
    CHECK(screen.scene.get<devex::scene::UiTable>(table).sortAscending);
    screen.click({100.0f, 15.0f});
    CHECK_FALSE(screen.scene.get<devex::scene::UiTable>(table).sortAscending);
    static_cast<void>(name);
}
