// The Inspector window: the entities, made with the interface of the engine in InspectorUi.cpp, and
// the assets, the code files and the elements of the Animator panel, still drawn with ImGui.
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <string>

namespace devex::tools::detail {
namespace {

constexpr std::array builtinMeshes{
    BuiltinAsset{"Cube", asset::builtin::cubeMesh},
    BuiltinAsset{"Sphere", asset::builtin::sphereMesh},
    BuiltinAsset{"Plane", asset::builtin::planeMesh},
};

// The share of the window the tile painter takes under the entity.
constexpr float painterShare = 0.45f;

} // namespace

std::span<const BuiltinAsset> builtinMeshAssets() noexcept
{
    return builtinMeshes;
}

std::string assetLabel(const ToolsState& state, asset::AssetId id)
{
    if (!id.isValid())
    {
        return "(none)";
    }
    for (const BuiltinAsset& builtin : builtinMeshes)
    {
        if (builtin.id == id)
        {
            return builtin.name;
        }
    }
    if (const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(id) : nullptr)
    {
        return info->name;
    }
    return id.uuid.toString();
}

bool drawAssetPicker(ToolsState& state, const char* id, std::optional<asset::AssetType> type, asset::AssetId& value,
                     bool mixed)
{
    bool changed = false;
    const auto choose = [&](const char* name, asset::AssetId candidate) {
        ImGui::PushID(name);
        if (ImGui::Selectable(name, candidate == value) && candidate != value)
        {
            value = candidate;
            changed = true;
        }
        ImGui::PopID();
    };

    const std::string preview = mixed ? std::string(mixedValue) : assetLabel(state, value);
    if (beginCombo(id, preview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        choose("(none)", asset::AssetId{});
        if (!type || *type == asset::AssetType::Mesh)
        {
            for (const BuiltinAsset& builtin : builtinMeshes)
            {
                choose(builtin.name, builtin.id);
            }
        }
        if (state.database != nullptr)
        {
            ImGui::Separator();
            for (const asset::AssetInfo& info : state.database->assets(type))
            {
                ImGui::PushID(info.id.uuid.toString().c_str());
                choose(info.name.c_str(), info.id);
                ImGui::PopID();
            }
        }
        ImGui::EndCombo();
    }
    if (const std::optional<asset::AssetId> dropped = acceptDroppedAsset(type); dropped && *dropped != value)
    {
        value = *dropped;
        changed = true;
    }
    return changed;
}

namespace {

// What ImGui still draws in the window: an element of the Animator panel, a code file, an asset,
// or the hint that nothing is selected. It scrolls in a child of its own, since the window does not.
void drawImGuiInspector(ToolsState& state)
{
    if (!ImGui::BeginChild("##inspected", ImVec2(0.0f, 0.0f)))
    {
        ImGui::EndChild();
        return;
    }
    if (drawAnimatorElementInspector(state))
    {
    }
    else if (!state.selectedCode.empty())
    {
        drawCodeInspector(state);
    }
    else if (state.selectedAsset.isValid())
    {
        const asset::AssetInfo* const selected = state.database != nullptr ? state.database->find(state.selectedAsset) : nullptr;
        if (selected != nullptr && selected->type == asset::AssetType::Model)
        {
            drawModelInspector(state);
        }
        else if (selected != nullptr && selected->type == asset::AssetType::Curve)
        {
            drawCurveInspector(state);
        }
        else if (selected != nullptr && selected->type == asset::AssetType::Texture)
        {
            drawTextureInspector(state);
        }
        else if (selected != nullptr && selected->type == asset::AssetType::SpriteFrames)
        {
            drawSpriteFramesInspector(state);
        }
        else if (selected != nullptr && selected->type == asset::AssetType::Tileset)
        {
            drawTilesetInspector(state);
        }
        else if (selected != nullptr && selected->type == asset::AssetType::Animator)
        {
            drawAnimatorInspector(state);
        }
        else
        {
            drawAudioClipInspector(state);
        }
    }
    else
    {
        const char* const hint = "Select an entity to inspect it.";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        ImGui::SetCursorPos(ImVec2(std::max(0.0f, (ImGui::GetWindowWidth() - size.x) * 0.5f), ImGui::GetWindowHeight() * 0.35f));
        ImGui::TextDisabled("%s", hint);
    }
    ImGui::EndChild();
}

} // namespace

void drawInspectorPanel(ToolsState& state, scene::Scene& scene)
{
    if (!ImGui::Begin(inspectorWindow, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }
    const scene::Entity entity = scene.findEntity(state.selection.active());
    if (entity.isValid())
    {
        state.selectedCode.clear();
        state.selectedAsset = {};
    }
    // A clip plays while its inspector shows.
    if (state.previewedClip.isValid() && state.previewedClip != state.selectedAsset)
    {
        stopAudioPreview(state);
    }
    // A state or a transition chosen in the Animator panel takes the window, as an asset does, until
    // another entity or asset is chosen.
    AnimatorEditor& animator = state.animatorEditor;
    if (animator.inspecting && (state.selection.active() != animator.inspectedEntity || state.selectedAsset != animator.inspectedAsset))
    {
        animator.inspecting = false;
    }
    const bool animatorElement = animator.inspecting && state.showAnimator &&
                                 (animator.selected == AnimatorElement::State || animator.selected == AnimatorElement::Transition);
    if (!entity.isValid() || animatorElement)
    {
        drawImGuiInspector(state);
        ImGui::End();
        return;
    }

    if (!state.uiKit)
    {
        state.uiKit = std::make_shared<EditorUiKit>(state.renderer, state.icons,
                                                    state.platform.baseDirectory() / "resources" / "fonts");
    }
    if (!state.inspectorUi)
    {
        state.inspectorUi = makeInspectorUi();
    }
    const core::Duration delta(ImGui::GetIO().DeltaTime);
    // A tilemap is painted with the tools under the entity, as Godot paints in a panel of its own.
    if (state.selection.size() == 1 && scene.has<scene::Tilemap>(entity))
    {
        const float height = ImGui::GetContentRegionAvail().y;
        const float painter = std::clamp(std::round(height * painterShare), ImGui::GetFrameHeight() * 6.0f, height * 0.7f);
        if (ImGui::BeginChild("##entity", ImVec2(0.0f, height - painter), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            updateInspectorUi(state, scene, delta);
        }
        ImGui::EndChild();
        if (ImGui::BeginChild("##painter", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders))
        {
            drawTilePainter(state, scene, entity);
        }
        ImGui::EndChild();
    }
    else
    {
        updateInspectorUi(state, scene, delta);
    }
    ImGui::End();
}

} // namespace devex::tools::detail
