// What the pages of the inspector share, and the pages that are only a few lines: nothing chosen, a
// code file, and an asset without a page of its own. Their pieces are those of the forms, in FormUi.cpp.
#include "InspectorUi.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <utility>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

std::vector<std::string> spriteDrops()
{
    return {"asset:sprite", "asset:texture"};
}

std::vector<asset::AssetId> droppedSprites(const ToolsState& state, const ui::Drop& dropped)
{
    const std::optional<core::Uuid> uuid = core::Uuid::parse(dropped.data);
    if (!uuid)
    {
        return {};
    }
    if (dropped.type == "asset:sprite")
    {
        return {asset::AssetId{*uuid}};
    }
    if (dropped.type == "asset:texture" && state.database != nullptr)
    {
        return spritesOfTexture(state, asset::AssetId{*uuid});
    }
    return {};
}

// ---- The import options, applied by Reimport ----

void ImportSettings::show(asset::AssetId asset)
{
    if (asset != m_asset)
    {
        m_asset = asset;
        m_waiting.clear();
    }
}

std::optional<serialization::TextValue> ImportSettings::value(const ToolsState& state, std::string_view key) const
{
    const auto found = std::ranges::find(m_waiting, key, &serialization::TextProperty::key);
    if (found != m_waiting.end())
    {
        return found->value;
    }
    return state.database != nullptr ? state.database->importOption(m_asset, key) : std::nullopt;
}

bool ImportSettings::boolean(const ToolsState& state, std::string_view key, bool fallback) const
{
    const std::optional<serialization::TextValue> found = value(state, key);
    return found ? serialization::asBool(*found).value_or(fallback) : fallback;
}

double ImportSettings::number(const ToolsState& state, std::string_view key, double fallback) const
{
    const std::optional<serialization::TextValue> found = value(state, key);
    return found ? serialization::asNumber(*found).value_or(fallback) : fallback;
}

std::string ImportSettings::text(const ToolsState& state, std::string_view key, std::string_view fallback) const
{
    const std::optional<serialization::TextValue> found = value(state, key);
    const std::string* const text = found ? serialization::asString(*found) : nullptr;
    return text != nullptr ? *text : std::string(fallback);
}

math::Vec4 ImportSettings::vector(const ToolsState& state, std::string_view key, math::Vec4 fallback) const
{
    const std::optional<serialization::TextValue> found = value(state, key);
    const auto* const call = found ? std::get_if<serialization::TextCall>(&*found) : nullptr;
    if (call == nullptr)
    {
        return fallback;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(call->arguments.size(), 4); ++index)
    {
        const auto axis = static_cast<math::Vec4::length_type>(index);
        fallback[axis] = static_cast<float>(serialization::asNumber(call->arguments[index]).value_or(fallback[axis]));
    }
    return fallback;
}

void ImportSettings::set(const ToolsState& state, std::string_view key, serialization::TextValue value)
{
    std::erase_if(m_waiting, [&](const serialization::TextProperty& property) { return property.key == key; });
    const std::optional<serialization::TextValue> written =
        state.database != nullptr ? state.database->importOption(m_asset, key) : std::nullopt;
    if (written && *written == value)
    {
        return;
    }
    m_waiting.push_back({std::string(key), std::move(value)});
}

bool ImportSettings::waits(std::string_view key) const
{
    return std::ranges::find(m_waiting, key, &serialization::TextProperty::key) != m_waiting.end();
}

std::size_t ImportSettings::waiting() const noexcept
{
    return m_waiting.size();
}

void ImportSettings::apply(ToolsState& state)
{
    if (state.database == nullptr)
    {
        return;
    }
    const core::Result<void> done = m_waiting.empty() ? state.database->reimport(m_asset) : state.database->setImportOptions(m_asset, m_waiting);
    if (!done)
    {
        DEVEX_LOG_ERROR("Cannot import the file again: {}", done.error());
        return;
    }
    m_waiting.clear();
}

void ImportSettings::revert() noexcept
{
    m_waiting.clear();
}

serialization::TextValue vectorValue(const float* values, int count)
{
    std::vector<serialization::TextValue> arguments;
    for (int index = 0; index < count; ++index)
    {
        arguments.emplace_back(std::stod(std::format("{}", values[index])));
    }
    return serialization::makeCall(std::format("vec{}", count), std::move(arguments));
}

ImportFooter importFooter(InspectorUi& ui, EditorUiKit& kit, Section& section)
{
    ImportFooter footer;
    const Entity row = ui.actions(&section);
    footer.reimport = ui.action(kit, row, Icon::Refresh, "Reimport", "primary");
    ui.tooltip(footer.reimport.entity, "Imports the file again, with the settings above");
    footer.revert = ui.action(kit, row, Icon::Undo, "Revert");
    ui.tooltip(footer.revert.entity, "Forgets the settings not applied yet");
    footer.note = ui.text(row, middle({ui.font * 12.0f, ui.line}), "", "dim");
    return footer;
}

void syncImportFooter(InspectorUi& ui, EditorUiKit& kit, const ImportFooter& footer, const ImportSettings& settings)
{
    static_cast<void>(kit);
    const std::size_t waiting = settings.waiting();
    ui.scene().get<UiRect>(footer.reimport.entity).style = waiting > 0 ? "primary" : "button";
    ui.enable(footer.revert, waiting > 0);
    ui.scene().get<scene::UiText>(footer.note).text =
        waiting == 0 ? std::string{} : std::format("{} {} waiting", waiting, waiting == 1 ? "change" : "changes");
}

