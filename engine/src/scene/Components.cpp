#include <devex/scene/Components.hpp>

namespace devex::scene {

DEVEX_REFLECT(Transform)
{
    type.field("position", &Transform::position)
        .field("rotation", &Transform::rotation)
        .field("scale", &Transform::scale);
}

DEVEX_REFLECT(MeshRenderer)
{
    type.field("mesh", &MeshRenderer::mesh);
}

DEVEX_REFLECT(Camera)
{
    type.field("vertical_fov", &Camera::verticalFov)
        .field("near_plane", &Camera::nearPlane)
        .field("primary", &Camera::primary);
}

DEVEX_REFLECT(DirectionalLight)
{
    type.field("ambient", &DirectionalLight::ambient);
}

} // namespace devex::scene
