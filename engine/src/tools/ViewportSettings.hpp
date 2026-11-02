#pragma once

#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/serialization/Text.hpp>

#include <array>
#include <string_view>

namespace devex::tools::detail
{

class Gizmo;

// User preferences, independent of the opened scene. Sizes are viewport pixels, colours sRGB.
struct ViewportSettings
{
    float gizmoSize = 90.0f;
    float gizmoThickness = 3.0f;
    float lineWidth = 1.5f;
    float iconSize = 22.0f;
    float gridSpacing = 1.0f;
    float gridOpacity = 0.55f;
    math::Vec3 axisX{0.941f, 0.353f, 0.353f};
    math::Vec3 axisY{0.486f, 0.812f, 0.282f};
    math::Vec3 axisZ{0.310f, 0.561f, 0.941f};
    math::Vec3 highlight{1.0f, 0.85f, 0.0f};
    math::Vec3 gridColor{0.537f};
    math::Vec3 gridMajorColor{0.626f};
    bool showGrid = true;
    bool showIcons = true;
    bool showColliders = false;
    bool showCameras = true;
    bool showLights = true;
    bool showAudio = true;
    bool showPhysics = true;
    bool showNavigation = true;

    bool operator==(const ViewportSettings&) const = default;
};

struct ViewportNumber
{
    std::string_view key;
    std::string_view label;
    float ViewportSettings::* member;
    float minimum;
    float maximum;
};
inline constexpr std::array viewportNumbers{
    ViewportNumber{"gizmo_size", "Handle Size", &ViewportSettings::gizmoSize, 40.0f, 240.0f},
    ViewportNumber{"gizmo_thickness", "Handle Thickness", &ViewportSettings::gizmoThickness, 0.5f,
                   6.0f},
    ViewportNumber{"line_width", "Line Width", &ViewportSettings::lineWidth, 0.5f, 6.0f},
    ViewportNumber{"icon_size", "Icon Size", &ViewportSettings::iconSize, 10.0f, 64.0f},
    ViewportNumber{"grid_spacing", "Grid Unit", &ViewportSettings::gridSpacing, 0.01f, 100.0f},
    ViewportNumber{"grid_opacity", "Grid Opacity", &ViewportSettings::gridOpacity, 0.0f, 1.0f},
};

struct ViewportColor
{
    std::string_view key;
    std::string_view label;
    math::Vec3 ViewportSettings::* member;
};
inline constexpr std::array viewportColors{
    ViewportColor{"axis_x", "X Axis", &ViewportSettings::axisX},
    ViewportColor{"axis_y", "Y Axis", &ViewportSettings::axisY},
    ViewportColor{"axis_z", "Z Axis", &ViewportSettings::axisZ},
    ViewportColor{"highlight", "Hovered Handle", &ViewportSettings::highlight},
    ViewportColor{"grid_color", "Grid Lines", &ViewportSettings::gridColor},
    ViewportColor{"grid_major_color", "Major Grid Lines", &ViewportSettings::gridMajorColor},
};

struct ViewportToggle
{
    std::string_view key;
    std::string_view label;
    bool ViewportSettings::* member;
};
inline constexpr std::array viewportToggles{
    ViewportToggle{"show_grid", "Grid", &ViewportSettings::showGrid},
    ViewportToggle{"show_icons", "Scene Icons", &ViewportSettings::showIcons},
    ViewportToggle{"show_colliders", "All Collision Shapes", &ViewportSettings::showColliders},
    ViewportToggle{"show_cameras", "Cameras", &ViewportSettings::showCameras},
    ViewportToggle{"show_lights", "Lights", &ViewportSettings::showLights},
    ViewportToggle{"show_audio", "Audio", &ViewportSettings::showAudio},
    ViewportToggle{"show_physics", "Physics and Joints", &ViewportSettings::showPhysics},
    ViewportToggle{"show_navigation", "Navigation", &ViewportSettings::showNavigation},
};

[[nodiscard]] DEVEX_API ViewportSettings
readViewportSettings(const serialization::TextSection& section);
[[nodiscard]] DEVEX_API serialization::TextSection
writeViewportSettings(const ViewportSettings& settings);
DEVEX_API void applyGizmoSettings(Gizmo& gizmo, const ViewportSettings& settings);

} // namespace devex::tools::detail
