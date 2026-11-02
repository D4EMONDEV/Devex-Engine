#include "ViewportSettings.hpp"

#include "Gizmo.hpp"

#include <devex/ui/Color.hpp>

#include <algorithm>
#include <cmath>
#include <string>

namespace devex::tools::detail
{

ViewportSettings readViewportSettings(const serialization::TextSection& section)
{
    ViewportSettings settings;
    for (const auto& field : viewportNumbers)
    {
        if (const auto* value = section.findAttribute(field.key))
        {
            if (const auto number = serialization::asNumber(*value);
                number && std::isfinite(*number))
            {
                settings.*field.member =
                    static_cast<float>(std::clamp(*number, static_cast<double>(field.minimum),
                                                  static_cast<double>(field.maximum)));
            }
        }
    }
    for (const auto& field : viewportColors)
    {
        const auto* value = section.findAttribute(field.key);
        const auto* text = value != nullptr ? serialization::asString(*value) : nullptr;
        if (const auto color = text != nullptr ? ui::colorFromHex(*text) : std::nullopt)
        {
            settings.*field.member = math::Vec3(*color);
        }
    }
    for (const auto& field : viewportToggles)
    {
        if (const auto* value = section.findAttribute(field.key))
        {
            settings.*field.member = serialization::asBool(*value).value_or(settings.*field.member);
        }
    }
    return settings;
}

serialization::TextSection writeViewportSettings(const ViewportSettings& settings)
{
    using serialization::TextValue;
    serialization::TextSection section;
    section.type = "viewport";
    for (const auto& field : viewportNumbers)
    {
        section.attributes.emplace_back(std::string(field.key),
                                        TextValue(static_cast<double>(settings.*field.member)));
    }
    for (const auto& field : viewportColors)
    {
        section.attributes.emplace_back(
            std::string(field.key),
            TextValue("#" + ui::hexFromColor(math::Vec4(settings.*field.member, 1.0f), false)));
    }
    for (const auto& field : viewportToggles)
    {
        section.attributes.emplace_back(std::string(field.key), TextValue(settings.*field.member));
    }
    return section;
}

void applyGizmoSettings(Gizmo& gizmo, const ViewportSettings& settings)
{
    gizmo.sizePixels = settings.gizmoSize;
    gizmo.thicknessPixels = settings.gizmoThickness;
    gizmo.axisColors = {ui::linearFromSrgb(math::Vec4(settings.axisX, 1.0f)),
                        ui::linearFromSrgb(math::Vec4(settings.axisY, 1.0f)),
                        ui::linearFromSrgb(math::Vec4(settings.axisZ, 1.0f))};
    gizmo.highlightColor = ui::linearFromSrgb(math::Vec4(settings.highlight, 1.0f));
}

} // namespace devex::tools::detail
