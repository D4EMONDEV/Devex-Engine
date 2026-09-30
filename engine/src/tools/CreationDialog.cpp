// The window that creates an entity or adds a component, as Godot's Create New Node does, made with
// the interface of the engine and shaped as a palette: a search on top, the categories on the left,
// what matches in the middle and a card about the chosen one on the right.
#include "CreationCatalog.hpp"
#include "EditorModal.hpp"
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <format>
#include <optional>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

// The image the window is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t creationSurface = 4;
constexpr const char* creationPopup = "##creation";
// The entry that writes a new component of the game rather than adding one.
constexpr std::string_view newScriptKey = "action:new-script";
// How many components a card lists, the others being counted.
constexpr std::size_t cardComponents = 6;

// What the list on the left chooses: every entry, the favorites, the recent ones, or a category.
enum class Filter : std::uint8_t
{
    All,
    Favorites,
    Recent,
    Category,
};

struct Side
{
    Filter filter = Filter::All;
    CreationCategory category = CreationCategory::General;
    Button button;
    Entity count;
};

// One result and what it shows.
struct Row
{
    Button button;
    Entity iconBox;
    Entity icon;
    Entity name;
    Entity description;
    Entity tag;
    Entity star;
    Entity starIcon;
};

// A line of the card that names what an entity receives.
struct CardLine
{
    Entity line;
    Entity icon;
    Entity text;
};

[[nodiscard]] EntityIcon entryIcon(const CreationEntry& entry)
{
    const ThemeColors& colors = themeColors();
    if (entry.key == newScriptKey)
    {
        return {icons::FilePlus, colors.gameCode};
    }
    if (!entry.isPreset())
    {
        return componentIcon(entry.component);
    }
    const std::string_view name = entry.name;
    if (name == "Empty")
    {
        return {icons::Axis, colors.entity};
    }
    if (name == "2D camera")
    {
        return {icons::Video, colors.camera};
    }
    if (name.starts_with("Static"))
    {
        return {icons::SquareDashed, colors.physics};
    }
    if (name.starts_with("Rigid box"))
    {
        return {icons::Weight, colors.physics};
    }
    if (name.starts_with("Rigid"))
    {
        return {icons::CircleDashed, colors.physics};
    }
    if (name.starts_with("Trigger"))
    {
        return {icons::Scan, colors.physics};
    }
    return {icons::Box, colors.entity};
}

[[nodiscard]] Icon categoryIcon(CreationCategory category) noexcept
{
    switch (category)
    {
    case CreationCategory::General:
        return Icon::Axis;
    case CreationCategory::ThreeD:
        return Icon::Box;
    case CreationCategory::TwoD:
        return Icon::Image;
    case CreationCategory::Interface:
        return Icon::LayoutDashboard;
    case CreationCategory::Physics:
        return Icon::Weight;
    case CreationCategory::Physics2D:
        return Icon::SquareDashed;
    case CreationCategory::Audio:
        return Icon::Volume;
    case CreationCategory::Animation:
        return Icon::Film;
    case CreationCategory::Effects:
        return Icon::Sparkles;
    case CreationCategory::Navigation:
        return Icon::Footprints;
    case CreationCategory::GameCode:
    case CreationCategory::Count:
        break;
    }
    return Icon::FileCode;
}

[[nodiscard]] math::Vec4 withAlpha(math::Vec4 color, float alpha) noexcept
{
    return math::Vec4{color.x, color.y, color.z, alpha};
}

} // namespace

// The window and the entities the code reads and changes.
struct CreationDialogUi : PanelBuilder
{
    CreationDialogUi()
        : PanelBuilder(creationSurface)
    {
    }

    bool built = false;
    bool open = false;
    CreationRequest request;
    std::string subject;

    Entity title;
    Entity subtitle;
    Button close;
    Entity search;
    Entity sideColumn;
    std::vector<Side> sides;
    Entity scroll;
    Entity column;
    std::vector<Row> rows;
    Entity empty;
    Entity cardIconBox;
    Entity cardIcon;
    Entity cardName;
    Entity cardType;
    Entity cardChip;
    Entity cardChipText;
    Entity cardDescription;
    Entity cardAddsTitle;
    std::vector<CardLine> cardLines;
    Entity cardMore;
    Button cardFavorite;
    Entity hint;
    Button cancel;
    Button confirm;

