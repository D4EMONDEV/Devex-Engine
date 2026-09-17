#pragma once

#include "Icons.hpp"
#include "Theme.hpp"

#include <imgui.h>

#include <optional>
#include <string>

namespace devex::tools::detail {

// A square button showing an icon, flat until hovered. A selected button stays highlighted, as the
// active tool of a toolbar.
bool toolButton(const char* id, IconText icon, const char* tooltip, bool selected = false, bool enabled = true,
                std::optional<ImVec4> iconColor = std::nullopt);

// A button with an icon before its label. A width of zero fits the contents.
bool labelButton(IconText icon, const char* label, float width = 0.0f, bool enabled = true);

// The button of the main action of a dialog, in the accent color.
bool primaryButton(IconText icon, const char* label, float width = 0.0f, bool enabled = true);

// ImGui::BeginCombo drawn as a field with a chevron, without the arrow button.
[[nodiscard]] bool beginCombo(const char* id, const char* preview, ImGuiComboFlags flags = ImGuiComboFlags_None);

// A text field with a search icon and a hint. A negative width fills the line.
bool searchField(const char* id, std::string& text, const char* hint, float width = -1.0f);

// Draws an icon in an sRGB color, then keeps the cursor on the same line.
void iconLabel(IconText icon, ImVec4 color);

// A thin vertical line between groups of toolbar buttons.
void toolbarSeparator();

// Text in the bold font.
void boldText(const char* text);

// Places the cursor so that items of the given width end at the right of the line.
void alignRight(float width);

// The width of a toolbar button.
[[nodiscard]] float toolButtonWidth() noexcept;

// Properties in two columns, names on the left and their editors filling the right. Each property
// starts with propertyName; the editor that follows fills the value column. A highlighted property,
// such as a value that differs from its prefab, has a bar in the accent color and its name in bold.
[[nodiscard]] bool beginProperties(const char* id);
void propertyName(const char* name, bool highlighted = false);
void endProperties();

// Edits the components of a vector, each labelled with its axis letter in the axis color. Acts as one
// item for IsItemActivated and IsItemDeactivatedAfterEdit.
bool dragVector(const char* id, float* values, int count, float speed, const char* format = "%.3f");

// Opens the fonts of the editor for the rest of the frame.
void setEditorFonts(const EditorFonts& fonts) noexcept;
[[nodiscard]] const EditorFonts& editorFonts() noexcept;

} // namespace devex::tools::detail
