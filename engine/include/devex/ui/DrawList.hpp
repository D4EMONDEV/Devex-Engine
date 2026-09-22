#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/asset/FontData.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/ui/Layout.hpp>

#include <functional>

namespace devex::scene {
class Scene;
}

// Turns the placed elements of an interface into the triangles the renderer draws.
namespace devex::ui {

// A font ready to draw with: its letters and the atlas they are read from.
struct FontRef
{
    const asset::FontData* data = nullptr;
    render::TextureHandle atlas;
};

// What the drawing needs from outside the scene: the assets, and the tint a button gives its image
// while the pointer is on it.
struct DrawContext
{
    std::function<FontRef(asset::AssetId)> fonts;
    std::function<render::TextureHandle(asset::AssetId)> textures;
    std::function<math::Vec4(scene::Entity)> tint;
    // The font used by a UiText that names none.
    asset::AssetId defaultFont;
};

// Appends the elements of one canvas to the interface of the frame, in the order they were laid
// out: parents first, then their children over them.
void buildDrawList(const scene::Scene& scene, const LayoutResult& layout,
                   const DrawContext& context, render::RenderWorld& world);

} // namespace devex::ui