    std::vector<CreationEntry> catalog;
    CreationMemory memory;
    std::filesystem::path memoryFile;
    std::vector<std::size_t> shown;
    std::size_t side = 0;
    std::string selectedKey;
    std::string lastSearch;
    bool scrollToSelection = false;

    void build(EditorUiKit& kit);
    void start(ToolsState& state, scene::Scene& edited, CreationRequest opened, EditorUiKit& kit);
    void filter();
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
    void choose(ToolsState& state, scene::Scene& edited, const CreationEntry& entry);

    [[nodiscard]] const CreationEntry* selectedEntry() const
    {
        const auto found = std::ranges::find(catalog, selectedKey, &CreationEntry::key);
        return found != catalog.end() ? &*found : nullptr;
    }

    [[nodiscard]] bool isFavorite(std::string_view key) const
    {
        return std::ranges::find(memory.favorites, key) != memory.favorites.end();
    }

    void saveMemory() const
    {
        if (!memoryFile.empty())
        {
            if (core::Result<void> saved = saveCreationMemory(memory, memoryFile); !saved)
            {
                DEVEX_LOG_WARNING("Cannot remember the favorites of the Create window: {}", saved.error());
            }
        }
    }
};

void CreationDialogUi::build(EditorUiKit& kit)
{
    built = true;
    const float line = font * 2.0f;
    const Entity root = add({}, "Palette", whole(), "dialog");
    scene().add<scene::UiImage>(root);
    scene().add<scene::UiLayout>(root, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = font * 0.8f,
                                                       .padding = math::Vec4{font * 1.3f},
                                                       .align = scene::TextAlign::Left});

    // What the window does and to what, and a way out.
    const Entity header = add(root, "Header", wide(font * 3.0f));
    title = text(header, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.6f, 0.58f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                 "Create Entity", "text", true, scene::TextAlign::Left, font * 1.3f);
    subtitle = text(header, UiRect{.anchorMin = {0.0f, 0.58f}, .anchorMax = {0.8f, 1.0f}, .offsetMin = {0.0f, font * 0.1f}, .offsetMax = {0.0f, 0.0f}},
                    "", "dim");
    const Entity corner = add(header, "Corner", UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-line, 0.0f}, .offsetMax = {0.0f, 0.0f}});
    scene().add<scene::UiLayout>(corner, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .align = scene::TextAlign::Right});
    close = button(kit, corner, Icon::Close, "", "side", line, line);
    tooltip(close.entity, "Close (Esc)");

    search = searchField(kit, root, wide(font * 2.6f), "", "Search components and presets");

    const Entity body = add(root, "Body", whole());
    scene().add<scene::UiLayout>(body, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                       .spacing = font * 1.0f,
                                                       .align = scene::TextAlign::Left});

    // The kinds of entries, as a column of icons and names.
    sideColumn = add(body, "Kinds", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {font * 11.0f, 0.0f}});
    scene().add<scene::UiLayout>(sideColumn, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                             .spacing = font * 0.15f,
                                                             .align = scene::TextAlign::Left});

    // What matches.
    const Entity list = add(body, "Results", whole(), "dialog_list");
    scene().add<scene::UiImage>(list, scene::UiImage{.raycastTarget = false});
    scroll = add(list, "Scroll", whole(math::Vec4{font * 0.35f}), "scroll");
    scene().add<scene::UiScroll>(scroll, scene::UiScroll{.speed = font * 6.0f});
    column = add(scroll, "Rows", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {-font * 0.8f, 0.0f}});
    scene().add<scene::UiLayout>(column, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                         .spacing = font * 0.25f,
                                                         .align = scene::TextAlign::Left});
    empty = text(list, whole(), "", "dim", false, scene::TextAlign::Center);

    // The card of the chosen entry.
    const Entity card = add(body, "Card", UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-font * 17.0f, 0.0f}, .offsetMax = {0.0f, 0.0f}}, "dialog_card");
    scene().add<scene::UiImage>(card, scene::UiImage{.raycastTarget = false});
    scene().add<scene::UiLayout>(card, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = font * 0.5f,
                                                       .padding = math::Vec4{font * 1.1f},
                                                       .align = scene::TextAlign::Left});
    cardIconBox = add(card, "Icon box", fixed({font * 4.0f, font * 4.0f}));
    scene().add<scene::UiImage>(cardIconBox, scene::UiImage{.cornerRadius = font * 0.9f, .raycastTarget = false});
    cardIcon = icon(kit, cardIconBox, whole(math::Vec4{font * 0.95f}), Icon::Box, {});
    cardName = text(card, wide(font * 1.9f), "", "text", true, scene::TextAlign::Left, font * 1.25f);
    cardType = text(card, wide(font * 1.2f), "", "dim");
    cardChip = add(card, "Chip", fixed({font * 6.0f, font * 1.6f}), "chip");
    scene().add<scene::UiImage>(cardChip, scene::UiImage{.raycastTarget = false});
    cardChipText = text(cardChip, whole(), "", "accent", false, scene::TextAlign::Center, font * 0.9f);
    cardDescription = text(card, wide(font * 5.0f), "", "text");
    scene::UiText& description = scene().get<scene::UiText>(cardDescription);
    description.wrap = true;
    description.verticalAlign = scene::TextVerticalAlign::Top;
    cardAddsTitle = text(card, wide(font * 1.4f), "Adds", "dim", true, scene::TextAlign::Left, font * 0.9f);
    for (std::size_t index = 0; index < cardComponents; ++index)
    {
        CardLine cardLine;
        cardLine.line = add(card, "Component", wide(font * 1.5f));
        cardLine.icon = icon(kit, cardLine.line, middle({font * 1.05f, font * 1.05f}), Icon::Box, {});
        cardLine.text = text(cardLine.line, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 1.6f, 0.0f}, .offsetMax = {0.0f, 0.0f}}, "", "text");
        cardLines.push_back(cardLine);
    }
    cardMore = text(card, wide(font * 1.4f), "", "dim");
    add(card, "Room", whole());
    cardFavorite = button(kit, card, Icon::Star, "Add to Favorites", "button", -1.0f, font * 2.0f, scene::TextAlign::Left);

    // How to go on.
    const Entity footer = add(root, "Footer", wide(font * 2.2f));
    scene().add<scene::UiLayout>(footer, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                         .spacing = font * 0.6f,
                                                         .align = scene::TextAlign::Left});
    hint = text(footer, grow(font * 2.2f), "Up and Down choose  \xC2\xB7  Enter creates  \xC2\xB7  Esc closes", "dim");
    cancel = button(kit, footer, std::nullopt, "Cancel", "button", font * 7.0f, font * 2.2f);
    confirm = button(kit, footer, Icon::Plus, "Create", "primary", font * 8.5f, font * 2.2f);
}

