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

TEST_CASE("A drag source carried to a target that accepts it is dropped there", "[ui][controls][drag]")
{
    Screen screen;
    const Entity gem = screen.button({}, {100.0f, 100.0f}, {200.0f, 140.0f}, "pick");
    screen.scene.add<devex::scene::UiDragSource>(gem, devex::scene::UiDragSource{.type = "item", .data = "gem"});
    const Entity slot = screen.element({}, {400.0f, 100.0f}, {500.0f, 200.0f}, "Slot");
    screen.scene.add<UiImage>(slot);
    screen.scene.add<devex::scene::UiDropTarget>(slot, devex::scene::UiDropTarget{.accepts = {"item"}, .action = "slot"});
    const Entity other = screen.element({}, {600.0f, 100.0f}, {700.0f, 200.0f}, "Other");
    screen.scene.add<UiImage>(other);
    screen.scene.add<devex::scene::UiDropTarget>(other, devex::scene::UiDropTarget{.accepts = {"spell"}});
    screen.update();

    const auto drag = [&](Vec2 point) { screen.update(UiInput{.pointer = point, .pointerDown = true, .pointerMoved = true}); };
    screen.press({150.0f, 120.0f});
    // A shake of the hand is still a click.
    drag({153.0f, 121.0f});
    CHECK(screen.world.carried() == nullptr);
    drag({300.0f, 150.0f});
    REQUIRE(screen.world.carried() != nullptr);
    CHECK(screen.world.carried()->source == gem);
    CHECK(screen.world.carried()->type == "item");

    // Only the targets that accept what is carried take it.
    drag({650.0f, 150.0f});
    CHECK_FALSE(screen.world.dropTarget().isValid());
    drag({450.0f, 150.0f});
    CHECK(screen.world.dropTarget() == slot);

    screen.release({450.0f, 150.0f});
    CHECK(screen.world.wasDropped("slot"));
    CHECK(screen.world.wasDropped(slot));
    REQUIRE(screen.world.dropped() != nullptr);
    CHECK(screen.world.dropped()->source == gem);
    CHECK(screen.world.dropped()->data == "gem");
    // Where in the target it was let go: halfway across, halfway down.
    CHECK(screen.world.dropped()->at.x == Catch::Approx(0.5f));
    CHECK(screen.world.dropped()->at.y == Catch::Approx(0.5f));
    // The button carried away is not clicked.
    CHECK_FALSE(screen.world.wasClicked("pick"));
    CHECK(screen.world.carried() == nullptr);
    screen.update();
    CHECK_FALSE(screen.world.wasDropped("slot"));
}

TEST_CASE("A drag source that is not moved is clicked, and Escape puts back what is carried", "[ui][controls][drag]")
{
    Screen screen;
    const Entity gem = screen.button({}, {100.0f, 100.0f}, {200.0f, 140.0f}, "pick");
    screen.scene.add<devex::scene::UiDragSource>(gem, devex::scene::UiDragSource{.type = "item"});
    const Entity slot = screen.element({}, {400.0f, 100.0f}, {500.0f, 200.0f}, "Slot");
    screen.scene.add<UiImage>(slot);
    screen.scene.add<devex::scene::UiDropTarget>(slot, devex::scene::UiDropTarget{.accepts = {"item"}, .action = "slot"});
    screen.update();

    screen.click({150.0f, 120.0f});
    CHECK(screen.world.wasClicked("pick"));
    CHECK(screen.world.carried() == nullptr);

    screen.press({150.0f, 120.0f});
    screen.update(UiInput{.pointer = {450.0f, 150.0f}, .pointerDown = true, .pointerMoved = true});
    REQUIRE(screen.world.carried() != nullptr);
    screen.update(UiInput{.pointer = {450.0f, 150.0f}, .pointerDown = true, .cancelPressed = true});
    CHECK(screen.world.carried() == nullptr);
    CHECK_FALSE(screen.world.wasCancelled());
    screen.release({450.0f, 150.0f});
    CHECK_FALSE(screen.world.wasDropped("slot"));
    CHECK_FALSE(screen.world.wasClicked("pick"));
}

