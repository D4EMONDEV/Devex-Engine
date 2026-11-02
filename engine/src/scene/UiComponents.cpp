#include <devex/scene/UiComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(Canvas)
{
    type.field("scale_mode", &Canvas::scaleMode, {.group = "Canvas"})
        .field("reference_resolution", &Canvas::referenceResolution)
        .field("theme", &Canvas::theme, {.assetType = "theme"})
        .field("match_width_or_height", &Canvas::matchWidthOrHeight)
        .field("sort_order", &Canvas::sortOrder, {.group = "Ordering"})
        .field("visible", &Canvas::visible, {.group = "Visibility and Input"})
        .field("interactive", &Canvas::interactive);
}

DEVEX_REFLECT(UiRect)
{
    type.field("anchor_min", &UiRect::anchorMin, {.group = "Anchors"})
        .field("anchor_max", &UiRect::anchorMax)
        .field("offset_min", &UiRect::offsetMin, {.group = "Offsets"})
        .field("offset_max", &UiRect::offsetMax)
        .field("pivot", &UiRect::pivot, {.group = "Transform"})
        .field("scale", &UiRect::scale)
        .field("rotation", &UiRect::rotation, {.angle = true})
        .field("opacity", &UiRect::opacity, {.group = "Visibility"})
        .field("visible", &UiRect::visible)
        .field("clip_children", &UiRect::clipChildren)
        .field("style", &UiRect::style, {.group = "Theme"});
}

DEVEX_REFLECT(UiScroll)
{
    type.field("offset", &UiScroll::offset, {.group = "Scrolling"})
        .field("horizontal", &UiScroll::horizontal)
        .field("vertical", &UiScroll::vertical)
        .field("speed", &UiScroll::speed)
        .field("scrollbar", &UiScroll::scrollbar, {.group = "Scrollbar"})
        .field("scrollbar_size", &UiScroll::scrollbarSize)
        .field("scrollbar_color", &UiScroll::scrollbarColor, {.color = true});
}

DEVEX_REFLECT(UiPopup)
{
    type.field("kind", &UiPopup::kind, {.group = "Popup"})
        .field("veil_color", &UiPopup::veilColor, {.color = true});
}

DEVEX_REFLECT(UiContextMenu)
{
    type.field("popup", &UiContextMenu::popup, {.group = "Context Menu"});
}

DEVEX_REFLECT(UiTooltip)
{
    type.field("text", &UiTooltip::text, {.group = "Tooltip"}).field("delay", &UiTooltip::delay);
}

DEVEX_REFLECT(UiDropdown)
{
    type.field("options", &UiDropdown::options, {.group = "Options"})
        .field("selected", &UiDropdown::selected)
        .field("list_color", &UiDropdown::listColor, {.color = true, .group = "Appearance"})
        .field("highlight_color", &UiDropdown::highlightColor, {.color = true})
        .field("arrow_color", &UiDropdown::arrowColor, {.color = true})
        .field("list_shadow_color", &UiDropdown::listShadowColor,
               {.color = true, .group = "Shadow"})
        .field("list_shadow_size", &UiDropdown::listShadowSize)
        .field("interactable", &UiDropdown::interactable, {.group = "Interaction"})
        .field("action", &UiDropdown::action)
        .field("placeholder", &UiDropdown::placeholder, {.group = "Placeholder"});
}

DEVEX_REFLECT(UiPlot)
{
    type.field("values", &UiPlot::values, {.group = "Data"})
        .field("min_value", &UiPlot::minValue)
        .field("max_value", &UiPlot::maxValue)
        .field("kind", &UiPlot::kind, {.group = "Appearance"})
        .field("color", &UiPlot::color, {.color = true})
        .field("colors", &UiPlot::colors, {.color = true})
        .field("back_values", &UiPlot::backValues, {.group = "Background"})
        .field("back_color", &UiPlot::backColor, {.color = true})
        .field("guides", &UiPlot::guides, {.group = "Guides"})
        .field("guide_color", &UiPlot::guideColor, {.color = true})
        .field("highlighted", &UiPlot::highlighted, {.group = "Highlight"})
        .field("highlight_color", &UiPlot::highlightColor, {.color = true})
        .field("line_width", &UiPlot::lineWidth, {.group = "Lines and Marker"})
        .field("marker", &UiPlot::marker)
        .field("marker_color", &UiPlot::markerColor, {.color = true});
}

