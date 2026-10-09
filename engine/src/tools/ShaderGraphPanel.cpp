// The Shader Graph panel, made with the interface of the engine as the Animator panel is: the nodes of
// one function of a shader graph at a time, each with its inputs at its left and its outputs at its
// right, and the links between them drawn as curves. The page the inspector shows for a node is at
// the end.
#include "InspectorUi.hpp"
#include "SettingsUi.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/ShaderGraphFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace devex::tools::detail {
namespace {

using asset::ShaderFunction;
using asset::ShaderGraphData;
using asset::ShaderGraphLink;
using asset::ShaderGraphNode;
using asset::ShaderGraphPort;
using asset::ShaderParameterType;
using scene::Entity;
using Button = PanelButton;

constexpr std::uint32_t shaderGraphSurface = 25;
// The sizes of nodes in the graph, in units that are pixels at a zoom of 1 with a font of 16 pixels.
constexpr float nodeWidth = 190.0f;
constexpr float headerHeight = 24.0f;
constexpr float rowHeight = 20.0f;
constexpr float portRadius = 5.0f;
constexpr std::size_t maxUndo = 200;
// Points a link is drawn with, as a curve.
constexpr std::size_t curvePoints = 20;

[[nodiscard]] std::filesystem::file_time_type writeTime(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::file_time_type time = std::filesystem::last_write_time(file, error);
    return error ? std::filesystem::file_time_type{} : time;
}

// Keeps the values and the links of a graph to the ports its nodes have, once a setting changed them.
void normalize(ShaderGraphData& graph)
{
    for (ShaderGraphNode& node : graph.nodes)
    {
        const std::vector<ShaderGraphPort> ports = asset::inputsOf(node, graph.kind);
        for (std::size_t port = node.inputs.size(); port < ports.size(); ++port)
        {
            node.inputs.push_back(ports[port].fallback);
        }
        node.inputs.resize(ports.size());
    }
    std::erase_if(graph.links, [&](const ShaderGraphLink& link) {
        const ShaderGraphNode* const from = graph.find(link.fromNode);
        const ShaderGraphNode* const to = graph.find(link.toNode);
        return from == nullptr || to == nullptr || link.fromPort >= asset::outputsOf(*from, graph.kind).size() ||
               link.toPort >= asset::inputsOf(*to, graph.kind).size();
    });
}

// Reads the errors and warnings of the last import of the graph, once it ends.
void refreshDiagnostics(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    const std::optional<asset::SourceFile> source = state.database->sourceOf(editor.asset);
    const std::int32_t status = source ? static_cast<std::int32_t>(source->status) : -1;
    if (status == editor.importStatus)
    {
        return;
    }
    editor.importStatus = status;
    editor.diagnostics.clear();
    if (const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(editor.asset))
    {
        if (core::Result<asset::ShaderData> shader = asset::decodeShader(*bytes))
        {
            editor.diagnostics = std::move(shader->diagnostics);
        }
    }
}

void writeShaderGraph(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    if (core::Result<void> written = core::writeTextFile(editor.file, asset::writeShaderGraphFile(editor.graph)); !written)
    {
        editor.error = std::format("Not saved: {}", written.error().message);
        return;
    }
    editor.saved = editor.graph;
    editor.fileTime = writeTime(editor.file);
    editor.error.clear();
    if (core::Result<void> queued = state.database->reimport(editor.asset); !queued)
    {
        DEVEX_LOG_WARNING("{}", queued.error());
    }
}

void undoShaderGraph(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    if (editor.undo.empty())
    {
        return;
    }
    editor.redo.push_back(editor.graph);
    editor.graph = std::move(editor.undo.back());
    editor.undo.pop_back();
    writeShaderGraph(state);
    if (editor.graph.find(editor.selected) == nullptr)
    {
        editor.selected = 0;
        editor.inspecting = false;
    }
}

void redoShaderGraph(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    if (editor.redo.empty())
    {
        return;
    }
    editor.undo.push_back(editor.graph);
    editor.graph = std::move(editor.redo.back());
    editor.redo.pop_back();
    writeShaderGraph(state);
    if (editor.graph.find(editor.selected) == nullptr)
    {
        editor.selected = 0;
        editor.inspecting = false;
    }
}

void selectNode(ToolsState& state, std::uint32_t id)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    editor.selected = id;
    // The inspector shows it, until another entity or asset is chosen.
    editor.inspecting = id != 0;
    editor.inspectedEntity = state.selection.active();
    editor.inspectedAsset = state.selectedAsset;
}

void deleteSelected(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    const ShaderGraphNode* const node = editor.graph.find(editor.selected);
    // The outputs stay: a graph keeps one for each of its functions.
    if (node == nullptr || node->type == "output")
    {
        return;
    }
    editor.graph.removeNode(editor.selected);
    selectNode(state, 0);
    commitShaderGraph(state);
}

void duplicateSelected(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    const ShaderGraphNode* const node = editor.graph.find(editor.selected);
    if (node == nullptr || node->type == "output")
    {
        return;
    }
    ShaderGraphNode copy = *node;
    copy.id = editor.graph.nextId();
    copy.position += math::Vec2{30.0f, 30.0f};
    if (copy.type == "parameter")
    {
        copy.setText("name", std::format("{}_{}", copy.text("name"), copy.id));
    }
    editor.graph.nodes.push_back(std::move(copy));
    selectNode(state, editor.graph.nodes.back().id);
    commitShaderGraph(state);
}

// ---- The graph ----

struct Graph
{
    math::Vec2 origin{0.0f};
    float scale = 1.0f;

    [[nodiscard]] math::Vec2 toArea(math::Vec2 point) const noexcept
    {
        return origin + point * scale;
    }

    [[nodiscard]] math::Vec2 toGraph(math::Vec2 point) const noexcept
    {
        return (point - origin) / scale;
    }
};

// A node as the graph lays it out, in units of the graph.
struct NodeBox
{
    std::uint32_t id = 0;
    math::Vec2 min{0.0f};
    math::Vec2 max{0.0f};
    std::vector<ShaderGraphPort> inputs;
    std::vector<ShaderGraphPort> outputs;

    [[nodiscard]] math::Vec2 input(std::size_t port) const noexcept
    {
        return {min.x, min.y + headerHeight + rowHeight * (static_cast<float>(port) + 0.5f)};
    }

    [[nodiscard]] math::Vec2 output(std::size_t port) const noexcept
    {
        return {max.x, min.y + headerHeight + rowHeight * (static_cast<float>(port) + 0.5f)};
    }

    [[nodiscard]] bool contains(math::Vec2 point) const noexcept
    {
        return point.x >= min.x && point.y >= min.y && point.x <= max.x && point.y <= max.y;
    }
};

[[nodiscard]] std::vector<NodeBox> boxesOf(const ShaderGraphData& graph, ShaderFunction function)
{
    std::vector<NodeBox> boxes;
    for (const ShaderGraphNode& node : graph.nodes)
    {
        if (node.function != function)
        {
            continue;
        }
        NodeBox& box = boxes.emplace_back();
        box.id = node.id;
        box.inputs = asset::inputsOf(node, graph.kind);
        box.outputs = asset::outputsOf(node, graph.kind);
        const float rows = static_cast<float>(std::max<std::size_t>(std::max(box.inputs.size(), box.outputs.size()), 1));
        box.min = node.position;
        box.max = node.position + math::Vec2{nodeWidth, headerHeight + rows * rowHeight + 6.0f};
    }
    return boxes;
}

// A port under the pointer.
struct PortSpot
{
    std::uint32_t node = 0;
    std::uint32_t port = 0;
    bool output = false;
};

