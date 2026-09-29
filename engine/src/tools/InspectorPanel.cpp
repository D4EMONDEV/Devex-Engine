// The Inspector window, made with the interface of the engine: the entities in InspectorUi.cpp, and the
// assets, the code files and the elements of the Animator panel in the pages of their kinds. Here are
// the window, and the pickers of assets other panels still draw with ImGui.
#include "EditorUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>
#include <devex/asset/AssetId.hpp>

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

void drawInspectorPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Inspector");
    if (!ImGui::Begin(inspectorWindow, nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        ImGui::End();
        return;
    }
    // The inspector shows one thing at a time: an entity chosen leaves the asset and the code file.
    if (scene.findEntity(state.selection.active()).isValid())
    {
        state.selectedCode.clear();
        state.selectedAsset = {};
    }
    // A clip plays while its inspector shows.
    if (state.previewedClip.isValid() && state.previewedClip != state.selectedAsset)
    {
        stopAudioPreview(state);
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
    updateInspectorUi(state, scene, core::Duration(ImGui::GetIO().DeltaTime));
    ImGui::End();
}

} // namespace devex::tools::detail
