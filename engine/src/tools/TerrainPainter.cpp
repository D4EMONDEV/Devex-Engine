#include "InspectorUi.hpp"
#include "SettingsUi.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>

namespace devex::tools::detail {
namespace {

struct ToolChoice
{
    TerrainBrushTool tool;
    const char* label;
    const char* tooltip;
};

constexpr std::array toolChoices{
    ToolChoice{TerrainBrushTool::Raise, "Raise", "Raise the ground under the brush, dragging (Shift lowers it)"},
    ToolChoice{TerrainBrushTool::Lower, "Lower", "Lower the ground under the brush, dragging (Shift raises it)"},
    ToolChoice{TerrainBrushTool::Smooth, "Smooth", "Even out the bumps under the brush"},
    ToolChoice{TerrainBrushTool::Flatten, "Flatten", "Bring the ground to one height (Ctrl+click picks it)"},
    ToolChoice{TerrainBrushTool::Noise, "Noise", "Add bumps that stay where they are, as the ground of a place"},
    ToolChoice{TerrainBrushTool::Paint, "Paint", "Paint the layer chosen below (Shift takes it away, leaving the rules)"},
    ToolChoice{TerrainBrushTool::Hole, "Holes", "Cut holes for caves and tunnels, without ground or collision (Shift fills them)"},
    ToolChoice{TerrainBrushTool::Grass, "Grass", "Grow grass under the brush (Shift mows it)"},
};

// The samples on a side of new terrain data: about a meter apart.
[[nodiscard]] std::uint32_t resolutionFor(float size) noexcept
{
    std::uint32_t cells = 32;
    while (cells < 4096 && static_cast<float>(cells) < size)
    {
        cells *= 2;
    }
    return cells + 1;
}

// The terrain of the active entity, which the brushes change.
[[nodiscard]] scene::Terrain* paintedTerrain(const ToolsState& state, scene::Scene& scene, scene::Entity& entity)
{
    entity = scene.findEntity(state.selection.active());
    return entity.isValid() ? scene.tryGet<scene::Terrain>(entity) : nullptr;
}

[[nodiscard]] math::Vec3 originOf(const scene::Scene& scene, scene::Entity entity)
{
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    return world != nullptr ? math::Vec3(world->matrix[3]) : math::Vec3{0.0f};
}

[[nodiscard]] asset::TerrainMaps* mapsOf(const ToolsState& state, const scene::Terrain& terrain)
{
    return state.terrains && state.terrains->maps && terrain.data.isValid() ? state.terrains->maps(terrain.data) : nullptr;
}

// A stroke of the brushes, undone and done again by putting back the samples it changed.
class TerrainStrokeCommand final : public Command
{
public:
    TerrainStrokeCommand(std::shared_ptr<TerrainAccess> access, asset::AssetId terrain, asset::TerrainPatch before,
                         asset::TerrainPatch after, std::string description)
        : m_access(std::move(access))
        , m_terrain(terrain)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_description(std::move(description))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return m_description;
    }

    [[nodiscard]] core::Result<void> apply(scene::Scene&) override
    {
        return put(m_after);
    }

    [[nodiscard]] core::Result<void> revert(scene::Scene&) override
    {
        return put(m_before);
    }

private:
    [[nodiscard]] core::Result<void> put(const asset::TerrainPatch& patch)
    {
        asset::TerrainMaps* const maps = m_access && m_access->maps ? m_access->maps(m_terrain) : nullptr;
        if (maps == nullptr)
        {
            return core::makeError(core::ErrorCode::NotFound, "the terrain is no longer loaded");
        }
        asset::pasteTerrainPatch(*maps, patch);
        if (m_access->changed)
        {
            m_access->changed(m_terrain, patch.region);
        }
        return {};
    }