[[nodiscard]] math::Vec4 portColor(ShaderParameterType type) noexcept
{
    switch (type)
    {
    case ShaderParameterType::Float:
    case ShaderParameterType::Int:
        return {0.55f, 0.75f, 0.95f, 1.0f};
    case ShaderParameterType::Bool:
        return {0.55f, 0.85f, 0.45f, 1.0f};
    case ShaderParameterType::Float2:
        return {0.45f, 0.85f, 0.8f, 1.0f};
    case ShaderParameterType::Float3:
        return {0.78f, 0.58f, 0.98f, 1.0f};
    case ShaderParameterType::Float4:
        return {0.95f, 0.6f, 0.8f, 1.0f};
    case ShaderParameterType::Texture:
        return {0.98f, 0.7f, 0.3f, 1.0f};
    }
    return {1.0f, 1.0f, 1.0f, 1.0f};
}

[[nodiscard]] math::Vec4 categoryColor(std::string_view category) noexcept
{
    if (category == "Output")
    {
        return {0.62f, 0.2f, 0.24f, 1.0f};
    }
    if (category == "Input")
    {
        return {0.62f, 0.38f, 0.18f, 1.0f};
    }
    if (category == "Constant")
    {
        return {0.24f, 0.38f, 0.6f, 1.0f};
    }
    if (category == "Parameter")
    {
        return {0.2f, 0.48f, 0.34f, 1.0f};
    }
    if (category == "Texture")
    {
        return {0.58f, 0.42f, 0.16f, 1.0f};
    }
    if (category == "Special")
    {
        return {0.4f, 0.4f, 0.44f, 1.0f};
    }
    return {0.36f, 0.3f, 0.56f, 1.0f};
}

// What an input that no link feeds holds, beside its name.
[[nodiscard]] std::string valueText(const ShaderGraphPort& port, math::Vec4 value)
{
    if (!port.builtin.empty())
    {
        return port.builtin;
    }
    switch (port.type)
    {
    case ShaderParameterType::Float:
        return std::format("{:g}", value.x);
    case ShaderParameterType::Int:
        return std::format("{}", static_cast<int>(std::round(value.x)));
    case ShaderParameterType::Bool:
        return value.x != 0.0f ? "true" : "false";
    case ShaderParameterType::Float2:
        return std::format("{:g}, {:g}", value.x, value.y);
    case ShaderParameterType::Float3:
        return std::format("{:g}, {:g}, {:g}", value.x, value.y, value.z);
    case ShaderParameterType::Float4:
        return std::format("{:g}, {:g}, {:g}, {:g}", value.x, value.y, value.z, value.w);
    case ShaderParameterType::Texture:
        break;
    }
    return {};
}

// The points of a link from an output to an input, as a curve that leaves and arrives level.
[[nodiscard]] std::vector<math::Vec2> curve(math::Vec2 from, math::Vec2 to, float scale)
{
    const float reach = std::max(std::abs(to.x - from.x) * 0.5f, 40.0f * scale);
    const math::Vec2 a = from + math::Vec2{reach, 0.0f};
    const math::Vec2 b = to - math::Vec2{reach, 0.0f};
    std::vector<math::Vec2> points;
    points.reserve(curvePoints + 1);
    for (std::size_t index = 0; index <= curvePoints; ++index)
    {
        const float t = static_cast<float>(index) / static_cast<float>(curvePoints);
        const float u = 1.0f - t;
        points.push_back(from * (u * u * u) + a * (3.0f * u * u * t) + b * (3.0f * u * t * t) + to * (t * t * t));
    }
    return points;
}

[[nodiscard]] std::string_view functionLabel(ShaderFunction function) noexcept
{
    switch (function)
    {
    case ShaderFunction::Vertex:
        return "Vertex";
    case ShaderFunction::Fragment:
        return "Fragment";
    case ShaderFunction::Light:
        return "Light";
    case ShaderFunction::Sky:
        return "Sky";
    }
    return "?";
}

constexpr std::array<std::string_view, 9> categories{"Input",  "Constant",      "Parameter", "Math",   "Vector",
                                                     "Interpolation", "Texture", "Special", "Output"};

} // namespace

// ---- The file ----

bool isShaderGraph(const ToolsState& state, asset::AssetId id)
{
    if (state.database == nullptr || !id.isValid())
    {
        return false;
    }
    const std::optional<asset::SourceFile> source = state.database->sourceOf(id);
    return source && source->path.ends_with(asset::shaderGraphExtension);
}

void loadShaderGraph(ToolsState& state, asset::AssetId id)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    const std::optional<asset::SourceFile> source = state.database->sourceOf(id);
    const std::optional<std::filesystem::path> file = source ? state.database->project().absolutePath(source->path) : std::nullopt;
    if (!file)
    {
        return;
    }
    const std::filesystem::file_time_type time = writeTime(*file);
    const bool sameAsset = editor.asset == id;
    if ((sameAsset && editor.file == *file && editor.fileTime == time) || (sameAsset && editor.dragged != 0))
    {
        return;
    }
    editor.asset = id;
    editor.file = *file;
    editor.fileTime = time;
    editor.error.clear();
    const core::Result<std::string> text = core::readTextFile(*file);
    core::Result<ShaderGraphData> graph =
        text ? asset::parseShaderGraphFile(*text) : core::Result<ShaderGraphData>(std::unexpected(text.error()));
    if (!graph)
    {
        editor.error = std::format("The file could not be read: {}", graph.error().message);
        editor.graph = {};
    }
    else
    {
        editor.graph = std::move(*graph);
        normalize(editor.graph);
    }
    editor.saved = editor.graph;
    if (!sameAsset)
    {
        editor.undo.clear();
        editor.redo.clear();
        editor.selected = 0;
        editor.inspecting = false;
        editor.linkingNode = 0;
        editor.importStatus = -2;
        editor.frame = true;
        const std::span<const ShaderFunction> functions = asset::functionsOf(editor.graph.kind);
        if (std::ranges::find(functions, editor.function) == functions.end())
        {
            editor.function = functions.size() > 1 ? functions[1] : functions.front();
        }
    }
}

void commitShaderGraph(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    normalize(editor.graph);
    if (editor.graph == editor.saved || editor.file.empty())
    {
        return;
    }
    editor.undo.push_back(editor.saved);
    if (editor.undo.size() > maxUndo)
    {
        editor.undo.erase(editor.undo.begin());
    }
    editor.redo.clear();
    writeShaderGraph(state);
}

void showShaderGraphCode(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    if (state.database == nullptr || editor.file.empty())
    {
        return;
    }
    // Beside the cache of the project, out of its assets: it is read, never imported.
    const std::filesystem::path folder = state.database->project().root / ".devex" / "generated";
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path file = folder / (core::toUtf8(editor.file.stem()) + std::string(asset::shaderExtension));
    const asset::GeneratedShader generated = asset::generateShaderGraph(editor.graph, core::toUtf8(editor.file.filename()));
    const std::string text = std::format("// Read only: changing this file changes nothing. Edit {} in the Shader Graph panel.\n{}",
                                         core::toUtf8(editor.file.filename()), generated.code);
    if (core::Result<void> written = core::writeTextFile(file, text); !written)
    {
        DEVEX_LOG_WARNING("Cannot write the code of the graph: {}", written.error());
        return;
    }
    openTextFile(state, file);
}

core::Result<std::filesystem::path> createShaderGraphFile(ToolsState& state, std::string_view folder, std::string_view name,
                                                          asset::ShaderKind kind)
{
    // A new graph opens in the panel, which shows it once it is imported and selected.
    core::Result<std::filesystem::path> created = writeNewAssetFile(state, folder, name, "Shader Graph", asset::shaderGraphExtension,
                                                                    asset::writeShaderGraphFile(asset::makeShaderGraph(kind)));
    if (created)
    {
        state.showShaderGraph = true;
        state.focusShaderGraph = true;
    }
    return created;
}

