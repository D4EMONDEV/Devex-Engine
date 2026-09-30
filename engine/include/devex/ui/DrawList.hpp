#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/asset/FontData.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/ui/Layout.hpp>

#include <functional>
#include <optional>

namespace devex::scene {
class Scene;
}

// Turns the placed elements of an interface into the triangles the renderer draws.
namespace devex::ui {

class TextCache;
struct TextAreaView;

// A font ready to draw with: its letters and the atlas they are read from.
struct DEVEX_API FontRef
{
    const asset::FontData* data = nullptr;
    render::TextureHandle atlas;
};

// A sprite an image shows: the part of its texture it covers.
struct DEVEX_API SpriteImage
{
    render::TextureHandle texture;
    // The texture coordinates of its top-left corner, then of its bottom-right corner.
    math::Vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};
    // Its size in pixels.
    math::Vec2 size{0.0f};
};

// What a field being edited shows over its text. The offsets are bytes of the text as it is
// drawn, which is not the text of the field when it hides what is typed.
struct DEVEX_API EditState
{
    std::size_t caret = 0;
    std::size_t selectionMin = 0;
    std::size_t selectionMax = 0;
    // Off half of the time, so that the cursor blinks.
    bool caretVisible = true;
};

// What the drawing needs from outside the scene: the assets, the tint a button gives its image
// while the pointer is on it, and the state of the field being edited.
struct DEVEX_API DrawContext
{
    std::function<FontRef(asset::AssetId)> fonts;
    std::function<render::TextureHandle(asset::AssetId)> textures;
    // The sprites an image may show in place of a texture; nothing for an asset that is not one,
    // which is then looked for among the textures.
    std::function<std::optional<SpriteImage>(asset::AssetId)> sprites;
    // The size of a texture in pixels, for the images whose borders stay unstretched.
    std::function<math::Vec2(asset::AssetId)> textureSize;
    std::function<math::Vec4(scene::Entity)> tint;
    // Nothing for a field that is not being edited: it then shows its placeholder rather than a
    // cursor.
    std::function<const EditState*(scene::Entity)> editing;
    // Whether the pointer holds or rests on the bar of a splitter, which then lights up.
    std::function<bool(scene::Entity)> grabbed;
    // The hue, saturation and value a colour picker shows, which its colour gives when this says
    // nothing: a grey loses its hue.
    std::function<bool(scene::Entity, math::Vec3&)> pickerHsv;
    // What an area of text shows beside its letters: its lines, its cursor, its selection and the
    // colours a tool gave it. Without it an area draws its text alone.
    std::function<const TextAreaView*(scene::Entity)> textAreas;
    // The font used by a UiText that names none.
    asset::AssetId defaultFont;
    // Where the texts keep their letters from a frame to the next; without it, the letters of every
    // text are placed again at every drawing.
    TextCache* textCache = nullptr;
};

// Appends the elements of one canvas to the interface of the frame, in the order they were laid
// out: parents first, then their children over them.
DEVEX_API void buildDrawList(const scene::Scene& scene, const LayoutResult& layout,
                             const DrawContext& context, render::RenderWorld& world);

// A line of text drawn over the canvases, in pixels: the list of a dropdown and the tooltips.
struct DEVEX_API OverlayText
{
    std::string_view text;
    // The default font of the context when invalid.
    asset::AssetId font;
    float size = 16.0f;
    math::Vec4 color{1.0f};
    // Lines longer than this go on to the next one, between words; 0 keeps them whole.
    float wrapWidth = 0.0f;
};

// A rectangle of one color over the canvases, in pixels, its corners rounded by `radius`.
DEVEX_API void drawOverlayBox(render::RenderWorld& world, math::Vec2 min, math::Vec2 max, math::Vec4 color,
                              float radius = 0.0f);
// Text in a box, in pixels: from its left edge, centred on its height, and cut to it.
DEVEX_API void drawOverlayText(render::RenderWorld& world, const DrawContext& context, math::Vec2 min, math::Vec2 max,
                               const OverlayText& text);
[[nodiscard]] DEVEX_API math::Vec2 measureOverlayText(const DrawContext& context, const OverlayText& text);

// How much interface the frame held at a moment, to move what is added after it.
struct DEVEX_API DrawListMark
{
    std::size_t vertices = 0;
    std::size_t draws = 0;
};
[[nodiscard]] DEVEX_API DrawListMark markDrawList(const render::RenderWorld& world) noexcept;

// Scales what was added since the mark around the top left corner of the image, then moves it by
// an offset in pixels: the editor draws the interfaces of a scene inside the frame of what its
// game shows, as the game would draw them on the whole image.
DEVEX_API void placeDrawList(render::RenderWorld& world, DrawListMark mark, math::Vec2 offset, float scale) noexcept;

} // namespace devex::ui