    std::shared_ptr<TerrainAccess> m_access;
    asset::AssetId m_terrain;
    asset::TerrainPatch m_before;
    asset::TerrainPatch m_after;
    std::string m_description;
};

// Ends a stroke as one step to undo, holding the samples it changed before and after.
void finishStroke(ToolsState& state)
{
    TerrainPainter& painter = state.terrainPainter;
    painter.stroking = false;
    std::optional<asset::TerrainMaps> before = std::exchange(painter.before, std::nullopt);
    asset::TerrainMaps* const maps = state.terrains && state.terrains->maps ? state.terrains->maps(painter.data) : nullptr;
    if (!before || maps == nullptr || painter.changed.empty())
    {
        return;
    }
    const bool sculpted = painter.tool != TerrainBrushTool::Paint && painter.tool != TerrainBrushTool::Hole &&
                          painter.tool != TerrainBrushTool::Grass;
    state.history.recordApplied(std::make_unique<TerrainStrokeCommand>(
        state.terrains, painter.data, asset::copyTerrainRegion(*before, painter.changed), asset::copyTerrainRegion(*maps, painter.changed),
        sculpted ? "Sculpt Terrain" : "Paint Terrain"));
    painter.changed = {};
}

// Writes flat data for the terrain of an entity, in the folder of its scene (or the terrains of the
// project), and points the terrain at it as one step to undo.
void createTerrainData(ToolsState& state, scene::Scene& scene, scene::Entity entity, const scene::Terrain& terrain)
{
    TerrainPainter& painter = state.terrainPainter;
    if (state.database == nullptr)
    {
        return;
    }
    const std::filesystem::path folder =
        state.scenePath.empty() ? state.database->project().assetsDirectory() / "terrains" : state.scenePath.parent_path();
    std::string stem = state.scenePath.empty() ? std::string(scene.name(entity)) : std::format("{} {}", state.scenePath.stem().string(), scene.name(entity));
    std::ranges::replace_if(stem, [](char character) { return std::string_view("/\\:*?\"<>|").find(character) != std::string_view::npos; }, ' ');
    std::filesystem::path file = folder / (stem + std::string(asset::terrainExtension));
    std::error_code error;
    for (int number = 2; std::filesystem::exists(file, error); ++number)
    {
        file = folder / (std::format("{} {}", stem, number) + std::string(asset::terrainExtension));
    }
    std::filesystem::create_directories(folder, error);
    const asset::TerrainMaps maps = asset::makeFlatTerrain(resolutionFor(terrain.size));
    if (core::Result<void> written = core::writeFileAtomically(file, asset::encodeTerrain(maps)); !written)
    {
        painter.message = std::format("Not created: {}", written.error().message);
        return;
    }
    state.database->refresh();
    state.database->waitForImports();
    const std::optional<asset::AssetId> id = state.database->findByPath(state.database->project().resourcePath(file));
    if (!id)
    {
        painter.message = "The data is written outside the assets folder: move the scene there.";
        return;
    }
    painter.message.clear();
    state.pendingCommand = makeSetFieldCommand(scene.uuid(entity), "Terrain", "data",
                                               scene::writeFieldValue(reflection::ValueKind::AssetId, &terrain.data),
                                               scene::writeFieldValue(reflection::ValueKind::AssetId, &*id));
    DEVEX_LOG_INFO("Created {} for the terrain of '{}'", core::toUtf8(file.filename()), scene.name(entity));
}

} // namespace

// The tools that sculpt and paint the terrain, under its card: the brush, its size, strength and
// softness, the layer painted, and what the tool does.
struct TerrainPainterUi
{
    std::array<PanelButton, toolChoices.size()> tools;
    scene::Entity size;
    scene::Entity strength;
    scene::Entity softness;
    scene::Entity layerRow;
    scene::Entity layer;
    std::vector<std::string> layerNames;
    scene::Entity createRow;
    PanelButton create;
    bool createRequested = false;
    scene::Entity message;
    scene::Entity hint;
};