void CreationDialogUi::start(ToolsState& state, scene::Scene& edited, CreationRequest opened, EditorUiKit& kit)
{
    request = std::move(opened);
    open = true;
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    catalog = creationCatalog(registry);
    if (request.addComponent)
    {
        // Only the components that one of the entities lacks, which is what Add Component adds.
        std::vector<Entity> targets;
        for (const core::Uuid uuid : request.targets)
        {
            if (const Entity entity = edited.findEntity(uuid); entity.isValid())
            {
                targets.push_back(entity);
            }
        }
        const auto lacked = [&](std::string_view name) {
            const scene::ComponentType* const type = registry.find(name);
            return type != nullptr && std::ranges::any_of(targets, [&](Entity entity) { return type->find(edited, entity) == nullptr; });
        };
        std::erase_if(catalog, [&](const CreationEntry& entry) { return entry.isPreset() || !lacked(entry.component); });
        // The card names what the entities will receive, not what they already have.
        for (CreationEntry& entry : catalog)
        {
            std::erase_if(entry.components, [&](const std::string& name) { return !lacked(name); });
        }
        if (state.mode == ToolsMode::Editor && state.database != nullptr)
        {
            catalog.insert(catalog.begin(), CreationEntry{.key = std::string(newScriptKey),
                                                          .name = "New Script",
                                                          .category = CreationCategory::GameCode,
                                                          .description = "Writes a component of the game in a new file of "
                                                                         "the code folder, and adds it here once built."});
        }
        subject = targets.size() == 1 ? std::format("to {}", edited.name(targets.front()))
                                      : std::format("to {} entities", targets.size());
    }
    else
    {
        const Entity parent = edited.findEntity(request.parent);
        subject = parent.isValid() ? std::format("as a child of {}", edited.name(parent)) : "at the root of the scene";
    }

    // What this project remembers.
    memoryFile = state.database != nullptr ? state.database->project().cacheDirectory() / "creation.dvx" : std::filesystem::path();
    if (!memoryFile.empty())
    {
        memory = loadCreationMemory(memoryFile);
    }

    if (!built)
    {
        build(kit);
    }
    scene().get<scene::UiText>(search).text.clear();
    // Filtered on the first update, once the kinds on the left exist.
    lastSearch = "";
    side = 0;
    selectedKey.clear();
    scene().get<scene::UiScroll>(scroll).offset = math::Vec2{0.0f};
    panel.world().startEditing(scene(), search);
    filter();
}

