#include "RemoteScenePanel.hpp"
#include "ToolsState.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>

namespace devex::tools::detail
{

using scene::Entity;
using scene::UiRect;
using namespace rects;

SceneSourceTabs addSceneSourceTabs(PanelBuilder& ui, EditorUiKit& kit, Entity parent, bool remote)
{
    const Entity row = ui.add(parent, "Scene Source", wide(ui.font * 2.0f));
    ui.scene().add<scene::UiLayout>(row, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                         .spacing = ui.font * 0.3f,
                                                         .align = scene::TextAlign::Left});
    const auto local =
        ui.button(kit, row, std::nullopt, "Local", remote ? "bar_button" : "bar_selected");
    const auto running =
        ui.button(kit, row, Icon::Monitor, "Remote", remote ? "bar_selected" : "bar_button");
    return {row, remote ? local : running};
}

struct RemoteSceneUi : PanelBuilder
{
    struct Row
    {
        Entity button;
        Entity arrow;
        Entity glyph;
        Entity name;
    };

    RemoteSceneUi(EditorUiKit& kit, std::uint32_t surface, float fontSize);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
    void gather();
    void fillRows(EditorUiKit& kit, core::Uuid selected);

    SceneSourceTabs sources;
    Entity chooser;
    Entity filter;
    Entity scroll;
    Entity lines;
    Entity status;
    std::vector<Row> rows;
    std::vector<std::size_t> visible;
    std::shared_ptr<const RemoteSceneSnapshot> snapshot;
    std::unordered_set<core::Uuid> collapsed;
    std::string search;
    std::array<std::uint32_t, 4> processes{};
    std::vector<std::uint32_t> choices;
    std::uint32_t pid = 0;
    std::size_t first = 0;
    bool treeDirty = false;
};

RemoteSceneUi::RemoteSceneUi(EditorUiKit& kit, std::uint32_t surface, float fontSize)
    : PanelBuilder(surface)
{
    font = fontSize;
    const float line = font * 2.0f;
    const Entity root = add({}, "Remote Scene", whole());
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = font * 0.4f,
                                                       .align = scene::TextAlign::Left});
    sources = addSceneSourceTabs(*this, kit, root, true);

    chooser = text(root, wide(line), "", "dropdown");
    scene().add<scene::UiImage>(chooser);
    scene().add<scene::UiDropdown>(
        chooser, scene::UiDropdown{
                     .selected = -1, .interactable = false, .placeholder = "Waiting for a player"});
    filter = searchField(kit, root, wide(line), "", "Filter Entities");

    const Entity list = add(root, "List", whole(), "list");
    scene().add<scene::UiImage>(list);
    scroll = add(list, "Scroll", whole(math::Vec4{2.0f}), "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{.speed = std::round(font * 1.85f) * 3.0f});
    lines = add(scroll, "Lines", wide(0.0f));
    scene().get<UiRect>(lines).offsetMax.x = -8.0f;
    scene().add<scene::UiVirtualList>(lines);
    status = text(root, wide(font * 1.7f), "", "dim", false, scene::TextAlign::Left, font * 0.9f);
    panel.setKeyboardNavigation(false);
}

void RemoteSceneUi::gather()
{
    visible.clear();
    if (!snapshot)
        return;
    const auto& entities = snapshot->entities;
    if (!search.empty())
    {
        // Keep the ancestors of each match so it can be located in the running scene.
        std::vector<std::size_t> parents;
        std::vector<bool> matches(entities.size());
        for (std::size_t index = 0; index < entities.size(); ++index)
        {
            const auto& entity = entities[index];
            parents.resize(entity.depth);
            if (containsIgnoringCase(entity.name, search))
            {
                matches[index] = true;
                for (auto parent = parents.rbegin(); parent != parents.rend() && !matches[*parent];
                     ++parent)
                    matches[*parent] = true;
            }
            parents.push_back(index);
        }
        for (std::size_t index = 0; index < matches.size(); ++index)
            if (matches[index])
                visible.push_back(index);
        return;
    }
    for (std::size_t index = 0; index < entities.size();)
    {
        visible.push_back(index);
        const auto& entity = entities[index++];
        if (collapsed.contains(entity.id))
            while (index < entities.size() && entities[index].depth > entity.depth)
                ++index;
    }
}

