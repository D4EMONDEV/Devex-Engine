#include <devex/scene/CsgComponents.hpp>

namespace devex::scene {

namespace {

// What the shapes have in common, in the same order after their own fields.
template <typename T>
void reflectSolid(reflection::TypeBuilder<T>& type)
{
    type.field("operation", &T::operation, {.group = "Solid"}).field("material", &T::material, {.assetType = "material"}).field("collision", &T::collision);
}

} // namespace

DEVEX_REFLECT(CsgBox)
{
    type.field("size", &CsgBox::size, {.group = "Shape"});
    reflectSolid(type);
}

DEVEX_REFLECT(CsgSphere)
{
    type.field("radius", &CsgSphere::radius, {.group = "Shape"}).field("sides", &CsgSphere::sides);
    reflectSolid(type);
}

DEVEX_REFLECT(CsgCylinder)
{
    type.field("radius", &CsgCylinder::radius, {.group = "Shape"}).field("height", &CsgCylinder::height).field("sides", &CsgCylinder::sides);
    reflectSolid(type);
}

DEVEX_REFLECT(CsgPath)
{
    type.field("path", &CsgPath::path, {.group = "Shape"}).field("width", &CsgPath::width).field("depth", &CsgPath::depth);
    reflectSolid(type);
}

DEVEX_REFLECT(CsgCombiner)
{
    type.field("operation", &CsgCombiner::operation, {.group = "Solid"}).field("collision", &CsgCombiner::collision);
}

} // namespace devex::scene