TEST_CASE("A drag from outside the interface is dropped on a target that accepts its type", "[ui][controls][drag]")
{
    Screen screen;
    const Entity folder = screen.button({}, {100.0f, 100.0f}, {300.0f, 140.0f}, "folder");
    screen.scene.add<devex::scene::UiDropTarget>(folder, devex::scene::UiDropTarget{.accepts = {"entity"}});
    screen.update();

    screen.world.carryFromOutside("entity", "e1");
    screen.update(UiInput{.pointer = {200.0f, 120.0f}, .pointerDown = true, .pointerMoved = true});
    REQUIRE(screen.world.carried() != nullptr);
    CHECK_FALSE(screen.world.carried()->source.isValid());
    CHECK(screen.world.dropTarget() == folder);

    screen.world.carryFromOutside("entity", "e1");
    screen.release({200.0f, 120.0f});
    CHECK(screen.world.wasDropped(folder));
    REQUIRE(screen.world.dropped() != nullptr);
    CHECK_FALSE(screen.world.dropped()->source.isValid());
    CHECK(screen.world.dropped()->data == "e1");
    CHECK_FALSE(screen.world.wasClicked("folder"));

    // Announced once more after the release, as a drag from another panel may be, it drops nothing.
    screen.world.carryFromOutside("entity", "e1");
    screen.update(UiInput{.pointer = {200.0f, 120.0f}});
    CHECK_FALSE(screen.world.wasDropped(folder));
    CHECK(screen.world.carried() == nullptr);

    // A drag that is no longer announced has ended.
    screen.update(UiInput{.pointer = {200.0f, 120.0f}, .pointerDown = true});
    CHECK(screen.world.carried() == nullptr);
}

namespace {

// A number field as an inspector has it: a box that shows the number and takes what is typed.
Entity numberField(Screen& screen, Entity parent, Vec2 min, Vec2 max, devex::scene::UiNumberField number)
{
    const Entity entity = screen.element(parent, min, max, "Number");
    screen.scene.add<UiImage>(entity);
    screen.scene.add<UiText>(entity, UiText{.text = ""});
    screen.scene.add<devex::scene::UiInput>(entity);
    screen.scene.add<devex::scene::UiNumberField>(entity, number);
    return entity;
}

} // namespace

TEST_CASE("A number field is dragged sideways, and typed into once clicked", "[ui][controls][number]")
{
    Screen screen;
    const Entity field = numberField(screen, {}, {100.0f, 100.0f}, {300.0f, 130.0f},
                                     {.value = 1.0f, .dragSpeed = 0.1f, .decimals = 2, .format = "{} m", .action = "speed"});
    screen.update();
    CHECK(screen.scene.get<UiText>(field).text == "1 m");
    const auto& number = screen.scene.get<devex::scene::UiNumberField>(field);

    // A press that shakes a little changes nothing; then the value follows the pointer across.
    screen.press({150.0f, 115.0f});
    screen.update(UiInput{.pointer = {152.0f, 116.0f}, .pointerDown = true, .pointerMoved = true});
    CHECK(number.value == Catch::Approx(1.0f));
    screen.update(UiInput{.pointer = {160.0f, 115.0f}, .pointerDown = true, .pointerMoved = true});
    CHECK(number.value == Catch::Approx(1.0f));
    CHECK(screen.world.held() == field);
    screen.update(UiInput{.pointer = {170.0f, 115.0f}, .pointerDown = true, .pointerMoved = true});
    CHECK(number.value == Catch::Approx(2.0f));
    CHECK(screen.world.wasChanged("speed"));
    CHECK(screen.scene.get<UiText>(field).text == "2 m");
    // Shift drags ten times finer.
    screen.update(UiInput{.pointer = {180.0f, 115.0f}, .pointerDown = true, .pointerMoved = true, .selecting = true});
    CHECK(number.value == Catch::Approx(2.1f));
    // Let go after a drag, nothing is typed into.
    screen.release({180.0f, 115.0f});
    CHECK_FALSE(screen.world.isEditing());
    CHECK_FALSE(screen.world.held().isValid());

    // A click types into it: the number alone, selected, which a sum replaces.
    screen.click({150.0f, 115.0f});
    CHECK(screen.world.editedField() == field);
    CHECK(screen.scene.get<UiText>(field).text == "2.1");
    screen.update(UiInput{.typed = "2*3+1"});
    CHECK(number.value == Catch::Approx(2.1f));
    screen.update(UiInput{.submitPressed = true});
    CHECK(number.value == Catch::Approx(7.0f));
    CHECK(screen.world.wasChanged(field));
    CHECK(screen.scene.get<UiText>(field).text == "7 m");

    // Escape leaves the value as it was; what the format writes may be typed as well.
    screen.click({150.0f, 115.0f});
    screen.update(UiInput{.typed = "50"});
    screen.update(UiInput{.cancelPressed = true});
    CHECK(number.value == Catch::Approx(7.0f));
    CHECK(screen.scene.get<UiText>(field).text == "7 m");
    screen.click({150.0f, 115.0f});
    screen.update(UiInput{.typed = "3,5 m"});
    // A click elsewhere ends the edit as Enter does.
    screen.click({900.0f, 900.0f});
    CHECK(number.value == Catch::Approx(3.5f));
}