void CreationDialogUi::filter()
{
    shown.clear();
    const std::string& searched = scene().get<scene::UiText>(search).text;
    if (!searched.empty())
    {
        // Every entry that matches, the best first.
        std::vector<std::pair<int, std::size_t>> scored;
        for (std::size_t index = 0; index < catalog.size(); ++index)
        {
            if (const int score = matchScore(catalog[index], searched); score > 0)
            {
                scored.emplace_back(-score, index);
            }
        }
        std::ranges::stable_sort(scored, {}, &std::pair<int, std::size_t>::first);
        for (const auto& [score, index] : scored)
        {
            shown.push_back(index);
        }
    }
    else if (side < sides.size())
    {
        const Side& chosen = sides[side];
        const auto byKeys = [&](const std::vector<std::string>& keys) {
            for (const std::string& key : keys)
            {
                const auto found = std::ranges::find(catalog, key, &CreationEntry::key);
                if (found != catalog.end())
                {
                    shown.push_back(static_cast<std::size_t>(found - catalog.begin()));
                }
            }
        };
        switch (chosen.filter)
        {
        case Filter::All:
            for (std::size_t index = 0; index < catalog.size(); ++index)
            {
                shown.push_back(index);
            }
            // By category, in the order of the list on the left.
            std::ranges::stable_sort(shown, {}, [&](std::size_t index) { return static_cast<int>(catalog[index].category); });
            break;
        case Filter::Favorites:
            byKeys(memory.favorites);
            break;
        case Filter::Recent:
            byKeys(memory.recent);
            break;
        case Filter::Category:
            for (std::size_t index = 0; index < catalog.size(); ++index)
            {
                if (catalog[index].category == chosen.category)
                {
                    shown.push_back(index);
                }
            }
            break;
        }
    }
    // The chosen entry stays chosen while it is shown; otherwise the first one is.
    if (std::ranges::none_of(shown, [&](std::size_t index) { return catalog[index].key == selectedKey; }))
    {
        selectedKey = shown.empty() ? std::string() : catalog[shown.front()].key;
        scrollToSelection = true;
    }
}

void CreationDialogUi::choose(ToolsState& state, scene::Scene& edited, const CreationEntry& entry)
{
    open = false;
    if (entry.key == newScriptKey)
    {
        state.openNewScriptPopup = true;
        return;
    }
    rememberCreation(memory, entry.key);
    saveMemory();
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    if (!request.addComponent)
    {
        requestCreatePreset(state, request.parent, entry.name.c_str(),
                            [&entry, &registry](scene::Scene& scratch, scene::Entity entity) { buildEntry(entry, registry, scratch, entity); });
        return;
    }
    // The component, and what it needs that an entity lacks, such as the rectangle of a button.
    std::vector<std::unique_ptr<Command>> commands;
    std::size_t receivers = 0;
    for (const core::Uuid uuid : request.targets)
    {
        const Entity entity = edited.findEntity(uuid);
        if (!entity.isValid())
        {
            continue;
        }
        bool received = false;
        for (const std::string& name : entry.components)
        {
            const scene::ComponentType* const type = registry.find(name);
            if (name != "Transform" && type != nullptr && type->find(edited, entity) == nullptr)
            {
                commands.push_back(makeAddComponentCommand(uuid, name));
                received = true;
            }
        }
        receivers += received ? 1 : 0;
    }
    if (!commands.empty())
    {
        state.pendingCommand = makeCompositeCommand(std::move(commands), receivers == 1 ? std::format("Add {}", entry.component)
                                                                                        : std::format("Add {} to {} entities", entry.component, receivers));
    }
}

void CreationDialogUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    font = state.theme.fontSize;
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();

    // The kinds on the left: everything, what the project keeps, then each category present.
    std::vector<std::pair<Filter, CreationCategory>> kinds{{Filter::All, CreationCategory::General},
                                                           {Filter::Favorites, CreationCategory::General},
                                                           {Filter::Recent, CreationCategory::General}};
    for (int category = 0; category < static_cast<int>(CreationCategory::Count); ++category)
    {
        if (std::ranges::any_of(catalog, [&](const CreationEntry& entry) { return static_cast<int>(entry.category) == category; }))
        {
            kinds.emplace_back(Filter::Category, static_cast<CreationCategory>(category));
        }
    }
    while (sides.size() < kinds.size())
    {
        // Placed by hand rather than in a row, the count against the right edge.
        Side made;
        made.button.entity = add(sideColumn, "Kind", wide(font * 2.0f), "side");
        scene().add<scene::UiImage>(made.button.entity);
        scene().add<scene::UiButton>(made.button.entity);
        made.button.icon = icon(kit, made.button.entity, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 0.6f, -font * 0.55f}, .offsetMax = {font * 1.7f, font * 0.55f}},
                                Icon::Shapes, "icon_dim");
        made.button.label = text(made.button.entity, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 2.3f, 0.0f}, .offsetMax = {-font * 2.6f, 0.0f}}, "", "text");
        made.count = text(made.button.entity, UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-font * 2.6f, 0.0f}, .offsetMax = {-font * 0.6f, 0.0f}},
                          "", "dim", false, scene::TextAlign::Right, font * 0.9f);
        sides.push_back(made);
    }
    const std::string searched = scene().get<scene::UiText>(search).text;
    for (std::size_t index = 0; index < sides.size(); ++index)
    {
        Side& item = sides[index];
        UiRect& rect = scene().get<UiRect>(item.button.entity);
        rect.visible = index < kinds.size();
        if (!rect.visible)
        {
            continue;
        }
        item.filter = kinds[index].first;
        item.category = kinds[index].second;
        const std::size_t count = item.filter == Filter::All       ? catalog.size()
                                  : item.filter == Filter::Favorites ? std::ranges::count_if(memory.favorites, [&](const std::string& key) {
                                                                            return std::ranges::find(catalog, key, &CreationEntry::key) != catalog.end();
                                                                        })
                                  : item.filter == Filter::Recent ? std::ranges::count_if(memory.recent, [&](const std::string& key) {
                                                                        return std::ranges::find(catalog, key, &CreationEntry::key) != catalog.end();
                                                                    })
                                                                  : static_cast<std::size_t>(std::ranges::count(catalog, item.category, &CreationEntry::category));
        const std::string_view label = item.filter == Filter::All       ? "All"
                                       : item.filter == Filter::Favorites ? "Favorites"
                                       : item.filter == Filter::Recent    ? "Recent"
                                                                          : categoryName(item.category);
        const Icon glyph = item.filter == Filter::All       ? Icon::Shapes
                           : item.filter == Filter::Favorites ? Icon::Star
                           : item.filter == Filter::Recent    ? Icon::Clock
                                                              : categoryIcon(item.category);
        scene().get<scene::UiText>(item.button.label).text = std::string(label);
        scene().get<scene::UiText>(item.count).text = std::format("{}", count);
        scene().get<scene::UiImage>(item.button.icon).texture = kit.icon(glyph);
        const bool current = searched.empty() && index == side;
        rect.style = current ? "side_selected" : "side";
        scene().get<UiRect>(item.button.icon).style = item.filter == Filter::Favorites ? "icon_favorite" : current ? "icon_accent" : "icon_dim";
    }

    // The entries, as the search and the kind choose them.
    if (searched != lastSearch)
    {
        lastSearch = searched;
        filter();
    }
    while (rows.size() < shown.size())
    {
        Row row;
        row.button.entity = add(column, "Result", wide(font * 3.2f), "soft_row");
        scene().add<scene::UiImage>(row.button.entity);
        scene().add<scene::UiButton>(row.button.entity);
        scene().get<UiRect>(row.button.entity).clipChildren = true;
        row.iconBox = add(row.button.entity, "Icon box", UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 0.5f, -font * 1.1f}, .offsetMax = {font * 2.7f, font * 1.1f}});
        scene().add<scene::UiImage>(row.iconBox, scene::UiImage{.cornerRadius = font * 0.5f, .raycastTarget = false});
        row.icon = icon(kit, row.iconBox, whole(math::Vec4{font * 0.5f}), Icon::Box, {});
        const Entity texts = add(row.button.entity, "Texts", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 3.3f, 0.0f}, .offsetMax = {-font * 8.5f, 0.0f}});
        scene().get<UiRect>(texts).clipChildren = true;
        row.name = text(texts, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {0.0f, font * 0.25f}, .offsetMax = {0.0f, 0.0f}}, "", "text");
        row.description = text(texts, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {0.0f, -font * 0.25f}}, "", "dim", false, scene::TextAlign::Left, font * 0.9f);
        row.tag = text(row.button.entity, UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-font * 8.3f, 0.0f}, .offsetMax = {-font * 2.6f, 0.0f}}, "", "dim", false, scene::TextAlign::Right, font * 0.85f);
        row.star = add(row.button.entity, "Star", UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-font * 2.3f, -font * 0.9f}, .offsetMax = {-font * 0.5f, font * 0.9f}}, "side");
        scene().add<scene::UiImage>(row.star);
        scene().add<scene::UiButton>(row.star);
        row.starIcon = icon(kit, row.star, whole(math::Vec4{font * 0.3f}), Icon::Star, "icon_dim");
        rows.push_back(row);
    }
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        const Row& row = rows[index];
        UiRect& rect = scene().get<UiRect>(row.button.entity);
        rect.visible = index < shown.size();
        if (!rect.visible)
        {
            continue;
        }
        const CreationEntry& entry = catalog[shown[index]];
        const EntityIcon look = entryIcon(entry);
        rect.style = entry.key == selectedKey ? "soft_row_selected" : "soft_row";
        const math::Vec4 tint = linearColor(look.color);
        scene().get<scene::UiImage>(row.iconBox).color = withAlpha(tint, 0.16f);
        scene::UiImage& glyph = scene().get<scene::UiImage>(row.icon);
        glyph.texture = kit.icon(iconOf(look.icon));
        glyph.color = tint;
        scene().get<scene::UiText>(row.name).text = entry.name;
        scene().get<scene::UiText>(row.description).text = entry.description;
        scene().get<scene::UiText>(row.tag).text = std::string(entry.isPreset() && entry.key != newScriptKey ? "Preset" : categoryName(entry.category));
        const bool favorite = isFavorite(entry.key);
        scene().get<UiRect>(row.star).visible = entry.key != newScriptKey;
        scene().get<UiRect>(row.starIcon).style = favorite ? "icon_favorite" : "icon_dim";
        tooltip(row.star, favorite ? "Remove from favorites" : "Add to favorites");
    }
    scene().get<scene::UiText>(empty).text =
        !shown.empty()             ? std::string()
        : !searched.empty()        ? "Nothing matches the search."
        : sides.size() > side && sides[side].filter == Filter::Favorites ? "No favorite yet: the star of an entry adds it here."
        : sides.size() > side && sides[side].filter == Filter::Recent    ? "Nothing created yet."
                                                                          : "Nothing to add.";

    // The card of the chosen entry.
    const CreationEntry* const chosen = selectedEntry();
    scene().get<UiRect>(cardIconBox).visible = chosen != nullptr;
    scene().get<UiRect>(cardChip).visible = chosen != nullptr;
    scene().get<UiRect>(cardAddsTitle).visible = chosen != nullptr && !chosen->components.empty();
    scene().get<UiRect>(cardFavorite.entity).visible = chosen != nullptr && chosen->key != newScriptKey;
    if (chosen != nullptr)
    {
        const EntityIcon look = entryIcon(*chosen);
        const math::Vec4 tint = linearColor(look.color);
        scene().get<scene::UiImage>(cardIconBox).color = withAlpha(tint, 0.18f);
        scene().get<scene::UiImage>(cardIcon).texture = kit.icon(iconOf(look.icon));
        scene().get<scene::UiImage>(cardIcon).color = tint;
        scene().get<scene::UiText>(cardName).text = chosen->name;
        scene().get<scene::UiText>(cardType).text = chosen->isPreset() ? (chosen->key == newScriptKey ? "C# or C++" : "Preset")
                                                                       : chosen->component;
        const std::string category(categoryName(chosen->category));
        scene().get<scene::UiText>(cardChipText).text = category;
        UiRect& chipRect = scene().get<UiRect>(cardChip);
        chipRect.offsetMax.x = chipRect.offsetMin.x + kit.textWidth(EditorUiKit::regularFont(), category, font * 0.9f) + font * 1.6f;
        scene().get<scene::UiText>(cardDescription).text = chosen->description;
        const asset::FontData* const regular = kit.fontData(EditorUiKit::regularFont());
        const float cardWidth = font * 17.0f - font * 2.2f;
        UiRect& descriptionRect = scene().get<UiRect>(cardDescription);
        descriptionRect.offsetMax.y =
            regular != nullptr ? ui::measureText(*regular, chosen->description, ui::TextStyle{.size = font}, cardWidth).y + 2.0f : font * 3.0f;
        scene().get<scene::UiText>(cardAddsTitle).text = request.addComponent ? "Adds what it lacks" : "The entity receives";
    }
    for (std::size_t index = 0; index < cardLines.size(); ++index)
    {
        const CardLine& cardLine = cardLines[index];
        const bool present = chosen != nullptr && index < chosen->components.size();
        scene().get<UiRect>(cardLine.line).visible = present;
        if (present)
        {
            const std::string& name = chosen->components[index];
            const EntityIcon look = componentIcon(name);
            scene::UiImage& glyph = scene().get<scene::UiImage>(cardLine.icon);
            glyph.texture = kit.icon(iconOf(look.icon));
            glyph.color = linearColor(look.color);
            scene().get<scene::UiText>(cardLine.text).text = name;
        }
    }
    const std::size_t more = chosen != nullptr && chosen->components.size() > cardComponents ? chosen->components.size() - cardComponents : 0;
    scene().get<UiRect>(cardMore).visible = more > 0;
    scene().get<scene::UiText>(cardMore).text = std::format("and {} more", more);
    if (chosen != nullptr)
    {
        relabel(kit, cardFavorite, isFavorite(chosen->key) ? "Remove from Favorites" : "Add to Favorites");
        scene().get<UiRect>(cardFavorite.icon).style = isFavorite(chosen->key) ? "icon_favorite" : "icon";
    }
    scene().get<scene::UiText>(title).text = request.addComponent ? "Add Component" : "Create Entity";
    scene().get<scene::UiText>(hint).text = request.addComponent
                                                ? "Up and Down choose  \xC2\xB7  Enter adds  \xC2\xB7  Esc closes"
                                                : "Up and Down choose  \xC2\xB7  Enter creates  \xC2\xB7  Esc closes";
    scene().get<scene::UiText>(subtitle).text = subject;
    relabel(kit, confirm, request.addComponent ? "Add" : "Create");
    enable(confirm, chosen != nullptr);

    // The chosen row stays in view as the keys move it.
    const ui::LaidOutRect* const view = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(scroll);
    const float viewHeight = view != nullptr ? view->size().y : 0.0f;
    const auto position = std::ranges::find_if(shown, [&](std::size_t index) { return catalog[index].key == selectedKey; });
    if (scrollToSelection && viewHeight > 0.0f && position != shown.end())
    {
        const float rowHeight = font * 3.2f + font * 0.25f;
        const float top = static_cast<float>(position - shown.begin()) * rowHeight;
        scene::UiScroll& scrolled = scene().get<scene::UiScroll>(scroll);
        if (top < scrolled.offset.y)
        {
            scrolled.offset.y = top;
        }
        else if (top + rowHeight > scrolled.offset.y + viewHeight)
        {
            scrolled.offset.y = top + rowHeight - viewHeight;
        }
    }
    scrollToSelection = false;

    panel.update(kit, delta, UiPanel::zoomFor(font));

    // What was asked this frame.
    std::optional<std::size_t> picked;
    for (std::size_t index = 0; index < rows.size() && index < shown.size(); ++index)
    {
        if (world.wasClicked(rows[index].star))
        {
            toggleFavorite(memory, catalog[shown[index]].key);
            saveMemory();
        }
        else if (world.wasClicked(rows[index].button.entity))
        {
            selectedKey = catalog[shown[index]].key;
            if (world.wasDoubleClicked(rows[index].button.entity))
            {
                picked = shown[index];
            }
        }
    }
    for (std::size_t index = 0; index < sides.size() && index < kinds.size(); ++index)
    {
        if (world.wasClicked(sides[index].button.entity))
        {
            side = index;
            scene().get<scene::UiText>(search).text.clear();
            lastSearch.clear();
            scene().get<scene::UiScroll>(scroll).offset = math::Vec2{0.0f};
            filter();
            panel.world().startEditing(scene(), search);
        }
    }
    if (world.wasClicked(cardFavorite.entity) && chosen != nullptr)
    {
        toggleFavorite(memory, chosen->key);
        saveMemory();
        if (sides.size() > side && sides[side].filter == Filter::Favorites)
        {
            filter();
        }
    }
    if (world.wasClicked(close.entity) || world.wasClicked(cancel.entity))
    {
        open = false;
    }

    // The keys: the arrows walk the results, Enter takes the chosen one, Escape closes.
    if (panel.focused() && !shown.empty())
    {
        const auto current = std::ranges::find_if(shown, [&](std::size_t index) { return catalog[index].key == selectedKey; });
        std::size_t at = current != shown.end() ? static_cast<std::size_t>(current - shown.begin()) : 0;
        const std::size_t page = std::max<std::size_t>(static_cast<std::size_t>(viewHeight / (font * 3.45f)), 1);
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        {
            at = std::min(at + 1, shown.size() - 1);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        {
            at = at > 0 ? at - 1 : 0;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true))
        {
            at = std::min(at + page, shown.size() - 1);
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true))
        {
            at = at > page ? at - page : 0;
        }
        if (catalog[shown[at]].key != selectedKey)
        {
            selectedKey = catalog[shown[at]].key;
            scrollToSelection = true;
        }
    }
    if (panel.focused() && (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)))
    {
        if (const CreationEntry* const entry = selectedEntry())
        {
            picked = static_cast<std::size_t>(entry - catalog.data());
        }
    }
    if (world.wasClicked(confirm.entity))
    {
        if (const CreationEntry* const entry = selectedEntry())
        {
            picked = static_cast<std::size_t>(entry - catalog.data());
        }
    }
    if (panel.focused() && ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        open = false;
    }
    if (picked)
    {
        const CreationEntry entry = catalog[*picked];
        choose(state, edited, entry);
    }
    // Typing keeps going to the search, wherever the pointer clicked.
    if (open && !world.isEditing())
    {
        world.startEditing(scene(), search, false);
    }
}