void answerImportFooter(InspectorUi& ui, ToolsState& state, const ImportFooter& footer, ImportSettings& settings)
{
    const ui::UiWorld& world = ui.panel.world();
    if (world.wasClicked(footer.reimport.entity))
    {
        settings.apply(state);
    }
    else if (world.wasClicked(footer.revert.entity))
    {
        settings.revert();
    }
}

namespace {

// ---- Nothing chosen ----

class EmptyPage final : public InspectorPage
{
public:
    std::string signature(ToolsState&) override
    {
        return {};
    }

    void build(InspectorUi& ui, ToolsState&, EditorUiKit&) override
    {
        m_room = ui.add(ui.content, "Room", wide(1.0f));
        const Entity hint = ui.note(nullptr, "Select an entity to inspect it.");
        ui.scene().get<scene::UiText>(hint).align = scene::TextAlign::Center;
    }

    void sync(InspectorUi& ui, ToolsState&, EditorUiKit&) override
    {
        // A third of the way down, as the hint of ImGui stood.
        scene::UiRect& room = ui.scene().get<UiRect>(m_room);
        room.offsetMax.y = std::max(std::round(ui.panel.size().y * 0.3f), 1.0f);
    }

    void answer(InspectorUi&, ToolsState&, EditorUiKit&) override
    {
    }

private:
    Entity m_room;
};

// ---- A code file ----

class CodePage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        return std::format("{}", state.mode == ToolsMode::Editor);
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const EntityIcon look = codeIcon(state.selectedCode);
        std::string where = core::toUtf8(state.selectedCode);
        if (state.database != nullptr)
        {
            where = state.database->project().resourcePath(state.selectedCode);
        }
        ui.heading(kit, look.icon, look.color, core::toUtf8(state.selectedCode.filename()), where);
        const Entity row = ui.actions(nullptr);
        if (state.mode == ToolsMode::Editor)
        {
            m_edit = ui.action(kit, row, Icon::FileText, "Edit in Devex Script");
            ui.tooltip(m_edit.entity, "Opens the file in the Text Editor panel");
        }
        m_open = ui.action(kit, row, Icon::ExternalLink, "Open in External Editor");
        ui.note(nullptr, "Choose the script editor in Editor Settings. Saved scripts are compiled automatically.", "dim", 2.0f);
    }

    void sync(InspectorUi&, ToolsState&, EditorUiKit&) override
    {
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        const ui::UiWorld& world = ui.panel.world();
        if (m_edit.entity.isValid() && world.wasClicked(m_edit.entity))
        {
            openTextFile(state, state.selectedCode);
        }
        else if (world.wasClicked(m_open.entity))
        {
            openInCodeEditor(state, state.selectedCode);
        }
    }

private:
    Button m_edit;
    Button m_open;
};

// ---- Any other asset ----

[[nodiscard]] EntityIcon lookOf(asset::AssetType type)
{
    const ThemeColors& colors = themeColors();
    switch (type)
    {
    case asset::AssetType::Scene:
        return {icons::Clapperboard, colors.scene};
    case asset::AssetType::Mesh:
        return {icons::Box, colors.entity};
    case asset::AssetType::Material:
        return {icons::Palette, colors.material};
    case asset::AssetType::Sprite:
        return {icons::Image, colors.texture};
    case asset::AssetType::AnimationClip:
        return {icons::Film, colors.animation};
    case asset::AssetType::NavMesh:
        return {icons::Footprints, colors.physics};
    default:
        return {icons::File, colors.neutral};
    }
}

class AssetPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        return std::format("{}", source ? static_cast<int>(source->status) : -1);
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        const EntityIcon look = lookOf(info->type);
        ui.heading(kit, look.icon, look.color, info->name, source ? source->path : std::string{});
        const Entity row = ui.actions(nullptr);
        m_reimport = ui.action(kit, row, Icon::Refresh, "Reimport");
        if (source && source->status == asset::ImportStatus::Importing)
        {
            ui.note(nullptr, "Importing...");
        }
        else if (source && source->status == asset::ImportStatus::Failed)
        {
            ui.note(nullptr, "The file could not be imported.", "error");
            ui.note(nullptr, source->error, "dim", 3.0f);
        }
        ui.note(nullptr, std::format("A {} asset: drag it from FileSystem onto the fields that take one.", asset::toString(info->type)),
                "dim", 2.0f);
    }

    void sync(InspectorUi&, ToolsState&, EditorUiKit&) override
    {
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        if (ui.panel.world().wasClicked(m_reimport.entity))
        {
            if (core::Result<void> queued = state.database->reimport(state.selectedAsset); !queued)
            {
                DEVEX_LOG_WARNING("{}", queued.error());
            }
        }
    }

private:
    Button m_reimport;
};

} // namespace

std::unique_ptr<InspectorPage> makeEmptyPage()
{
    return std::make_unique<EmptyPage>();
}

std::unique_ptr<InspectorPage> makeCodePage()
{
    return std::make_unique<CodePage>();
}

std::unique_ptr<InspectorPage> makeAssetPage()
{
    return std::make_unique<AssetPage>();
}

} // namespace devex::tools::detail