// ---- The panel ----

using scene::UiRect;
using namespace rects;

struct ShaderGraphUi : FormUi
{
    ShaderGraphUi()
        : FormUi(shaderGraphSurface)
    {
    }

    struct PortView
    {
        Entity dot;
        Entity label;
    };
    // A node as it shows: its frame, its inside, its title on the band of its category, its ports.
    struct NodeView
    {
        Entity border;
        Entity fill;
        Entity band;
        Entity title;
        std::vector<PortView> inputs;
        std::vector<PortView> outputs;
    };

    bool built = false;
    float builtFont = 0.0f;

    Entity toolbar;
    Entity glyph;
    Entity title;
    std::array<Button, 3> functionTabs{};
    Button undo;
    Button redo;
    Button frame;
    Button code;
    Entity hint;
    Entity message;

    Entity graph;
    Entity gridLayer;
    Entity linksLayer;
    Entity nodesLayer;
    Entity drawing;
    Entity graphHint;
    std::vector<Entity> gridLines;
    std::vector<Entity> linkViews;
    std::vector<NodeView> nodeViews;

    // The menu of the categories, then that of the nodes of one; the menu of a node.
    Entity addMenu;
    std::array<Button, categories.size()> categoryItems{};
    Entity entriesMenu;
    std::vector<std::pair<std::size_t, Button>> entryItems;
    Entity nodeMenu;
    Button duplicateItem;
    Button deleteItem;
    std::uint32_t menuNode = 0;
    math::Vec2 menuPosition{0.0f};
    math::Vec2 menuPointer{0.0f};

    math::Vec2 areaSize{0.0f};
    math::Vec2 pointer{0.0f};
    bool pointed = false;
    bool grabbed = false;

    [[nodiscard]] float barHeight() const noexcept
    {
        return std::round(font * 2.5f);
    }
    [[nodiscard]] float graphUnit() const noexcept
    {
        return regularFontPixels(font) / 16.0f;
    }
    [[nodiscard]] bool menuOpen()
    {
        ui::UiWorld& world = panel.world();
        return world.isPopupOpen(scene(), addMenu) || world.isPopupOpen(scene(), entriesMenu) || world.isPopupOpen(scene(), nodeMenu);
    }

    void build(EditorUiKit& kit);
    void say(std::string sentence);
    void showGraph(ToolsState& state);
    void answerGraph(ToolsState& state, EditorUiKit& kit, const ui::LaidOutRect& area, bool menuWasOpen);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

void ShaderGraphUi::build(EditorUiKit& kit)
{
    ui::UiWorld& world = panel.world();
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        world.closePopup(scene(), child);
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    gridLines.clear();
    linkViews.clear();
    nodeViews.clear();
    entryItems.clear();
    panel.setKeyboardNavigation(false);

    const float edge = std::round(font * 0.5f);
    const float tall = std::round(font * 1.85f);
    const float iconSize = std::round(font * 1.2f);
    const float tool = std::round(font * 1.75f);

    // The toolbar: the graph, its functions, undo, the frame, its code, and what the last import said.
    toolbar = add({}, "Toolbar", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {edge, 0.0f}, .offsetMax = {-edge, barHeight()}});
    scene().add<scene::UiLayout>(toolbar, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.5f, .align = scene::TextAlign::Left});
    glyph = icon(kit, toolbar, middle({iconSize, iconSize}), Icon::Workflow, {});
    title = text(toolbar, middle({font * 6.0f, tall}), "", "text", true);
    for (Button& tab : functionTabs)
    {
        tab = button(kit, toolbar, std::nullopt, "Fragment", "button", std::round(font * 6.0f), tall);
    }
    undo = toolButton(kit, toolbar, Icon::Undo, middle({tool, tool}));
    tooltip(undo.entity, "Undo (Ctrl+Z)");
    redo = toolButton(kit, toolbar, Icon::Redo, middle({tool, tool}));
    tooltip(redo.entity, "Redo (Ctrl+Y)");
    frame = toolButton(kit, toolbar, Icon::Scan, middle({tool, tool}));
    tooltip(frame.entity, "Frame the graph");
    code = button(kit, toolbar, Icon::FileCode, "Code", "button", 0.0f, tall);
    tooltip(code.entity, "Show the code the graph turns into, in the text editor");
    hint = text(toolbar, grow(tall), "", "dim");

    message = text({}, whole(math::Vec4{edge}), "", "dim", false, scene::TextAlign::Center);
    scene().get<scene::UiText>(message).wrap = true;

    // The graph: a grid, the links, the nodes over them, and the link being drawn.
    graph = add({}, "Graph", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, barHeight()}, .offsetMax = {0.0f, 0.0f}, .clipChildren = true},
                "window");
    scene().add<scene::UiImage>(graph);
    gridLayer = add(graph, "Grid", whole());
    linksLayer = add(graph, "Links", whole());
    nodesLayer = add(graph, "Nodes", whole());
    drawing = add(graph, "Link drawn", whole());
    scene().add<scene::UiLine>(drawing, scene::UiLine{});
    graphHint = text(graph, whole(), "Right-click to add nodes. Drag from an output to an input to link them.", "dim", false,
                     scene::TextAlign::Center);

    addMenu = menu("Add node", font * 11.0f);
    for (std::size_t index = 0; index < categories.size(); ++index)
    {
        categoryItems[index] = menuItem(kit, addMenu, Icon::ChevronRight, categories[index]);
    }
    fitMenu(addMenu, font * 11.0f);
    entriesMenu = menu("Nodes", font * 14.0f);
    const std::span<const asset::ShaderNodeEntry> catalog = asset::shaderNodeCatalog();
    for (std::size_t index = 0; index < catalog.size(); ++index)
    {
        Button item = menuItem(kit, entriesMenu, std::nullopt, catalog[index].label);
        tooltip(item.entity, std::string(catalog[index].description));
        entryItems.emplace_back(index, item);
    }
    nodeMenu = menu("Node menu", font * 11.0f);
    duplicateItem = menuItem(kit, nodeMenu, Icon::CopyPlus, "Duplicate", "Ctrl+D");
    deleteItem = menuItem(kit, nodeMenu, Icon::Trash, "Delete", "Delete");
    fitMenu(nodeMenu, font * 11.0f);
}

void ShaderGraphUi::say(std::string sentence)
{
    scene().get<UiRect>(message).visible = true;
    scene().get<UiRect>(toolbar).visible = false;
    scene().get<UiRect>(graph).visible = false;
    scene::UiText& shown = scene().get<scene::UiText>(message);
    if (shown.text != sentence)
    {
        shown.text = std::move(sentence);
    }
}

