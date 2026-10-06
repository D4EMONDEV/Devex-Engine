#include <devex/scene/UiComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(Canvas)
{
    type.field("scale_mode", &Canvas::scaleMode)
        .field("reference_resolution", &Canvas::referenceResolution)
        .field("theme", &Canvas::theme, {.assetType = "theme"})
        .field("match_width_or_height", &Canvas::matchWidthOrHeight)
        .field("sort_order", &Canvas::sortOrder)
        .field("visible", &Canvas::visible)
        .field("interactive", &Canvas::interactive);
}

DEVEX_REFLECT(UiRect)
{
    type.field("anchor_min", &UiRect::anchorMin)
        .field("anchor_max", &UiRect::anchorMax)
        .field("offset_min", &UiRect::offsetMin)
        .field("offset_max", &UiRect::offsetMax)
        .field("pivot", &UiRect::pivot)
        .field("scale", &UiRect::scale)
        .field("rotation", &UiRect::rotation, {.angle = true})
        .field("opacity", &UiRect::opacity)
        .field("visible", &UiRect::visible)
        .field("clip_children", &UiRect::clipChildren)
        .field("style", &UiRect::style);
}

DEVEX_REFLECT(UiScroll)
{
    type.field("offset", &UiScroll::offset)
        .field("horizontal", &UiScroll::horizontal)
        .field("vertical", &UiScroll::vertical)
        .field("speed", &UiScroll::speed)
        .field("scrollbar", &UiScroll::scrollbar)
        .field("scrollbar_size", &UiScroll::scrollbarSize)
        .field("scrollbar_color", &UiScroll::scrollbarColor, {.color = true});
}

DEVEX_REFLECT(UiPopup)
{
    type.field("kind", &UiPopup::kind).field("veil_color", &UiPopup::veilColor, {.color = true});
}

DEVEX_REFLECT(UiContextMenu)
{
    type.field("popup", &UiContextMenu::popup);
}

DEVEX_REFLECT(UiTooltip)
{
    type.field("text", &UiTooltip::text).field("delay", &UiTooltip::delay);
}

DEVEX_REFLECT(UiDropdown)
{
    type.field("options", &UiDropdown::options)
        .field("selected", &UiDropdown::selected)
        .field("list_color", &UiDropdown::listColor, {.color = true})
        .field("highlight_color", &UiDropdown::highlightColor, {.color = true})
        .field("arrow_color", &UiDropdown::arrowColor, {.color = true})
        .field("interactable", &UiDropdown::interactable)
        .field("action", &UiDropdown::action)
        .field("placeholder", &UiDropdown::placeholder);
}

DEVEX_REFLECT(UiPlot)
{
    type.field("values", &UiPlot::values)
        .field("min_value", &UiPlot::minValue)
        .field("max_value", &UiPlot::maxValue)
        .field("kind", &UiPlot::kind)
        .field("color", &UiPlot::color, {.color = true})
        .field("colors", &UiPlot::colors, {.color = true})
        .field("back_values", &UiPlot::backValues)
        .field("back_color", &UiPlot::backColor, {.color = true})
        .field("guides", &UiPlot::guides)
        .field("guide_color", &UiPlot::guideColor, {.color = true})
        .field("highlighted", &UiPlot::highlighted)
        .field("highlight_color", &UiPlot::highlightColor, {.color = true})
        .field("line_width", &UiPlot::lineWidth)
        .field("marker", &UiPlot::marker)
        .field("marker_color", &UiPlot::markerColor, {.color = true});
}

DEVEX_REFLECT(UiTextArea)
{
    type.field("selection_color", &UiTextArea::selectionColor, {.color = true})
        .field("caret_color", &UiTextArea::caretColor, {.color = true})
        .field("padding", &UiTextArea::padding)
        .field("scroll", &UiTextArea::scroll)
        .field("line_numbers", &UiTextArea::lineNumbers)
        .field("line_number_color", &UiTextArea::lineNumberColor, {.color = true})
        .field("current_line_color", &UiTextArea::currentLineColor, {.color = true})
        .field("tab_size", &UiTextArea::tabSize)
        .field("auto_indent", &UiTextArea::autoIndent)
        .field("indent_after", &UiTextArea::indentAfter)
        .field("read_only", &UiTextArea::readOnly)
        .field("scrollbar_size", &UiTextArea::scrollbarSize)
        .field("scrollbar_color", &UiTextArea::scrollbarColor, {.color = true})
        .field("interactable", &UiTextArea::interactable)
        .field("action", &UiTextArea::action);
}

DEVEX_REFLECT(UiLine)
{
    type.field("points", &UiLine::points)
        .field("width", &UiLine::width)
        .field("color", &UiLine::color, {.color = true})
        .field("arrow", &UiLine::arrow)
        .field("arrow_size", &UiLine::arrowSize)
        .field("closed", &UiLine::closed);
}

DEVEX_REFLECT(UiNumberField)
{
    type.field("value", &UiNumberField::value)
        .field("min_value", &UiNumberField::minValue)
        .field("max_value", &UiNumberField::maxValue)
        .field("step", &UiNumberField::step)
        .field("drag_speed", &UiNumberField::dragSpeed)
        .field("decimals", &UiNumberField::decimals)
        .field("format", &UiNumberField::format)
        .field("interactable", &UiNumberField::interactable)
        .field("action", &UiNumberField::action);
}

DEVEX_REFLECT(UiColorPicker)
{
    type.field("color", &UiColorPicker::color, {.color = true})
        .field("alpha", &UiColorPicker::alpha)
        .field("bar_size", &UiColorPicker::barSize)
        .field("spacing", &UiColorPicker::spacing)
        .field("interactable", &UiColorPicker::interactable)
        .field("action", &UiColorPicker::action);
}