DEVEX_REFLECT(UiTextArea)
{
    type.field("selection_color", &UiTextArea::selectionColor,
               {.color = true, .group = "Selection"})
        .field("caret_color", &UiTextArea::caretColor, {.color = true})
        .field("padding", &UiTextArea::padding, {.group = "Layout"})
        .field("scroll", &UiTextArea::scroll)
        .field("line_numbers", &UiTextArea::lineNumbers, {.group = "Gutter"})
        .field("line_number_color", &UiTextArea::lineNumberColor, {.color = true})
        .field("current_line_color", &UiTextArea::currentLineColor, {.color = true})
        .field("tab_size", &UiTextArea::tabSize, {.group = "Editing"})
        .field("auto_indent", &UiTextArea::autoIndent)
        .field("indent_after", &UiTextArea::indentAfter)
        .field("read_only", &UiTextArea::readOnly)
        .field("scrollbar_size", &UiTextArea::scrollbarSize, {.group = "Scrollbar"})
        .field("scrollbar_color", &UiTextArea::scrollbarColor, {.color = true})
        .field("interactable", &UiTextArea::interactable, {.group = "Interaction"})
        .field("action", &UiTextArea::action);
}

DEVEX_REFLECT(UiLine)
{
    type.field("points", &UiLine::points, {.group = "Shape"})
        .field("width", &UiLine::width)
        .field("color", &UiLine::color, {.color = true})
        .field("arrow", &UiLine::arrow, {.group = "Arrow"})
        .field("arrow_size", &UiLine::arrowSize)
        .field("closed", &UiLine::closed, {.group = "Outline"});
}

DEVEX_REFLECT(UiNumberField)
{
    type.field("value", &UiNumberField::value, {.group = "Value"})
        .field("min_value", &UiNumberField::minValue)
        .field("max_value", &UiNumberField::maxValue)
        .field("step", &UiNumberField::step)
        .field("drag_speed", &UiNumberField::dragSpeed)
        .field("decimals", &UiNumberField::decimals, {.group = "Format"})
        .field("format", &UiNumberField::format)
        .field("interactable", &UiNumberField::interactable, {.group = "Interaction"})
        .field("action", &UiNumberField::action);
}

DEVEX_REFLECT(UiColorPicker)
{
    type.field("color", &UiColorPicker::color, {.color = true, .group = "Color"})
        .field("alpha", &UiColorPicker::alpha)
        .field("bar_size", &UiColorPicker::barSize, {.group = "Layout"})
        .field("spacing", &UiColorPicker::spacing)
        .field("interactable", &UiColorPicker::interactable, {.group = "Interaction"})
        .field("action", &UiColorPicker::action);
}

DEVEX_REFLECT(UiSplitter)
{
    type.field("vertical", &UiSplitter::vertical, {.group = "Split"})
        .field("position", &UiSplitter::position)
        .field("min_size", &UiSplitter::minSize)
        .field("bar_size", &UiSplitter::barSize, {.group = "Handle"})
        .field("bar_color", &UiSplitter::barColor, {.color = true})
        .field("hover_color", &UiSplitter::hoverColor, {.color = true});
}

DEVEX_REFLECT(UiFoldout)
{
    type.field("expanded", &UiFoldout::expanded, {.group = "Foldout"})
        .field("content", &UiFoldout::content)
        .field("arrow_color", &UiFoldout::arrowColor, {.color = true, .group = "Appearance"})
        .field("interactable", &UiFoldout::interactable, {.group = "Interaction"})
        .field("action", &UiFoldout::action);
}

DEVEX_REFLECT(UiVirtualList)
{
    type.field("item_count", &UiVirtualList::itemCount, {.group = "Items"})
        .field("item_size", &UiVirtualList::itemSize)
        .field("first", &UiVirtualList::first, {.runtime = true});
}

DEVEX_REFLECT(UiTable)
{
    type.field("columns", &UiTable::columns, {.group = "Columns"})
        .field("resizable", &UiTable::resizable)
        .field("sort_column", &UiTable::sortColumn, {.group = "Sorting"})
        .field("sort_ascending", &UiTable::sortAscending)
        .field("arrow_color", &UiTable::arrowColor, {.color = true, .group = "Appearance"})
        .field("action", &UiTable::action, {.group = "Interaction"});
}

DEVEX_REFLECT(UiTableRow) { type.field("header", &UiTableRow::header, {.group = "Row"}); }

DEVEX_REFLECT(UiDragSource)
{
    type.field("type", &UiDragSource::type, {.group = "Payload"})
        .field("data", &UiDragSource::data)
        .field("label", &UiDragSource::label)
        .field("interactable", &UiDragSource::interactable, {.group = "Interaction"});
}

DEVEX_REFLECT(UiDropTarget)
{
    type.field("accepts", &UiDropTarget::accepts, {.group = "Payload"})
        .field("highlight_color", &UiDropTarget::highlightColor,
               {.color = true, .group = "Appearance"})
        .field("action", &UiDropTarget::action, {.group = "Interaction"});
}