void ShaderGraphUi::showGraph(ToolsState& state)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    const ShaderGraphData& data = editor.graph;
    const ThemeColors& colors = themeColors();
    const float unit = graphUnit();
    const std::vector<NodeBox> boxes = boxesOf(data, editor.function);
    if (editor.frame && areaSize.x > 1.0f && areaSize.y > 1.0f && !boxes.empty())
    {
        math::Vec2 low = boxes.front().min;
        math::Vec2 high = boxes.front().max;
        for (const NodeBox& box : boxes)
        {
            low = math::min(low, box.min);
            high = math::max(high, box.max);
        }
        const math::Vec2 extent = (high - low) + math::Vec2{80.0f, 80.0f};
        editor.zoom = std::clamp(std::min(areaSize.x / (extent.x * unit), areaSize.y / (extent.y * unit)), 0.3f, 1.0f);
        editor.pan = areaSize * 0.5f - (low + high) * 0.5f * (editor.zoom * unit);
        editor.frame = false;
    }
    const Graph view{editor.pan, editor.zoom * unit};

    // The grid, a line in two once they come too close.
    float step = 32.0f * view.scale;
    while (step < 18.0f)
    {
        step *= 2.0f;
    }
    const math::Vec4 gridColor = linearColor(math::Vec4(colors.border.x, colors.border.y, colors.border.z, 0.16f));
    std::size_t linesUsed = 0;
    const auto gridLine = [&](math::Vec2 from, math::Vec2 to) {
        if (linesUsed == gridLines.size())
        {
            const Entity made = add(gridLayer, "Line", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}});
            scene().add<scene::UiImage>(made, scene::UiImage{.raycastTarget = false});
            gridLines.push_back(made);
        }
        const Entity shown = gridLines[linesUsed++];
        UiRect& rect = scene().get<UiRect>(shown);
        rect.visible = true;
        rect.offsetMin = from;
        rect.offsetMax = to;
        scene().get<scene::UiImage>(shown).color = gridColor;
    };
    for (float x = std::fmod(view.origin.x, step); x < areaSize.x && linesUsed < 400; x += step)
    {
        gridLine({std::round(x), 0.0f}, {std::round(x) + 1.0f, areaSize.y});
    }
    for (float y = std::fmod(view.origin.y, step); y < areaSize.y && linesUsed < 400; y += step)
    {
        gridLine({0.0f, std::round(y)}, {areaSize.x, std::round(y) + 1.0f});
    }
    for (std::size_t index = linesUsed; index < gridLines.size(); ++index)
    {
        scene().get<UiRect>(gridLines[index]).visible = false;
    }

    const auto boxOf = [&](std::uint32_t id) -> const NodeBox* {
        const auto found = std::ranges::find(boxes, id, &NodeBox::id);
        return found != boxes.end() ? &*found : nullptr;
    };

    // The links, in the colour of what they carry; those of the node selected brighter.
    std::size_t linksUsed = 0;
    const auto showLink = [&](std::vector<math::Vec2> points, math::Vec4 color, float width) {
        if (linksUsed == linkViews.size())
        {
            const Entity made = add(linksLayer, "Link", whole());
            scene().add<scene::UiLine>(made, scene::UiLine{});
            linkViews.push_back(made);
        }
        const Entity shown = linkViews[linksUsed++];
        scene().get<UiRect>(shown).visible = true;
        scene::UiLine& drawn = scene().get<scene::UiLine>(shown);
        drawn.points = std::move(points);
        drawn.color = linearColor(color);
        drawn.width = width;
    };
    for (const ShaderGraphLink& link : data.links)
    {
        const NodeBox* const from = boxOf(link.fromNode);
        const NodeBox* const to = boxOf(link.toNode);
        if (from == nullptr || to == nullptr || link.fromPort >= from->outputs.size() || link.toPort >= to->inputs.size())
        {
            continue;
        }
        const bool selected = link.fromNode == editor.selected || link.toNode == editor.selected;
        math::Vec4 color = portColor(from->outputs[link.fromPort].type);
        color.w = selected ? 1.0f : 0.75f;
        showLink(curve(view.toArea(from->output(link.fromPort)), view.toArea(to->input(link.toPort)), view.scale), color,
                 (selected ? 2.5f : 1.8f) * view.scale);
    }
    for (std::size_t index = linksUsed; index < linkViews.size(); ++index)
    {
        scene().get<UiRect>(linkViews[index]).visible = false;
    }
    // The link being drawn follows the pointer.
    scene().get<UiRect>(drawing).visible = false;
    if (editor.linkingNode != 0)
    {
        if (const NodeBox* const from = boxOf(editor.linkingNode); from != nullptr && pointed)
        {
            const math::Vec2 anchor = view.toArea(editor.linkingFromOutput ? from->output(editor.linkingPort) : from->input(editor.linkingPort));
            scene().get<UiRect>(drawing).visible = true;
            scene::UiLine& drawn = scene().get<scene::UiLine>(drawing);
            drawn.points = editor.linkingFromOutput ? curve(anchor, pointer, view.scale) : curve(pointer, anchor, view.scale);
            drawn.color = linearColor(colors.accent);
            drawn.width = 2.0f * view.scale;
        }
    }

    // The nodes, each in a view taken from the reserve.
    const float textSize = font * editor.zoom * 0.95f;
    const float rounding = 5.0f * view.scale;
    for (std::size_t index = 0; index < boxes.size(); ++index)
    {
        if (index == nodeViews.size())
        {
            NodeView made;
            made.border = add(nodesLayer, "Node", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}});
            scene().add<scene::UiImage>(made.border, scene::UiImage{.raycastTarget = false});
            made.fill = add(made.border, "Inside", whole());
            scene().add<scene::UiImage>(made.fill, scene::UiImage{.raycastTarget = false});
            made.band = add(made.border, "Band", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}});
            scene().add<scene::UiImage>(made.band, scene::UiImage{.raycastTarget = false});
            made.title = text(made.band, whole(), "", "text", true, scene::TextAlign::Left);
            nodeViews.push_back(std::move(made));
        }
        const NodeBox& box = boxes[index];
        NodeView& shown = nodeViews[index];
        const ShaderGraphNode& node = *data.find(box.id);
        const bool selected = editor.selected == node.id;
        const bool broken = std::ranges::any_of(editor.diagnostics, [&](const asset::ShaderDiagnostic& diagnostic) {
            return diagnostic.node == node.id && diagnostic.error;
        });
        const math::Vec2 low = view.toArea(box.min);
        const math::Vec2 high = view.toArea(box.max);
        const float thickness = std::max((selected || broken ? 2.0f : 1.0f) * view.scale, 1.0f);
        UiRect& frameRect = scene().get<UiRect>(shown.border);
        frameRect.visible = true;
        frameRect.offsetMin = low;
        frameRect.offsetMax = high;
        scene::UiImage& frameImage = scene().get<scene::UiImage>(shown.border);
        frameImage.color = linearColor(broken ? colors.error : selected ? colors.accent : colors.border);
        frameImage.cornerRadius = rounding;
        UiRect& inside = scene().get<UiRect>(shown.fill);
        inside.offsetMin = {thickness, thickness};
        inside.offsetMax = {-thickness, -thickness};
        scene::UiImage& insideImage = scene().get<scene::UiImage>(shown.fill);
        insideImage.color = linearColor(colors.raised);
        insideImage.cornerRadius = std::max(rounding - thickness, 0.0f);
        UiRect& bandRect = scene().get<UiRect>(shown.band);
        bandRect.offsetMin = {thickness, thickness};
        bandRect.offsetMax = {-thickness, headerHeight * view.scale};
        scene::UiImage& bandImage = scene().get<scene::UiImage>(shown.band);
        bandImage.color = linearColor(categoryColor(asset::nodeCategory(node)));
        bandImage.cornerRadius = std::max(rounding - thickness, 0.0f);
        UiRect& titleRect = scene().get<UiRect>(shown.title);
        titleRect.offsetMin = {6.0f * view.scale, 0.0f};
        titleRect.offsetMax = {-4.0f * view.scale, 0.0f};
        scene::UiText& titleText = scene().get<scene::UiText>(shown.title);
        if (std::string name = asset::nodeTitle(node); titleText.text != name)
        {
            titleText.text = std::move(name);
        }
        titleText.size = textSize;

        // The ports: a dot on the edge, its name inside; an input no link feeds says what it holds.
        const auto showPorts = [&](std::vector<PortView>& views, const std::vector<ShaderGraphPort>& ports, bool output) {
            while (views.size() < ports.size())
            {
                PortView made;
                made.dot = add(shown.border, "Port", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}});
                scene().add<scene::UiImage>(made.dot, scene::UiImage{.raycastTarget = false});
                made.label = text(shown.border, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}}, "", "text", false,
                                  output ? scene::TextAlign::Right : scene::TextAlign::Left);
                views.push_back(made);
            }
            for (std::size_t port = 0; port < views.size(); ++port)
            {
                const bool used = port < ports.size();
                scene().get<UiRect>(views[port].dot).visible = used;
                scene().get<UiRect>(views[port].label).visible = used;
                if (!used)
                {
                    continue;
                }
                const math::Vec2 spot = (output ? box.output(port) : box.input(port)) - box.min;
                const float radius = portRadius * view.scale;
                UiRect& dotRect = scene().get<UiRect>(views[port].dot);
                dotRect.offsetMin = spot * view.scale - math::Vec2{radius};
                dotRect.offsetMax = spot * view.scale + math::Vec2{radius};
                scene::UiImage& dotImage = scene().get<scene::UiImage>(views[port].dot);
                dotImage.color = linearColor(portColor(ports[port].type));
                dotImage.cornerRadius = radius;
                UiRect& labelRect = scene().get<UiRect>(views[port].label);
                const float top = spot.y * view.scale - rowHeight * 0.5f * view.scale;
                labelRect.offsetMin = output ? math::Vec2{nodeWidth * 0.5f * view.scale, top} : math::Vec2{10.0f * view.scale, top};
                labelRect.offsetMax = output ? math::Vec2{(nodeWidth - 10.0f) * view.scale, top + rowHeight * view.scale}
                                             : math::Vec2{(nodeWidth - 10.0f) * view.scale, top + rowHeight * view.scale};
                std::string label = ports[port].name;
                if (!output && data.linkInto(node.id, static_cast<std::uint32_t>(port)) == nullptr && node.type != "output")
                {
                    if (const std::string value = valueText(ports[port], node.input(port, ports[port].fallback)); !value.empty())
                    {
                        label += "  " + value;
                    }
                }
                scene::UiText& labelText = scene().get<scene::UiText>(views[port].label);
                if (labelText.text != label)
                {
                    labelText.text = std::move(label);
                }
                labelText.size = textSize * 0.9f;
                scene().get<UiRect>(views[port].label).style = !output && !ports[port].builtin.empty() &&
                                                                       data.linkInto(node.id, static_cast<std::uint32_t>(port)) == nullptr
                                                                   ? "dim"
                                                                   : "text";
            }
        };
        showPorts(shown.inputs, box.inputs, false);
        showPorts(shown.outputs, box.outputs, true);
    }
    for (std::size_t index = boxes.size(); index < nodeViews.size(); ++index)
    {
        scene().get<UiRect>(nodeViews[index].border).visible = false;
    }
    scene().get<UiRect>(graphHint).visible = boxes.size() <= 1;
}

