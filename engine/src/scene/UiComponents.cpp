#include <devex/scene/UiComponents.hpp>

namespace devex::scene {

DEVEX_REFLECT(Canvas)
{
    type.field("scale_mode", &Canvas::scaleMode)
        .field("reference_resolution", &Canvas::referenceResolution)
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
        .field("visible", &UiRect::visible);
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
        .field("raycast_target", &UiText::raycastTarget);
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