void openCreateEntity(ToolsState& state, core::Uuid parent)
{
    state.creationRequest = CreationRequest{.parent = parent};
}

void openAddComponent(ToolsState& state, std::vector<core::Uuid> targets)
{
    state.creationRequest = CreationRequest{.addComponent = true, .targets = std::move(targets)};
}

void drawCreationDialog(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Create window");
    if (state.creationRequest)
    {
        if (!state.uiKit)
        {
            state.uiKit = std::make_shared<EditorUiKit>(state.renderer, state.icons,
                                                        state.platform.baseDirectory() / "resources" / "fonts");
        }
        if (!state.creationDialog)
        {
            state.creationDialog = std::make_shared<CreationDialogUi>();
        }
        state.creationDialog->font = state.theme.fontSize;
        state.creationDialog->start(state, scene, std::move(*state.creationRequest), *state.uiKit);
        state.creationRequest.reset();
        openModal(state, creationPopup);
    }
    if (!state.creationDialog || !state.creationDialog->open)
    {
        closeModal(state, creationPopup);
        return;
    }

    // In the middle of the window, as large as it comfortably holds, over a veil.
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    // Points of text, scaled as ImGui scales its own.
    const float unit = ImGui::GetFontSize() / std::max(regularFontPixels(state.theme.fontSize), 1.0f);
    const ImVec2 size(std::clamp(viewport->WorkSize.x * 0.68f, std::min(unit * 780.0f, viewport->WorkSize.x), unit * 1150.0f),
                      std::clamp(viewport->WorkSize.y * 0.72f, std::min(unit * 520.0f, viewport->WorkSize.y), unit * 760.0f));
    if (!beginModal(state, creationPopup, size))
    {
        return;
    }
    state.creationDialog->update(state, *state.uiKit, scene, core::Duration(ImGui::GetIO().DeltaTime));
    endModal();
    if (!state.creationDialog->open)
    {
        closeModal(state, creationPopup);
    }
}

void renderCreationDialog(ToolsState& state, render::RenderWorld& world)
{
    if (state.creationDialog && state.uiKit)
    {
        // Clear around the rounded card, which the veil shows through.
        state.creationDialog->panel.render(*state.uiKit, world, math::Vec4{0.0f});
    }
}

} // namespace devex::tools::detail