void ShaderGraphUi::answerGraph(ToolsState& state, EditorUiKit& kit, const ui::LaidOutRect& area, bool menuWasOpen)
{
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    ShaderGraphData& data = editor.graph;
    ui::UiWorld& world = panel.world();
    const ui::UiInput& input = panel.input();
    areaSize = area.size();
    const math::Vec2 mouse = input.pointer - area.min;
    pointer = mouse;
    const bool inside = mouse.x >= 0.0f && mouse.y >= 0.0f && mouse.x < areaSize.x && mouse.y < areaSize.y;
    pointed = panel.hovered() && inside;
    const bool hovered = pointed && !menuWasOpen && !menuOpen();
    const Graph view{editor.pan, editor.zoom * graphUnit()};
    const float unitsPerPoint = panel.unitsOf(math::Vec2(1.0f, 0.0f)).x - panel.unitsOf(math::Vec2(0.0f, 0.0f)).x;

    const std::vector<NodeBox> boxes = boxesOf(data, editor.function);
    const math::Vec2 at = view.toGraph(mouse);
    // What the pointer is on: a port first, then a node.
    std::optional<PortSpot> port;
    const NodeBox* hoveredBox = nullptr;
    const float reach = std::max(portRadius * 2.2f, 9.0f / std::max(view.scale, 0.01f));
    for (const NodeBox& box : boxes)
    {
        for (std::size_t index = 0; index < box.inputs.size(); ++index)
        {
            if (math::length(box.input(index) - at) <= reach)
            {
                port = PortSpot{box.id, static_cast<std::uint32_t>(index), false};
            }
        }
        for (std::size_t index = 0; index < box.outputs.size(); ++index)
        {
            if (math::length(box.output(index) - at) <= reach)
            {
                port = PortSpot{box.id, static_cast<std::uint32_t>(index), true};
            }
        }
        hoveredBox = box.contains(at) ? &box : hoveredBox;
    }
    if (!pointed)
    {
        port.reset();
        hoveredBox = nullptr;
    }

    // A press on a port draws a link; on a linked input, it takes its link off to draw it elsewhere.
    if (hovered && input.pointerPressed)
    {
        if (port)
        {
            const ShaderGraphLink* const existing = port->output ? nullptr : data.linkInto(port->node, port->port);
            if (existing != nullptr)
            {
                editor.linkingNode = existing->fromNode;
                editor.linkingPort = existing->fromPort;
                editor.linkingFromOutput = true;
                const ShaderGraphLink taken = *existing;
                std::erase(data.links, taken);
                editor.detached = true;
            }
            else
            {
                editor.linkingNode = port->node;
                editor.linkingPort = port->port;
                editor.linkingFromOutput = port->output;
                editor.detached = false;
            }
        }
        else if (hoveredBox != nullptr)
        {
            selectNode(state, hoveredBox->id);
            editor.dragged = hoveredBox->id;
            editor.moved = false;
        }
        else
        {
            selectNode(state, 0);
        }
    }
    if (editor.linkingNode != 0 && !input.pointerDown)
    {
        // Released: on a port of the other side, the link is made.
        if (port && port->output != editor.linkingFromOutput)
        {
            const ShaderGraphLink link = editor.linkingFromOutput
                                             ? ShaderGraphLink{editor.linkingNode, editor.linkingPort, port->node, port->port}
                                             : ShaderGraphLink{port->node, port->port, editor.linkingNode, editor.linkingPort};
            static_cast<void>(data.connect(link));
        }
        editor.linkingNode = 0;
        editor.detached = false;
        commitShaderGraph(state);
    }
    if (editor.dragged != 0)
    {
        if (input.pointerDown)
        {
            const math::Vec2 moved = math::Vec2{state.input.mouseDelta().x, state.input.mouseDelta().y} * (unitsPerPoint / view.scale);
            if (ShaderGraphNode* const node = data.find(editor.dragged); node != nullptr && (moved.x != 0.0f || moved.y != 0.0f))
            {
                node->position += moved;
                editor.moved = true;
            }
        }
        else
        {
            editor.dragged = 0;
            if (editor.moved)
            {
                commitShaderGraph(state);
            }
        }
    }

    // The view moves with the middle or the right button, and zooms around the pointer.
    if (hovered && (state.input.clicked(Mouse::Right) || state.input.clicked(Mouse::Middle)))
    {
        grabbed = true;
    }
    if (grabbed && (state.input.dragging(Mouse::Middle) || state.input.dragging(Mouse::Right)))
    {
        editor.pan += math::Vec2{state.input.mouseDelta().x, state.input.mouseDelta().y} * unitsPerPoint;
        editor.panning = true;
    }
    if (hovered && input.wheel != 0.0f)
    {
        const math::Vec2 under = view.toGraph(mouse);
        editor.zoom = std::clamp(editor.zoom * std::pow(1.15f, input.wheel), 0.3f, 2.5f);
        editor.pan = mouse - under * (editor.zoom * graphUnit());
    }
    // A right click that did not move the view opens the menu of the node under it, or the menu that
    // adds nodes there.
    if (grabbed && state.input.released(Mouse::Right))
    {
        if (hovered && !editor.panning && editor.linkingNode == 0)
        {
            menuPosition = at;
            menuPointer = input.pointer;
            menuNode = hoveredBox != nullptr ? hoveredBox->id : 0;
            if (menuNode != 0)
            {
                selectNode(state, menuNode);
                const ShaderGraphNode* const node = data.find(menuNode);
                const bool output = node != nullptr && node->type == "output";
                enable(duplicateItem, !output);
                enable(deleteItem, !output);
                world.openPopup(scene(), nodeMenu, input.pointer);
            }
            else
            {
                // A category shows when it has a node to offer here.
                for (std::size_t index = 0; index < categories.size(); ++index)
                {
                    const bool offered = std::ranges::any_of(asset::shaderNodeCatalog(), [&](const asset::ShaderNodeEntry& entry) {
                        return entry.category == categories[index] && asset::offers(entry, data.kind, editor.function);
                    });
                    scene().get<UiRect>(categoryItems[index].entity).visible = offered;
                }
                fitMenu(addMenu, font * 11.0f);
                world.openPopup(scene(), addMenu, input.pointer);
            }
        }
    }
    if (!state.input.down(Mouse::Right) && !state.input.down(Mouse::Middle))
    {
        editor.panning = false;
        grabbed = false;
    }
    if (editor.linkingNode != 0 && panel.focused() && state.input.pressed(platform::Key::Escape, true))
    {
        editor.linkingNode = 0;
        commitShaderGraph(state);
    }

    // What a node with an error says, next to the pointer.
    if (hovered && hoveredBox != nullptr && !port)
    {
        std::string said;
        for (const asset::ShaderDiagnostic& diagnostic : editor.diagnostics)
        {
            if (diagnostic.node == hoveredBox->id)
            {
                said += (said.empty() ? "" : "\n") + diagnostic.message;
            }
        }
        if (!said.empty())
        {
            kit.showTooltip(said, state.input.mouse());
        }
    }
    else if (hovered && port)
    {
        const auto found = std::ranges::find(boxes, port->node, &NodeBox::id);
        if (found != boxes.end())
        {
            const std::vector<ShaderGraphPort>& ports = port->output ? found->outputs : found->inputs;
            if (port->port < ports.size())
            {
                kit.showTooltip(std::format("{} ({})", ports[port->port].name, asset::toString(ports[port->port].type)), state.input.mouse());
            }
        }
    }

    // The menus.
    for (std::size_t index = 0; index < categories.size(); ++index)
    {
        if (!world.wasClicked(categoryItems[index].entity))
        {
            continue;
        }
        // The nodes of the category this function offers.
        for (auto& [entry, item] : entryItems)
        {
            const asset::ShaderNodeEntry& shown = asset::shaderNodeCatalog()[entry];
            scene().get<UiRect>(item.entity).visible = shown.category == categories[index] && asset::offers(shown, data.kind, editor.function);
        }
        fitMenu(entriesMenu, font * 14.0f);
        world.closePopup(scene(), addMenu);
        world.openPopup(scene(), entriesMenu, menuPointer);
    }
    for (const auto& [entry, item] : entryItems)
    {
        if (world.wasClicked(item.entity))
        {
            const std::uint32_t id = data.nextId();
            data.nodes.push_back(asset::makeShaderNode(asset::shaderNodeCatalog()[entry], data.kind, editor.function, id, menuPosition));
            selectNode(state, id);
            commitShaderGraph(state);
        }
    }
    if (world.wasClicked(duplicateItem.entity))
    {
        duplicateSelected(state);
    }
    if (world.wasClicked(deleteItem.entity))
    {
        deleteSelected(state);
    }
}

void ShaderGraphUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        setFont(state.theme.fontSize);
        build(kit);
    }
    styleTooltips(colors);
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    ui::UiWorld& world = panel.world();
    const float zoom = UiPanel::zoomFor(font);
    const auto sayOnly = [&](std::string sentence) {
        say(std::move(sentence));
        panel.update(kit, delta, zoom);
        editor.focused = panel.focused();
    };
    if (state.database == nullptr)
    {
        sayOnly("Open a project to edit its shader graphs.");
        return;
    }
    // What the panel edits: the graph selected in the FileSystem, else the one it showed.
    asset::AssetId target = isShaderGraph(state, state.selectedAsset) ? state.selectedAsset : editor.asset;
    if (!target.isValid() || state.database->find(target) == nullptr || !isShaderGraph(state, target))
    {
        sayOnly("Select a shader graph in the FileSystem, or make one with Create New > Shader Graph.");
        return;
    }
    loadShaderGraph(state, target);
    refreshDiagnostics(state);
    scene().get<UiRect>(message).visible = false;
    scene().get<UiRect>(toolbar).visible = true;
    scene().get<UiRect>(graph).visible = true;

    // The toolbar: the graph, the functions of its kind, undo, and what the last import said.
    const asset::AssetInfo* const info = state.database->find(editor.asset);
    scene().get<scene::UiImage>(glyph).color = linearColor(colors.material);
    fitText(kit, title, info != nullptr ? info->name : std::string("Shader Graph"), true);
    const std::span<const ShaderFunction> functions = asset::functionsOf(editor.graph.kind);
    for (std::size_t index = 0; index < functionTabs.size(); ++index)
    {
        const bool used = index < functions.size();
        scene().get<UiRect>(functionTabs[index].entity).visible = used;
        if (used)
        {
            relabel(kit, functionTabs[index], functionLabel(functions[index]));
            scene().get<UiRect>(functionTabs[index].entity).style = functions[index] == editor.function ? "primary" : "button";
        }
    }
    enable(undo, !editor.undo.empty());
    enable(redo, !editor.redo.empty());
    {
        std::string said;
        std::string_view style = "dim";
        const std::size_t errors = static_cast<std::size_t>(std::ranges::count_if(editor.diagnostics, &asset::ShaderDiagnostic::error));
        if (!editor.error.empty())
        {
            said = editor.error;
            style = "error";
        }
        else if (errors > 0)
        {
            const auto first = std::ranges::find_if(editor.diagnostics, &asset::ShaderDiagnostic::error);
            const ShaderGraphNode* const node = first->node != 0 ? editor.graph.find(first->node) : nullptr;
            said = std::format("{} error{}: {}{}", errors, errors == 1 ? "" : "s", node != nullptr ? asset::nodeTitle(*node) + ": " : std::string{},
                               first->message);
            style = "error";
        }
        else
        {
            said = "Right-click: add nodes. Drag from a port to link. Middle or right drag: move the view.";
        }
        scene::UiText& shown = scene().get<scene::UiText>(hint);
        if (shown.text != said)
        {
            shown.text = std::move(said);
        }
        scene().get<UiRect>(hint).style = style;
    }
    showGraph(state);

    const bool menuWasOpen = menuOpen();
    panel.update(kit, delta, zoom);
    editor.focused = panel.focused();

    if (world.wasClicked(undo.entity))
    {
        undoShaderGraph(state);
    }
    else if (world.wasClicked(redo.entity))
    {
        redoShaderGraph(state);
    }
    if (world.wasClicked(frame.entity))
    {
        editor.frame = true;
    }
    if (world.wasClicked(code.entity))
    {
        showShaderGraphCode(state);
    }
    for (std::size_t index = 0; index < functions.size() && index < functionTabs.size(); ++index)
    {
        if (world.wasClicked(functionTabs[index].entity) && editor.function != functions[index])
        {
            editor.function = functions[index];
            selectNode(state, 0);
            editor.linkingNode = 0;
            editor.frame = true;
        }
    }
    if (const ui::LaidOutRect* const area = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(graph))
    {
        answerGraph(state, kit, *area, menuWasOpen);
    }

    // Shortcuts, while no field takes the keys.
    if (editor.focused && !world.isEditing())
    {
        if (state.input.chord(KeyModifiers{.ctrl = true}, 'z'))
        {
            undoShaderGraph(state);
        }
        if (state.input.chord(KeyModifiers{.ctrl = true}, 'y') || state.input.chord(KeyModifiers{.ctrl = true, .shift = true}, 'z'))
        {
            redoShaderGraph(state);
        }
        if (state.input.chord(KeyModifiers{.ctrl = true}, 'd'))
        {
            duplicateSelected(state);
        }
        if (state.input.pressed(platform::Key::Delete, false))
        {
            deleteSelected(state);
        }
    }
}