void addTerrainPainter(InspectorUi& ui, EditorUiKit& kit, Section& section)
{
    auto painter = std::make_shared<TerrainPainterUi>();
    const scene::Entity title = ui.note(&section, "Sculpt and Paint", "text");
    ui.scene().get<scene::UiText>(title).font = EditorUiKit::boldFont();
    // The tools, in rows of four however narrow the panel.
    const scene::Entity tools = ui.add(section.card, "Tools", rects::wide(ui.line * 2.0f + ui.gap * 3.0f));
    ui.scene().add<scene::UiLayout>(tools, scene::UiLayout{.kind = scene::UiLayoutKind::Grid,
                                                           .spacing = ui.gap * 3.0f,
                                                           .padding = {ui.font * 0.35f, 0.0f, 0.0f, 0.0f},
                                                           .columns = 4,
                                                           .equalSize = true});
    section.lines.push_back(Line{.entity = tools});
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        painter->tools[index] = ui.button(kit, tools, std::nullopt, toolChoices[index].label, "button", 0.0f, ui.line - 4.0f);
        ui.tooltip(painter->tools[index].entity, toolChoices[index].tooltip);
    }
    const FormRow sizeRow = ui.formRow(section, "Brush Size");
    painter->size = ui.numbers(sizeRow.editor, singleNumber, {.minValue = 0.5f, .maxValue = 100.0f, .dragSpeed = 0.1f, .decimals = 1,
                                                               .format = "{} m"})
                        .front();
    ui.tooltip(sizeRow.row, "The radius of the brush");
    const FormRow strengthRow = ui.formRow(section, "Strength");
    painter->strength = ui.numbers(strengthRow.editor, singleNumber, {.minValue = 0.0f, .maxValue = 1.0f, .dragSpeed = 0.005f, .decimals = 2})
                            .front();
    ui.tooltip(strengthRow.row, "How much the brush does each second it is held");
    const FormRow softnessRow = ui.formRow(section, "Softness");
    painter->softness = ui.numbers(softnessRow.editor, singleNumber, {.minValue = 0.0f, .maxValue = 1.0f, .dragSpeed = 0.005f, .decimals = 2})
                            .front();
    ui.tooltip(softnessRow.row, "How much of the radius the brush fades over towards its edge");
    const FormRow layerRow = ui.formRow(section, "Layer");
    painter->layerRow = layerRow.row;
    painter->layer = ui.choice(layerRow.editor);
    ui.tooltip(layerRow.row, "The layer Paint lays: one of the materials of the terrain");
    painter->createRow = ui.actions(&section);
    painter->create = ui.action(kit, painter->createRow, std::nullopt, "Create Data");
    ui.tooltip(painter->create.entity, "Write flat ground for this terrain, about one sample per meter, next to its scene");
    painter->message = ui.note(&section, "", "warning", 2.0f);
    painter->hint = ui.note(&section, "", "dim", 3.0f);
    section.terrainPainter = std::move(painter);
}

void syncTerrainPainter(InspectorUi& ui, ToolsState& state, scene::Scene& edited, scene::Entity entity, Section& section)
{
    TerrainPainterUi& shown = *section.terrainPainter;
    const scene::Terrain* const terrain = edited.tryGet<scene::Terrain>(entity);
    if (terrain == nullptr)
    {
        return;
    }
    TerrainPainter& painter = state.terrainPainter;
    if (std::exchange(shown.createRequested, false) && !terrain->data.isValid())
    {
        createTerrainData(state, edited, entity, *terrain);
    }
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        ui.scene().get<scene::UiRect>(shown.tools[index].entity).style = painter.tool == toolChoices[index].tool ? "primary" : "button";
    }
    ui.setNumber(shown.size, painter.size);
    ui.setNumber(shown.strength, painter.strength);
    ui.setNumber(shown.softness, painter.softness);

    // The layers by the names of their materials.
    std::vector<std::string> names;
    for (std::size_t layer = 0; layer < terrain->layers.size() && layer < asset::maxTerrainLayers; ++layer)
    {
        const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(terrain->layers[layer]) : nullptr;
        names.push_back(std::format("{}: {}", layer + 1, info != nullptr ? info->name : std::string("(no material)")));
    }
    if (names.empty())
    {
        names.emplace_back("(no layer: add materials to Layers)");
    }
    painter.layer = std::min<std::uint32_t>(painter.layer, static_cast<std::uint32_t>(names.size() - 1));
    shown.layerNames = names;
    ui.setChoice(shown.layer, std::move(names), static_cast<std::int32_t>(painter.layer));
    ui.showLine(section, shown.layerRow, painter.tool == TerrainBrushTool::Paint);

    const bool loaded = mapsOf(state, *terrain) != nullptr;
    ui.showLine(section, shown.createRow, !terrain->data.isValid());
    std::string message = painter.message;
    if (message.empty() && !terrain->data.isValid())
    {
        message = "This terrain has no data yet: create it to sculpt it.";
    }
    else if (message.empty() && !loaded)
    {
        message = "The data of this terrain cannot be loaded.";
    }
    ui.scene().get<scene::UiText>(shown.message).text = message;
    ui.showLine(section, shown.message, !message.empty());
    ui.scene().get<scene::UiText>(shown.hint).text =
        painter.tool == TerrainBrushTool::None
            ? "Choose a tool to sculpt or paint the terrain in the view."
            : "Left drag uses the brush in the view, Shift does the opposite, Ctrl+click takes the height to flatten to; "
              "[ and ] change its size. Escape stops. Each stroke is one step to undo; saving the scene saves the terrain.";
}

