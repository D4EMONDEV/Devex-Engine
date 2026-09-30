// The modal windows of the editor, made with the interface of the engine: a veil over everything, and
// in its middle the card of the dialog, which its own panel fills. The windows of settings stand in
// a frame with their title and a cross, as Godot's dialogs do. The last modal opened is the one
// shown; the others wait under it, and the editor answers none of its shortcuts meanwhile.
#pragma once

#include <devex/core/Export.hpp>

#include <imgui.h>

#include <string_view>

namespace devex::render {
struct RenderWorld;
}

namespace devex::tools::detail {

struct ToolsState;

// Puts a modal on top of the others; it stays open until closeModal, or until a frame passes
// without asking for it.
DEVEX_API void openModal(ToolsState& state, std::string_view id);
DEVEX_API void closeModal(ToolsState& state, std::string_view id);
[[nodiscard]] DEVEX_API bool isModalOpen(const ToolsState& state) noexcept;
// Forgets the modals that were not asked for since the last call, once a frame.
DEVEX_API void pruneModals(ToolsState& state);

// Opens the window of a modal that is open, `size` points large without its title; false while
// another one is on top of it. An endModal follows a true only.
[[nodiscard]] DEVEX_API bool beginModal(ToolsState& state, std::string_view id, ImVec2 size, std::string_view title = {});
DEVEX_API void endModal();
// Whether the cross of a titled modal, or Escape, asked to close it since the last call.
[[nodiscard]] DEVEX_API bool takeModalClose(ToolsState& state, std::string_view id);

DEVEX_API void renderModalLayer(ToolsState& state, render::RenderWorld& world);

} // namespace devex::tools::detail