void drawShaderGraphPanel(ToolsState& state)
{
    DEVEX_PROFILE_SCOPE("Shader Graph panel");
    ShaderGraphEditor& editor = state.shaderGraphEditor;
    if (!state.showShaderGraph)
    {
        editor.focused = false;
        return;
    }
    if (std::exchange(state.focusShaderGraph, false))
    {
        focusPanel(state, shaderGraphWindow);
    }
    if (!beginDockedPanel(state, shaderGraphWindow))
    {
        editor.focused = false;
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.shaderGraphUi)
    {
        state.shaderGraphUi = std::make_shared<ShaderGraphUi>();
    }
    state.shaderGraphUi->update(state, kit, core::Duration(state.input.delta()));
    endDockedPanel(state);
}

void renderShaderGraphPanel(ToolsState& state, render::RenderWorld& world)
{
    if (state.shaderGraphUi && state.uiKit)
    {
        state.shaderGraphUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

// ---- The page of a node in the inspector ----

namespace {

class ShaderNodePage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const ShaderGraphEditor& editor = state.shaderGraphEditor;
        const ShaderGraphNode* const node = editor.graph.find(editor.selected);
        if (node == nullptr)
        {
            return "none";
        }
        // Rebuilt when what it edits changes shape: its type, its hints, its ports, its links.
        std::string text = std::format("{}|{}|{}|{}|{}|{}|", node->id, node->type, node->text("type"), node->text("hint"),
                                       node->text("inputs"), node->text("outputs"));
        for (std::size_t port = 0; port < node->inputs.size(); ++port)
        {
            text += editor.graph.linkInto(node->id, static_cast<std::uint32_t>(port)) != nullptr ? "1" : "0";
        }
        for (const asset::ShaderDiagnostic& diagnostic : editor.diagnostics)
        {
            text += diagnostic.node == node->id ? diagnostic.message : std::string{};
        }
        return text;
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        m_rows.clear();
        m_inputs.clear();
        m_back = {};
        m_colorRow.reset();
        ShaderGraphEditor& editor = state.shaderGraphEditor;
        const ShaderGraphNode* const node = editor.graph.find(editor.selected);
        if (node == nullptr)
        {
            return;
        }
        const ThemeColors& colors = themeColors();
        const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(editor.asset) : nullptr;
        ui.heading(kit, icons::Workflow, colors.material, asset::nodeTitle(*node),
                   std::format("in {}", info != nullptr ? info->name : std::string("the shader graph")));
        const Entity top = ui.actions(nullptr);
        m_back = ui.action(kit, top, Icon::Close, "Show the Selection Again");

        for (const asset::ShaderDiagnostic& diagnostic : editor.diagnostics)
        {
            if (diagnostic.node == node->id)
            {
                ui.note(nullptr, diagnostic.message, diagnostic.error ? "error" : "warning", 2.0f);
            }
        }

        const std::vector<asset::ShaderSettingInfo> settings = asset::settingsOf(*node, editor.graph.kind);
        if (!settings.empty())
        {
            Section& card = ui.card(kit, "Settings");
            for (const asset::ShaderSettingInfo& setting : settings)
            {
                Row& row = m_rows.emplace_back();
                row.info = setting;
                row.form = ui.formRow(card, setting.label, setting.kind == asset::ShaderSettingKind::Code ? ui.line * 6.0f : 0.0f);
                if (!setting.tooltip.empty())
                {
                    ui.tooltip(row.form.editor, setting.tooltip);
                }
                switch (setting.kind)
                {
                case asset::ShaderSettingKind::Choice:
                    row.control = ui.choice(row.form.editor, setting.options);
                    break;
                case asset::ShaderSettingKind::Name:
                    row.control = ui.textField(row.form.editor);
                    break;
                case asset::ShaderSettingKind::Code: {
                    row.control = ui.textField(row.form.editor);
                    ui.scene().get<scene::UiInput>(row.control).multiline = true;
                    ui.scene().get<scene::UiText>(row.control).font = EditorUiKit::monoFont();
                    ui.scene().get<scene::UiText>(row.control).verticalAlign = scene::TextVerticalAlign::Top;
                    break;
                }
                case asset::ShaderSettingKind::Toggle:
                    row.control = ui.toggle(row.form.editor);
                    break;
                case asset::ShaderSettingKind::Color:
                    row.control = ui.swatch(kit, row.form.editor, true);
                    break;
                case asset::ShaderSettingKind::Numbers: {
                    static constexpr std::array<std::string_view, 4> letters{"X", "Y", "Z", "W"};
                    static constexpr std::array<std::string_view, 1> one{""};
                    const bool range = setting.name == "range";
                    static constexpr std::array<std::string_view, 3> rangeLetters{"Min", "Max", "Step"};
                    row.numbers = ui.numbers(row.form.editor,
                                             range              ? std::span<const std::string_view>(rangeLetters)
                                             : setting.count == 1 ? std::span<const std::string_view>(one)
                                                                  : std::span<const std::string_view>(letters).first(setting.count),
                                             scene::UiNumberField{.dragSpeed = 0.01f, .decimals = 3});
                    break;
                }
                }
            }
        }

        // The values of the inputs no link feeds.
        const std::vector<ShaderGraphPort> ports = asset::inputsOf(*node, editor.graph.kind);
        if (node->type != "output")
        {
            Section* card = nullptr;
            for (std::size_t port = 0; port < ports.size(); ++port)
            {
                if (ports[port].type == ShaderParameterType::Texture || editor.graph.linkInto(node->id, static_cast<std::uint32_t>(port)) != nullptr)
                {
                    continue;
                }
                if (card == nullptr)
                {
                    card = &ui.card(kit, "Inputs");
                }
                InputRow& row = m_inputs.emplace_back();
                row.port = port;
                row.type = ports[port].type;
                row.form = ui.formRow(*card, ports[port].builtin.empty() ? ports[port].name : std::format("{} ({})", ports[port].name, ports[port].builtin));
                if (!ports[port].builtin.empty())
                {
                    ui.tooltip(row.form.editor, std::format("Without a link, it reads {}", ports[port].builtin));
                    ui.text(row.form.editor, rects::whole(math::Vec4{ui.font * 0.3f, 0.0f, 0.0f, 0.0f}), ports[port].builtin, "dim");
                    row.builtin = true;
                    continue;
                }
                if (row.type == ShaderParameterType::Bool)
                {
                    row.control = ui.toggle(row.form.editor);
                    continue;
                }
                static constexpr std::array<std::string_view, 4> letters{"X", "Y", "Z", "W"};
                static constexpr std::array<std::string_view, 1> one{""};
                const std::uint32_t count = asset::componentCount(row.type);
                row.numbers = ui.numbers(row.form.editor,
                                         count == 1 ? std::span<const std::string_view>(one) : std::span<const std::string_view>(letters).first(count),
                                         scene::UiNumberField{.dragSpeed = 0.01f, .decimals = row.type == ShaderParameterType::Int ? 0 : 3,
                                                              .format = "{}"});
            }
        }
        if (node->type == "output")
        {
            ui.note(nullptr, "What the links into it compute is what the shader writes; an input left alone keeps the default.", "dim", 2.0f);
        }
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const ShaderGraphEditor& editor = state.shaderGraphEditor;
        const ShaderGraphNode* const node = editor.graph.find(editor.selected);
        if (node == nullptr)
        {
            return;
        }
        for (Row& row : m_rows)
        {
            const asset::ShaderSettingInfo& info = row.info;
            switch (info.kind)
            {
            case asset::ShaderSettingKind::Choice: {
                const std::string_view current = node->text(info.name);
                const auto found = std::ranges::find(info.options, current);
                ui.setChoice(row.control, info.options, found == info.options.end() ? 0 : static_cast<std::int32_t>(found - info.options.begin()));
                break;
            }
            case asset::ShaderSettingKind::Name:
            case asset::ShaderSettingKind::Code:
                ui.setText(row.control, std::string(node->text(info.name)));
                break;
            case asset::ShaderSettingKind::Toggle:
                ui.setToggle(row.control, node->vector(info.name).x != 0.0f);
                break;
            case asset::ShaderSettingKind::Color:
                ui.setSwatch(row.control, node->vector(info.name, math::Vec4{1.0f}), true);
                break;
            case asset::ShaderSettingKind::Numbers: {
                const math::Vec4 value = node->vector(info.name);
                for (std::size_t component = 0; component < row.numbers.size(); ++component)
                {
                    ui.setNumber(row.numbers[component], value[static_cast<int>(component)]);
                }
                break;
            }
            }
        }
        for (InputRow& row : m_inputs)
        {
            if (row.builtin)
            {
                continue;
            }
            const math::Vec4 value = node->input(row.port, math::Vec4{0.0f});
            if (row.type == ShaderParameterType::Bool)
            {
                ui.setToggle(row.control, value.x != 0.0f);
                continue;
            }
            for (std::size_t component = 0; component < row.numbers.size(); ++component)
            {
                ui.setNumber(row.numbers[component], value[static_cast<int>(component)]);
            }
        }
        if (m_colorRow && *m_colorRow < m_rows.size() && ui.colorPopupOpen())
        {
            ui.syncColorPopup(node->vector(m_rows[*m_colorRow].info.name, math::Vec4{1.0f}), true);
        }
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        ShaderGraphEditor& editor = state.shaderGraphEditor;
        ShaderGraphNode* const node = editor.graph.find(editor.selected);
        const ui::UiWorld& world = ui.panel.world();
        if (m_back.entity.isValid() && world.wasClicked(m_back.entity))
        {
            editor.inspecting = false;
            return;
        }
        if (node == nullptr)
        {
            return;
        }
        for (std::size_t index = 0; index < m_rows.size(); ++index)
        {
            const Row& row = m_rows[index];
            const asset::ShaderSettingInfo& info = row.info;
            switch (info.kind)
            {
            case asset::ShaderSettingKind::Choice:
                if (world.wasChanged(row.control))
                {
                    const std::int32_t chosen = ui.scene().get<scene::UiDropdown>(row.control).selected;
                    if (chosen >= 0 && static_cast<std::size_t>(chosen) < info.options.size())
                    {
                        node->setText(info.name, info.options[static_cast<std::size_t>(chosen)]);
                        // A new type asks for operators and functions it has.
                        if (info.name == "type")
                        {
                            for (const asset::ShaderSettingInfo& other : asset::settingsOf(*node, editor.graph.kind))
                            {
                                if (other.kind == asset::ShaderSettingKind::Choice && other.name != "type" &&
                                    std::ranges::find(other.options, node->text(other.name)) == other.options.end() && !other.options.empty())
                                {
                                    node->setText(other.name, other.options.front());
                                }
                            }
                        }
                        m_dirty = true;
                    }
                }
                break;
            case asset::ShaderSettingKind::Name:
            case asset::ShaderSettingKind::Code:
                // Kept once the field lets go of it.
                if (ui.endedField == row.control)
                {
                    const std::string typed = ui.scene().get<scene::UiText>(row.control).text;
                    if (typed != node->text(info.name))
                    {
                        node->setText(info.name, typed);
                        m_dirty = true;
                    }
                }
                break;
            case asset::ShaderSettingKind::Toggle:
                if (world.wasChanged(row.control))
                {
                    node->setVector(info.name, math::Vec4{ui.scene().get<scene::UiToggle>(row.control).value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f});
                    m_dirty = true;
                }
                break;
            case asset::ShaderSettingKind::Color:
                if (world.wasClicked(row.control))
                {
                    m_colorRow = index;
                    ui.openColorPopup(row.control);
                }
                break;
            case asset::ShaderSettingKind::Numbers: {
                math::Vec4 value = node->vector(info.name);
                for (std::size_t component = 0; component < row.numbers.size(); ++component)
                {
                    if (world.wasChanged(row.numbers[component]))
                    {
                        value[static_cast<int>(component)] = ui.scene().get<scene::UiNumberField>(row.numbers[component]).value;
                        node->setVector(info.name, value);
                        m_dirty = true;
                    }
                }
                break;
            }
            }
        }
        for (const InputRow& row : m_inputs)
        {
            if (row.builtin)
            {
                continue;
            }
            if (node->inputs.size() <= row.port)
            {
                node->inputs.resize(row.port + 1, math::Vec4{0.0f});
            }
            if (row.type == ShaderParameterType::Bool)
            {
                if (world.wasChanged(row.control))
                {
                    node->inputs[row.port].x = ui.scene().get<scene::UiToggle>(row.control).value ? 1.0f : 0.0f;
                    m_dirty = true;
                }
                continue;
            }
            for (std::size_t component = 0; component < row.numbers.size(); ++component)
            {
                if (world.wasChanged(row.numbers[component]))
                {
                    node->inputs[row.port][static_cast<int>(component)] = ui.scene().get<scene::UiNumberField>(row.numbers[component]).value;
                    m_dirty = true;
                }
            }
        }
        if (m_colorRow && *m_colorRow < m_rows.size())
        {
            const std::string& name = m_rows[*m_colorRow].info.name;
            if (const std::optional<math::Vec4> chosen = ui.answerColorPopup(node->vector(name, math::Vec4{1.0f})))
            {
                node->setVector(name, *chosen);
                m_dirty = true;
            }
            if (!ui.colorPopupOpen())
            {
                m_colorRow.reset();
            }
        }
        // Saved once the pointer and the keys let go of the value.
        const bool busy = world.held().isValid() || world.isEditing();
        if (m_dirty && !busy)
        {
            commitShaderGraph(state);
            m_dirty = false;
        }
    }

private:
    struct Row
    {
        asset::ShaderSettingInfo info;
        FormRow form;
        Entity control;
        std::vector<Entity> numbers;
    };
    struct InputRow
    {
        std::size_t port = 0;
        ShaderParameterType type = ShaderParameterType::Float;
        FormRow form;
        Entity control;
        std::vector<Entity> numbers;
        bool builtin = false;
    };

    std::vector<Row> m_rows;
    std::vector<InputRow> m_inputs;
    Button m_back;
    std::optional<std::size_t> m_colorRow;
    bool m_dirty = false;
};

} // namespace

std::unique_ptr<InspectorPage> makeShaderNodePage()
{
    return std::make_unique<ShaderNodePage>();
}

} // namespace devex::tools::detail
