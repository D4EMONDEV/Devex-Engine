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
    // Whether the pointer holds or rests on the bar of a splitter, which then lights up.
    std::function<bool(scene::Entity)> grabbed;
    // The font used by a UiText that names none.
    asset::AssetId defaultFont;
};

// Appends the elements of one canvas to the interface of the frame, in the order they were laid
// out: parents first, then their children over them.
void buildDrawList(const scene::Scene& scene, const LayoutResult& layout,
                   const DrawContext& context, render::RenderWorld& world);

// A line of text drawn over the canvases, in pixels: the list of a dropdown and the tooltips.
struct OverlayText
{
    std::string_view text;
    // The default font of the context when invalid.
    asset::AssetId font;
    float size = 16.0f;
    math::Vec4 color{1.0f};
};

// A rectangle of one color over the canvases, in pixels, its corners rounded by `radius`.
void drawOverlayBox(render::RenderWorld& world, math::Vec2 min, math::Vec2 max, math::Vec4 color,
                    float radius = 0.0f);
// One line of text in a box, in pixels: from its left edge, centred on its height, and cut to it.
void drawOverlayText(render::RenderWorld& world, const DrawContext& context, math::Vec2 min, math::Vec2 max,
                     const OverlayText& text);
[[nodiscard]] math::Vec2 measureOverlayText(const DrawContext& context, const OverlayText& text);

// How much interface the frame held at a moment, to move what is added after it.
struct DrawListMark
{
    std::size_t vertices = 0;
    std::size_t draws = 0;
};
[[nodiscard]] DrawListMark markDrawList(const render::RenderWorld& world) noexcept;

// Scales what was added since the mark around the top left corner of the image, then moves it by
// an offset in pixels: the editor draws the interfaces of a scene inside the frame of what its
// game shows, as the game would draw them on the whole image.
void placeDrawList(render::RenderWorld& world, DrawListMark mark, math::Vec2 offset, float scale) noexcept;

} // namespace devex::ui