void RemoteSceneUi::fillRows(EditorUiKit& kit, core::Uuid selected)
{
    const float rowHeight = std::round(font * 1.85f);
    const float arrowWidth = std::round(font * 1.4f);
    const float iconSize = font * 1.1f;
    const auto& colors = themeColors();
    auto& list = scene().get<scene::UiVirtualList>(lines);
    list.itemCount = static_cast<std::uint32_t>(visible.size());
    list.itemSize = rowHeight;
    const auto& canvases = panel.world().canvases();
    const auto* view = canvases.empty() ? nullptr : canvases.front().layout.find(scroll);
    auto& offset = scene().get<scene::UiScroll>(scroll).offset.y;
    if (view != nullptr)
        offset = std::clamp(
            offset, 0.0f,
            std::max(static_cast<float>(visible.size()) * rowHeight - view->size().y, 0.0f));
    first = std::min(static_cast<std::size_t>(std::max(offset, 0.0f) / rowHeight), visible.size());
    const auto shown =
        static_cast<std::size_t>(std::ceil(std::max(panel.size().y, 300.0f) / rowHeight)) + 2;
    const auto count = std::min(shown, visible.size() - first);
    while (rows.size() < count)
    {
        Row row;
        row.button = add(lines, "Entity", fixed({1.0f, rowHeight}), "soft_row");
        scene().add<scene::UiImage>(row.button);
        scene().add<scene::UiButton>(row.button);
        row.arrow = add(row.button, "Arrow", whole());
        scene().add<scene::UiFoldout>(row.arrow);
        row.glyph = icon(kit, row.button, middle({iconSize, iconSize}), Icon::Box, "icon_dim");
        row.name = text(row.button, whole(), "", "text");
        scene().get<scene::UiText>(row.name).translate = false;
        rows.push_back(row);
    }
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const auto& row = rows[index];
        auto& rect = scene().get<UiRect>(row.button);
        rect.visible = index < count;
        if (!rect.visible)
            continue;
        const auto at = visible[first + index];
        const auto& entities = snapshot->entities;
        const auto& entity = entities[at];
        rect.style = entity.id == selected ? "soft_row_selected" : "soft_row";
        const float indent = std::min(static_cast<float>(entity.depth) * font * 1.3f,
                                      std::max(panel.size().x * 0.45f, font * 2.0f));
        const float left = font * 0.3f + indent;
        auto& arrow = scene().get<UiRect>(row.arrow);
        arrow.anchorMax.x = 0.0f;
        arrow.offsetMin.x = left;
        arrow.offsetMax.x = left + arrowWidth;
        arrow.visible =
            search.empty() && at + 1 < entities.size() && entities[at + 1].depth > entity.depth;
        auto& foldout = scene().get<scene::UiFoldout>(row.arrow);
        foldout.expanded = !collapsed.contains(entity.id);
        foldout.arrowColor = linearColor(colors.textDim);
        auto& glyph = scene().get<UiRect>(row.glyph);
        glyph.offsetMin.x = left + arrowWidth;
        glyph.offsetMax.x = glyph.offsetMin.x + iconSize;
        scene().get<UiRect>(row.name).offsetMin.x = glyph.offsetMax.x + font * 0.45f;
        const std::string_view name =
            entity.name.empty() ? "(unnamed)" : std::string_view(entity.name);
        auto& label = scene().get<scene::UiText>(row.name).text;
        if (label != name)
            label = name;
    }
}

void RemoteSceneUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    auto& remote = state.remoteScene;
    auto& world = panel.world();
    auto& dropdown = scene().get<scene::UiDropdown>(chooser);
    if (processes != remote.view.processes && world.listedDropdown() != chooser)
    {
        processes = remote.view.processes;
        dropdown.options.clear();
        choices.clear();
        for (std::size_t index = 0; index < processes.size(); ++index)
        {
            if (processes[index] == 0)
                continue;
            choices.push_back(static_cast<std::uint32_t>(index + 1));
            dropdown.options.push_back(std::format("Player {}", index + 1));
        }
        dropdown.interactable = choices.size() > 1;
    }
    const auto choice = std::ranges::find(choices, remote.player);
    dropdown.selected =
        choice == choices.end() ? -1 : static_cast<std::int32_t>(choice - choices.begin());
    const auto currentPid = remote.player == 0 ? 0u : remote.view.processes[remote.player - 1];
    if (pid != currentPid)
    {
        pid = currentPid;
        collapsed.clear();
        scene().get<scene::UiScroll>(scroll).offset.y = 0.0f;
        treeDirty = true;
    }
    const auto current = remote.player == remote.view.player ? remote.view.snapshot : nullptr;
    const auto& filterText = scene().get<scene::UiText>(filter).text;
    if (snapshot != current || search != filterText || treeDirty)
    {
        if (search != filterText)
            scene().get<scene::UiScroll>(scroll).offset.y = 0.0f;
        snapshot = current;
        search = filterText;
        gather();
        treeDirty = false;
    }
    auto& message = scene().get<scene::UiText>(status).text;
    if (remote.player == 0)
        message = "Waiting for a player";
    else if (remote.player == remote.view.player && remote.view.unavailable)
        message = snapshot ? "Scene could not be refreshed" : "Scene unavailable";
    else if (!snapshot)
        message = "Waiting for the scene";
    else if (snapshot->treeLimited)
        message = std::format("First {} entities shown", snapshot->entities.size());
    else if (visible.empty())
        message = search.empty() ? "No entities" : "No matching entities";
    else
        message.clear();
    scene().get<UiRect>(status).visible = !message.empty();
    fillRows(kit, remote.entity);
    panel.update(kit, delta, UiPanel::zoomFor(font));

    if (world.wasClicked(sources.other.entity))
        remote.switchRequested = true;
    if (world.wasChanged(chooser) && dropdown.selected >= 0)
    {
        const auto at = static_cast<std::size_t>(dropdown.selected);
        if (at < choices.size() && remote.view.processes[choices[at] - 1] != 0)
        {
            remote.player = choices[at];
            remote.entity = {};
        }
        return;
    }
    for (std::size_t index = 0; index < rows.size() && first + index < visible.size(); ++index)
    {
        const auto& row = rows[index];
        const auto id = snapshot->entities[visible[first + index]].id;
        if (world.wasChanged(row.arrow))
        {
            if (!collapsed.erase(id))
                collapsed.insert(id);
            treeDirty = true;
        }
        else if (world.wasClicked(row.button))
            remote.entity = id;
    }
}

void updateRemoteScenePanel(ToolsState& state, EditorUiKit& kit, core::Duration delta,
                            std::uint32_t surface)
{
    kit.refreshTheme(themeColors());
    auto& ui = state.remoteScene.ui;
    if (!ui || ui->font != state.theme.fontSize)
        ui = std::make_shared<RemoteSceneUi>(kit, surface, state.theme.fontSize);
    ui->update(state, kit, delta);
}

void renderRemoteScenePanel(ToolsState& state, render::RenderWorld& world)
{
    if (state.remoteScene.ui && state.uiKit)
        state.remoteScene.ui->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
}

} // namespace devex::tools::detail
