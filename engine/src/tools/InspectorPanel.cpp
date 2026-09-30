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

void drawInspectorPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Inspector");
    if (!beginDockedPanel(state, inspectorWindow))
    {
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
    updateInspectorUi(state, scene, core::Duration(state.input.delta()));
    endDockedPanel(state);
}

} // namespace devex::tools::detail