void answerTerrainPainter(InspectorUi& ui, ToolsState& state, Section& section)
{
    TerrainPainterUi& shown = *section.terrainPainter;
    TerrainPainter& painter = state.terrainPainter;
    const ui::UiWorld& world = ui.panel.world();
    for (std::size_t index = 0; index < toolChoices.size(); ++index)
    {
        if (world.wasClicked(shown.tools[index].entity))
        {
            painter.tool = painter.tool == toolChoices[index].tool ? TerrainBrushTool::None : toolChoices[index].tool;
        }
    }
    if (world.wasChanged(shown.size))
    {
        painter.size = ui.scene().get<scene::UiNumberField>(shown.size).value;
    }
    if (world.wasChanged(shown.strength))
    {
        painter.strength = ui.scene().get<scene::UiNumberField>(shown.strength).value;
    }
    if (world.wasChanged(shown.softness))
    {
        painter.softness = ui.scene().get<scene::UiNumberField>(shown.softness).value;
    }
    if (world.wasChanged(shown.layer))
    {
        painter.layer = static_cast<std::uint32_t>(std::max(ui.scene().get<scene::UiDropdown>(shown.layer).selected, 0));
    }
    if (world.wasClicked(shown.create.entity))
    {
        shown.createRequested = true;
    }
}

bool handleTerrainPainting(ToolsState& state, scene::Scene& scene, const ViewportView& view, math::Vec2 mouse, bool hovered)
{
    TerrainPainter& painter = state.terrainPainter;
    scene::Entity entity;
    scene::Terrain* const terrain = paintedTerrain(state, scene, entity);
    asset::TerrainMaps* const maps = terrain != nullptr ? mapsOf(state, *terrain) : nullptr;
    if (painter.tool == TerrainBrushTool::None || terrain == nullptr || maps == nullptr || state.playState != PlayState::Editing)
    {
        if (painter.stroking)
        {
            finishStroke(state);
        }
        painter.hovered.reset();
        return false;
    }
    if (state.viewportFocused && !painter.stroking && state.input.pressed(platform::Key::Escape, true))
    {
        painter.tool = TerrainBrushTool::None;
        painter.hovered.reset();
        return false;
    }
    // [ and ] shrink and grow the brush, as in painting programs.
    if (state.viewportFocused && state.input.pressed(platform::Key::LeftBracket, true))
    {
        painter.size = std::max(painter.size / 1.25f, 0.5f);
    }
    if (state.viewportFocused && state.input.pressed(platform::Key::RightBracket, true))
    {
        painter.size = std::min(painter.size * 1.25f, 100.0f);
    }

    const math::Vec3 origin = originOf(scene, entity);
    const Ray ray = view.ray(mouse);
    const std::optional<float> distance = asset::raycastTerrain(*maps, terrain->size, ray.origin - origin, ray.direction, 10000.0f);
    const std::optional<math::Vec3> hit = distance ? std::optional(ray.origin + ray.direction * *distance) : std::nullopt;
    painter.hovered = hovered || painter.stroking ? hit : std::nullopt;

    if (!painter.stroking)
    {
        if (!hovered || !hit || !state.input.clicked(Mouse::Left))
        {
            return hovered;
        }
        state.hosts.focusCurrent();
        if (state.input.ctrl())
        {
            painter.flattenHeight = hit->y - origin.y;
            return true;
        }
        painter.stroking = true;
        painter.target = scene.uuid(entity);
        painter.data = terrain->data;
        painter.before = *maps;
        painter.changed = {};
    }
    if (!state.input.down(Mouse::Left))
    {
        finishStroke(state);
        return true;
    }
    if (!hit)
    {
        return true;
    }

    // As much each second as the strength says, wherever the frame rate.
    const float seconds = std::clamp(state.input.delta(), 0.0f, 0.1f);
    const float strength = std::clamp(painter.strength, 0.0f, 1.0f);
    const bool opposite = state.input.shift();
    asset::TerrainBrush brush{
        .center = {hit->x - origin.x, hit->z - origin.z},
        .radius = std::max(painter.size, 0.1f),
        .softness = std::clamp(painter.softness, 0.0f, 1.0f),
        .height = painter.flattenHeight,
        .layer = painter.layer,
        .erase = opposite,
    };
    switch (painter.tool)
    {
    case TerrainBrushTool::Raise:
    case TerrainBrushTool::Lower:
        brush.tool = (painter.tool == TerrainBrushTool::Raise) != opposite ? asset::TerrainTool::Raise : asset::TerrainTool::Lower;
        brush.amount = strength * brush.radius * 0.5f * seconds;
        break;
    case TerrainBrushTool::Smooth:
        brush.tool = asset::TerrainTool::Smooth;
        brush.amount = strength * 8.0f * seconds;
        break;
    case TerrainBrushTool::Flatten:
        brush.tool = asset::TerrainTool::Flatten;
        brush.amount = strength * 6.0f * seconds;
        break;
    case TerrainBrushTool::Noise:
        brush.tool = asset::TerrainTool::Noise;
        brush.amount = strength * brush.radius * 0.1f * seconds;
        break;
    case TerrainBrushTool::Paint:
        brush.tool = asset::TerrainTool::Paint;
        brush.amount = strength * 4.0f * seconds;
        break;
    case TerrainBrushTool::Hole:
        brush.tool = asset::TerrainTool::Hole;
        brush.amount = 1.0f;
        break;
    case TerrainBrushTool::Grass:
        brush.tool = asset::TerrainTool::Grass;
        brush.amount = strength * 4.0f * seconds;
        break;
    case TerrainBrushTool::None:
        return true;
    }
    const asset::TerrainRegion region = asset::applyTerrainBrush(*maps, terrain->size, brush);
    if (!region.empty())
    {
        painter.changed = painter.changed.merged(region);
        if (state.terrains->changed)
        {
            state.terrains->changed(painter.data, region);
        }
    }
    return true;
}

