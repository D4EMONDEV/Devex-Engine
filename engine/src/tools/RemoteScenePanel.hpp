#pragma once

#include "EditorUi.hpp"

namespace devex::tools::detail
{

struct ToolsState;

struct SceneSourceTabs
{
    scene::Entity row;
    PanelButton other;
};

[[nodiscard]] SceneSourceTabs addSceneSourceTabs(PanelBuilder& ui, EditorUiKit& kit,
                                                 scene::Entity parent, bool remote);
void updateRemoteScenePanel(ToolsState& state, EditorUiKit& kit, core::Duration delta,
                            std::uint32_t surface);
void renderRemoteScenePanel(ToolsState& state, render::RenderWorld& world);

} // namespace devex::tools::detail
