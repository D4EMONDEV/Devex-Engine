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

// What a field being edited shows over its text. The offsets are bytes of the text as it is
// drawn, which is not the text of the field when it hides what is typed.
struct EditState
{
    std::size_t caret = 0;
    std::size_t selectionMin = 0;
    std::size_t selectionMax = 0;
    // Off half of the time, so that the cursor blinks.
    bool caretVisible = true;
};

// What the drawing needs from outside the scene: the assets, the tint a button gives its image
// while the pointer is on it, and the state of the field being edited.
struct DrawContext
{
    std::function<FontRef(asset::AssetId)> fonts;
    std::function<render::TextureHandle(asset::AssetId)> textures;
    // The size of a texture in pixels, for the images whose borders stay unstretched.
    std::function<math::Vec2(asset::AssetId)> textureSize;
    std::function<math::Vec4(scene::Entity)> tint;
    // Nothing for a field that is not being edited: it then shows its placeholder rather than a
    // cursor.
    std::function<const EditState*(scene::Entity)> editing;
    // The font used by a UiText that names none.
    asset::AssetId defaultFont;
};

// Appends the elements of one canvas to the interface of the frame, in the order they were laid
// out: parents first, then their children over them.
void buildDrawList(const scene::Scene& scene, const LayoutResult& layout,
                   const DrawContext& context, render::RenderWorld& world);

} // namespace devex::ui