DEVEX_REFLECT(UiSplitter)
{
    type.field("vertical", &UiSplitter::vertical)
        .field("position", &UiSplitter::position)
        .field("min_size", &UiSplitter::minSize)
        .field("bar_size", &UiSplitter::barSize)
        .field("bar_color", &UiSplitter::barColor, {.color = true})
        .field("hover_color", &UiSplitter::hoverColor, {.color = true});
}

DEVEX_REFLECT(UiFoldout)
{
    type.field("expanded", &UiFoldout::expanded)
        .field("content", &UiFoldout::content)
        .field("arrow_color", &UiFoldout::arrowColor, {.color = true})
        .field("interactable", &UiFoldout::interactable)
        .field("action", &UiFoldout::action);
}

DEVEX_REFLECT(UiVirtualList)
{
    type.field("item_count", &UiVirtualList::itemCount)
        .field("item_size", &UiVirtualList::itemSize)
        .field("first", &UiVirtualList::first, {.runtime = true});
}

DEVEX_REFLECT(UiTable)
{
    type.field("columns", &UiTable::columns)
        .field("resizable", &UiTable::resizable)
        .field("sort_column", &UiTable::sortColumn)
        .field("sort_ascending", &UiTable::sortAscending)
        .field("arrow_color", &UiTable::arrowColor, {.color = true})
        .field("action", &UiTable::action);
}

DEVEX_REFLECT(UiTableRow)
{
    type.field("header", &UiTableRow::header);
}

DEVEX_REFLECT(UiDragSource)
{
    type.field("type", &UiDragSource::type)
        .field("data", &UiDragSource::data)
        .field("label", &UiDragSource::label)
        .field("interactable", &UiDragSource::interactable);
}

DEVEX_REFLECT(UiDropTarget)
{
    type.field("accepts", &UiDropTarget::accepts)
        .field("highlight_color", &UiDropTarget::highlightColor, {.color = true})
        .field("action", &UiDropTarget::action);
}

DEVEX_REFLECT(UiImage)
{
    type.field("texture", &UiImage::texture, {.assetType = "texture"})
        .field("color", &UiImage::color, {.color = true})
        .field("border", &UiImage::border)
        .field("corner_radius", &UiImage::cornerRadius)
        .field("preserve_aspect", &UiImage::preserveAspect)
        .field("raycast_target", &UiImage::raycastTarget);
}

DEVEX_REFLECT(UiText)
{
    type.field("text", &UiText::text)
        .field("translate", &UiText::translate)
        .field("font", &UiText::font, {.assetType = "font"})
        .field("size", &UiText::size)
        .field("color", &UiText::color, {.color = true})
        .field("align", &UiText::align)
        .field("vertical_align", &UiText::verticalAlign)
        .field("wrap", &UiText::wrap)
        .field("line_spacing", &UiText::lineSpacing)
        .field("outline_color", &UiText::outlineColor, {.color = true})
        .field("outline_width", &UiText::outlineWidth)
        .field("raycast_target", &UiText::raycastTarget)
        .field("rich", &UiText::rich)
        .field("icons", &UiText::icons, {.assetType = "texture"});
}

DEVEX_REFLECT(UiInput)
{
    type.field("placeholder", &UiInput::placeholder)
        .field("placeholder_color", &UiInput::placeholderColor, {.color = true})
        .field("selection_color", &UiInput::selectionColor, {.color = true})
        .field("caret_color", &UiInput::caretColor, {.color = true})
        .field("padding", &UiInput::padding)
        .field("multiline", &UiInput::multiline)
        .field("password", &UiInput::password)
        .field("max_length", &UiInput::maxLength)
        .field("interactable", &UiInput::interactable)
        .field("action", &UiInput::action);
}

DEVEX_REFLECT(UiButton)
{
    type.field("action", &UiButton::action)
        .field("interactable", &UiButton::interactable)
        .field("hover_color", &UiButton::hoverColor, {.color = true})
        .field("pressed_color", &UiButton::pressedColor, {.color = true})
        .field("disabled_color", &UiButton::disabledColor, {.color = true})
        .field("fade_time", &UiButton::fadeTime);
}

DEVEX_REFLECT(UiBinding)
{
    type.field("component", &UiBinding::component)
        .field("field", &UiBinding::field)
        .field("format", &UiBinding::format)
        .field("decimals", &UiBinding::decimals)
        .field("source", &UiBinding::source);
}

DEVEX_REFLECT(UiSlider)
{
    type.field("value", &UiSlider::value)
        .field("min_value", &UiSlider::minValue)
        .field("max_value", &UiSlider::maxValue)
        .field("step", &UiSlider::step)
        .field("fill_color", &UiSlider::fillColor, {.color = true})
        .field("handle_color", &UiSlider::handleColor, {.color = true})
        .field("handle_size", &UiSlider::handleSize)
        .field("key_step", &UiSlider::keyStep)
        .field("interactable", &UiSlider::interactable)
        .field("action", &UiSlider::action);
}

DEVEX_REFLECT(UiToggle)
{
    type.field("value", &UiToggle::value)
        .field("check_color", &UiToggle::checkColor, {.color = true})
        .field("check_size", &UiToggle::checkSize)
        .field("interactable", &UiToggle::interactable)
        .field("action", &UiToggle::action);
}

DEVEX_REFLECT(UiLayout)
{
    type.field("kind", &UiLayout::kind)
        .field("spacing", &UiLayout::spacing)
        .field("padding", &UiLayout::padding)
        .field("columns", &UiLayout::columns)
        .field("equal_size", &UiLayout::equalSize)
        .field("align", &UiLayout::align);
}

} // namespace devex::scene