TEST_CASE("A number field keeps its bounds, its steps and the digits it does not show", "[ui][controls][number]")
{
    Screen screen;
    const Entity field =
        numberField(screen, {}, {100.0f, 100.0f}, {300.0f, 130.0f}, {.value = 4.0f, .minValue = 0.0f, .maxValue = 10.0f, .step = 0.5f});
    const Entity fine = numberField(screen, {}, {100.0f, 200.0f}, {300.0f, 230.0f}, {.value = 0.123456f, .decimals = 3});
    screen.update();
    const auto& number = screen.scene.get<devex::scene::UiNumberField>(field);

    screen.click({150.0f, 115.0f});
    screen.update(UiInput{.typed = "99"});
    screen.update(UiInput{.submitPressed = true});
    CHECK(number.value == Catch::Approx(10.0f));
    screen.click({150.0f, 115.0f});
    screen.update(UiInput{.typed = "2.3"});
    screen.update(UiInput{.submitPressed = true});
    CHECK(number.value == Catch::Approx(2.5f));
    // What cannot be read changes nothing.
    screen.click({150.0f, 115.0f});
    screen.update(UiInput{.typed = "two"});
    screen.update(UiInput{.submitPressed = true});
    CHECK(number.value == Catch::Approx(2.5f));

    // Shown with three digits, the value keeps its own when Enter changes nothing.
    CHECK(screen.scene.get<UiText>(fine).text == "0.123");
    screen.click({150.0f, 215.0f});
    screen.update(UiInput{.submitPressed = true});
    CHECK(screen.scene.get<devex::scene::UiNumberField>(fine).value == Catch::Approx(0.123456f));
    CHECK_FALSE(screen.world.wasChanged(fine));

    // A format without {} shows as it is, as a dash for values that differ.
    screen.scene.get<devex::scene::UiNumberField>(fine).format = "-";
    screen.update();
    CHECK(screen.scene.get<UiText>(fine).text == "-");
}

TEST_CASE("A number field clicked in a menu leaves the menu open", "[ui][controls][number]")
{
    Screen screen;
    const Entity menu = screen.element({}, {100.0f, 100.0f}, {400.0f, 300.0f}, "Menu");
    screen.scene.add<devex::scene::UiPopup>(menu);
    const Entity field = numberField(screen, menu, {10.0f, 10.0f}, {200.0f, 40.0f}, {.value = 1.0f});
    screen.world.openPopup(screen.scene, menu);
    screen.update();
    screen.click({150.0f, 125.0f});
    CHECK(screen.world.isPopupOpen(screen.scene, menu));
    CHECK(screen.world.editedField() == field);
}

TEST_CASE("Numbers are written short and read from sums", "[ui][controls][number]")
{
    using devex::ui::evaluateNumber;
    using devex::ui::formatNumber;
    CHECK(formatNumber(1.5, 3) == "1.5");
    CHECK(formatNumber(2.0, 3) == "2");
    CHECK(formatNumber(-0.0001, 3) == "0");
    CHECK(formatNumber(0.126, 2) == "0.13");
    CHECK(formatNumber(1234.5678, 0) == "1235");
    CHECK(evaluateNumber("  42 ") == Catch::Approx(42.0));
    CHECK(evaluateNumber("2*3+1") == Catch::Approx(7.0));
    CHECK(evaluateNumber("-(1 + 2) / 4") == Catch::Approx(-0.75));
    CHECK(evaluateNumber("1,5") == Catch::Approx(1.5));
    CHECK(evaluateNumber("1e3") == Catch::Approx(1000.0));
    CHECK(evaluateNumber("2 - -1") == Catch::Approx(3.0));
    CHECK_FALSE(evaluateNumber("").has_value());
    CHECK_FALSE(evaluateNumber("1/0").has_value());
    CHECK_FALSE(evaluateNumber("(1").has_value());
    CHECK_FALSE(evaluateNumber("1 2").has_value());
    CHECK_FALSE(evaluateNumber("abc").has_value());
}

