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
        .field("speed", &UiScroll::speed);
}

DEVEX_REFLECT(UiImage)
{
    type.field("texture", &UiImage::texture, {.assetType = "texture"})
        .field("color", &UiImage::color, {.color = true})
        .field("border", &UiImage::border)
        .field("corner_radius", &UiImage::cornerRadius)
        .field("raycast_target", &UiImage::raycastTarget);
}

DEVEX_REFLECT(UiText)
{
    type.field("text", &UiText::text)
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