void addTerrainPainterOverlay(ToolsState& state, scene::Scene& scene, render::RenderWorld& world)
{
    const TerrainPainter& painter = state.terrainPainter;
    scene::Entity entity;
    const scene::Terrain* const terrain = paintedTerrain(state, scene, entity);
    const asset::TerrainMaps* const maps = terrain != nullptr ? mapsOf(state, *terrain) : nullptr;
    if (painter.tool == TerrainBrushTool::None || maps == nullptr || !painter.hovered)
    {
        return;
    }
    const math::Vec3 origin = originOf(scene, entity);
    const math::Vec2 center{painter.hovered->x - origin.x, painter.hovered->z - origin.z};
    const bool opposite = state.input.shift();
    const math::Vec4 color = opposite ? math::Vec4{1.0f, 0.35f, 0.3f, 1.0f} : math::Vec4{1.0f, 0.75f, 0.2f, 1.0f};
    // The brush and where it starts to fade, lying on the ground.
    const auto circle = [&](float radius, math::Vec4 tint) {
        constexpr int segments = 64;
        math::Vec3 previous{0.0f};
        for (int index = 0; index <= segments; ++index)
        {
            const float angle = static_cast<float>(index) / static_cast<float>(segments) * 2.0f * std::numbers::pi_v<float>;
            const math::Vec2 point = center + math::Vec2{std::cos(angle), std::sin(angle)} * radius;
            const math::Vec3 lying = origin + math::Vec3{point.x, asset::terrainHeight(*maps, terrain->size, point.x, point.y) + 0.05f, point.y};
            if (index > 0)
            {
                world.overlayLines.push_back({previous, tint});
                world.overlayLines.push_back({lying, tint});
            }
            previous = lying;
        }
    };
    circle(painter.size, color);
    const float inner = painter.size * (1.0f - std::clamp(painter.softness, 0.0f, 1.0f));
    if (inner > 0.05f)
    {
        circle(inner, math::Vec4{color.r, color.g, color.b, 0.4f});
    }
}

void saveTerrains(ToolsState& state, const scene::Scene& scene)
{
    if (!state.terrains || !state.terrains->maps || !state.terrains->edited || state.database == nullptr)
    {
        return;
    }
    for ([[maybe_unused]] auto [entity, terrain] : scene.view<scene::Terrain>())
    {
        if (!terrain.data.isValid() || !state.terrains->edited(terrain.data))
        {
            continue;
        }
        const asset::TerrainMaps* const maps = state.terrains->maps(terrain.data);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(terrain.data);
        const std::optional<std::filesystem::path> file = source ? state.database->project().absolutePath(source->path) : std::nullopt;
        if (maps == nullptr || !file)
        {
            continue;
        }
        if (core::Result<void> written = core::writeFileAtomically(*file, asset::encodeTerrain(*maps)); !written)
        {
            DEVEX_LOG_ERROR("Cannot save the terrain {}: {}", core::toUtf8(file->filename()), written.error());
            continue;
        }
        if (state.terrains->saved)
        {
            state.terrains->saved(terrain.data);
        }
        DEVEX_LOG_INFO("Saved {}", core::toUtf8(file->filename()));
    }
}

} // namespace devex::tools::detail