TEST_CASE("A colour picker is dragged in its square, its hue and its opacity", "[ui][controls][color]")
{
    Screen screen;
    const Entity picker = screen.element({}, {100.0f, 100.0f}, {300.0f, 300.0f}, "Picker");
    screen.scene.add<devex::scene::UiColorPicker>(
        picker, devex::scene::UiColorPicker{.color = {1.0f, 1.0f, 1.0f, 1.0f}, .barSize = 20.0f, .spacing = 10.0f, .action = "tint"});
    screen.update();
    const auto& chosen = screen.scene.get<devex::scene::UiColorPicker>(picker);

    // The hue of a white changes what the square shows, not the colour.
    screen.press({290.0f, 100.0f + 170.0f / 3.0f});
    CHECK(screen.world.held() == picker);
    CHECK_FALSE(screen.world.wasChanged("tint"));
    screen.release({290.0f, 100.0f + 170.0f / 3.0f});
    devex::math::Vec3 hsv{0.0f};
    REQUIRE(screen.world.pickerHsv(screen.scene, picker, hsv));
    CHECK(hsv.x == Catch::Approx(1.0f / 3.0f).margin(0.01f));

    // The top right corner of the square is the hue itself: green.
    screen.press({270.0f, 100.0f});
    CHECK(screen.world.wasChanged("tint"));
    CHECK(chosen.color.x == Catch::Approx(0.0f).margin(0.01f));
    CHECK(chosen.color.y == Catch::Approx(1.0f).margin(0.01f));
    CHECK(chosen.color.z == Catch::Approx(0.0f).margin(0.01f));
    // Dragged out of the square, it stays at its edge: black at the bottom.
    screen.update(UiInput{.pointer = {400.0f, 500.0f}, .pointerDown = true, .pointerMoved = true});
    CHECK(chosen.color.y == Catch::Approx(0.0f).margin(0.001f));
    screen.release({400.0f, 500.0f});

    // A black keeps the hue it was chosen with.
    REQUIRE(screen.world.pickerHsv(screen.scene, picker, hsv));
    CHECK(hsv.x == Catch::Approx(1.0f / 3.0f).margin(0.01f));
    CHECK(hsv.y == Catch::Approx(1.0f).margin(0.01f));

    // The bar under them sets the opacity alone.
    screen.click({200.0f, 290.0f});
    CHECK(chosen.color.w == Catch::Approx(0.5f).margin(0.01f));

    // A colour set from outside is shown, and one brighter than white keeps its intensity.
    screen.scene.get<devex::scene::UiColorPicker>(picker).color = {2.0f, 0.0f, 0.0f, 1.0f};
    screen.update();
    screen.press({270.0f, 100.0f});
    CHECK(chosen.color.x == Catch::Approx(2.0f).margin(0.01f));
    screen.release({270.0f, 100.0f});
}

TEST_CASE("A dropdown with no option chosen shows its placeholder", "[ui][controls]")
{
    Screen screen;
    const Entity dropdown = screen.element({}, {100.0f, 100.0f}, {300.0f, 130.0f}, "Dropdown");
    screen.scene.add<UiImage>(dropdown);
    screen.scene.add<UiText>(dropdown);
    screen.scene.add<devex::scene::UiDropdown>(
        dropdown, devex::scene::UiDropdown{.options = {"Easy", "Hard"}, .selected = -1, .placeholder = "Choose"});
    screen.update();
    CHECK(screen.scene.get<UiText>(dropdown).text == "Choose");
    screen.scene.get<devex::scene::UiDropdown>(dropdown).selected = 1;
    screen.update();
    CHECK(screen.scene.get<UiText>(dropdown).text == "Hard");
}