DEVEX_REFLECT(UiImage)
{
    type.field("texture", &UiImage::texture, {.assetType = "texture", .group = "Image"})
        .field("color", &UiImage::color, {.color = true})
        .field("border", &UiImage::border)
        .field("corner_radius", &UiImage::cornerRadius, {.group = "Corners"})
        .field("outline_color", &UiImage::outlineColor, {.color = true, .group = "Outline"})
        .field("outline_width", &UiImage::outlineWidth)
        .field("shadow_color", &UiImage::shadowColor, {.color = true, .group = "Shadow"})
        .field("shadow_size", &UiImage::shadowSize)
        .field("shadow_offset", &UiImage::shadowOffset)
        .field("preserve_aspect", &UiImage::preserveAspect, {.group = "Layout and Input"})
        .field("raycast_target", &UiImage::raycastTarget);
}

DEVEX_REFLECT(UiText)
{
    type.field("text", &UiText::text, {.group = "Text"})
        .field("translate", &UiText::translate)
        .field("font", &UiText::font, {.assetType = "font", .group = "Typography"})
        .field("size", &UiText::size)
        .field("color", &UiText::color, {.color = true})
        .field("align", &UiText::align, {.group = "Alignment"})
        .field("vertical_align", &UiText::verticalAlign)
        .field("wrap", &UiText::wrap)
        .field("line_spacing", &UiText::lineSpacing)
        .field("outline_color", &UiText::outlineColor, {.color = true, .group = "Outline"})
        .field("outline_width", &UiText::outlineWidth)
        .field("raycast_target", &UiText::raycastTarget, {.group = "Input"})
        .field("rich", &UiText::rich, {.group = "Rich Text"})
        .field("icons", &UiText::icons, {.assetType = "texture"});
}

DEVEX_REFLECT(UiInput)
{
    type.field("placeholder", &UiInput::placeholder, {.group = "Placeholder"})
        .field("placeholder_color", &UiInput::placeholderColor, {.color = true})
        .field("selection_color", &UiInput::selectionColor,
               {.color = true, .group = "Editing Colors"})
        .field("caret_color", &UiInput::caretColor, {.color = true})
        .field("focus_color", &UiInput::focusColor, {.color = true})
        .field("focus_width", &UiInput::focusWidth)
        .field("padding", &UiInput::padding, {.group = "Editing"})
        .field("multiline", &UiInput::multiline)
        .field("password", &UiInput::password)
        .field("max_length", &UiInput::maxLength)
        .field("interactable", &UiInput::interactable, {.group = "Interaction"})
        .field("action", &UiInput::action);
}

DEVEX_REFLECT(UiButton)
{
    type.field("action", &UiButton::action, {.group = "Interaction"})
        .field("interactable", &UiButton::interactable)
        .field("hover_color", &UiButton::hoverColor, {.color = true, .group = "Appearance"})
        .field("pressed_color", &UiButton::pressedColor, {.color = true})
        .field("disabled_color", &UiButton::disabledColor, {.color = true})
        .field("fade_time", &UiButton::fadeTime);
}

DEVEX_REFLECT(UiBinding)
{
    type.field("component", &UiBinding::component, {.group = "Binding"})
        .field("field", &UiBinding::field)
        .field("format", &UiBinding::format, {.group = "Format"})
        .field("decimals", &UiBinding::decimals)
        .field("source", &UiBinding::source, {.group = "Source"});
}

DEVEX_REFLECT(UiSlider)
{
    type.field("value", &UiSlider::value, {.group = "Range"})
        .field("min_value", &UiSlider::minValue)
        .field("max_value", &UiSlider::maxValue)
        .field("step", &UiSlider::step)
        .field("fill_color", &UiSlider::fillColor, {.color = true, .group = "Appearance"})
        .field("handle_color", &UiSlider::handleColor, {.color = true})
        .field("handle_size", &UiSlider::handleSize)
        .field("key_step", &UiSlider::keyStep, {.group = "Interaction"})
        .field("interactable", &UiSlider::interactable)
        .field("action", &UiSlider::action);
}

DEVEX_REFLECT(UiToggle)
{
    type.field("value", &UiToggle::value, {.group = "Toggle"})
        .field("check_color", &UiToggle::checkColor, {.color = true, .group = "Appearance"})
        .field("mark_color", &UiToggle::markColor, {.color = true})
        .field("check_size", &UiToggle::checkSize)
        .field("interactable", &UiToggle::interactable, {.group = "Interaction"})
        .field("action", &UiToggle::action);
}

DEVEX_REFLECT(UiLayout)
{
    type.field("kind", &UiLayout::kind, {.group = "Layout"})
        .field("spacing", &UiLayout::spacing)
        .field("padding", &UiLayout::padding)
        .field("columns", &UiLayout::columns)
        .field("equal_size", &UiLayout::equalSize)
        .field("align", &UiLayout::align);
}

} // namespace devex::scene
