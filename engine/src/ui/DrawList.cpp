#include <devex/ui/DrawList.hpp>

#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace devex::ui {
namespace {

// Everything one canvas needs while it is turned into triangles.
struct Builder
{
    render::RenderWorld& world;
    float scale = 1.0f;

    // What the elements being drawn are cut to, in pixels.
    math::Vec4 clip{0.0f};

    // The draw the next triangles join, when they share its texture, its shape and its cut.
    [[nodiscard]] render::UiDraw& batch(render::UiDrawKind kind, render::TextureHandle texture,
                                        math::Vec4 rect, float radius, float sharpness)
    {
        const bool mergeable = kind != render::UiDrawKind::RoundedQuad;
        if (!world.uiDraws.empty())
        {
            render::UiDraw& last = world.uiDraws.back();
            if (mergeable && last.kind == kind && last.texture == texture &&
                last.sharpness == sharpness && last.clip == clip)
            {
                return last;
            }
        }
        world.uiDraws.push_back({.kind = kind,
                                 .texture = texture,
                                 .firstIndex = static_cast<std::uint32_t>(world.uiIndices.size()),
                                 .rect = rect,
                                 .radius = radius,
                                 .sharpness = sharpness,
                                 .clip = clip});
        return world.uiDraws.back();
    }

    // Two triangles, in pixels of the image.
    void quad(render::UiDraw& draw, math::Vec2 min, math::Vec2 max, math::Vec2 uvMin,
              math::Vec2 uvMax, math::Vec4 color)
    {
        const auto first = static_cast<std::uint32_t>(world.uiVertices.size());
        world.uiVertices.push_back({.position = min * scale, .uv = uvMin, .color = color});
        world.uiVertices.push_back(
            {.position = math::Vec2{max.x, min.y} * scale,
             .uv = math::Vec2{uvMax.x, uvMin.y},
             .color = color});
        world.uiVertices.push_back({.position = max * scale, .uv = uvMax, .color = color});
        world.uiVertices.push_back(
            {.position = math::Vec2{min.x, max.y} * scale,
             .uv = math::Vec2{uvMin.x, uvMax.y},
             .color = color});
        for (const std::uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
        {
            world.uiIndices.push_back(first + corner);
        }
        draw.indexCount += 6;
    }

    // The same, with the four corners given: a turned or scaled element keeps its shape.
    void quad(render::UiDraw& draw, const std::array<math::Vec2, 4>& points, math::Vec2 uvMin,
              math::Vec2 uvMax, math::Vec4 color)
    {
        const auto first = static_cast<std::uint32_t>(world.uiVertices.size());
        const std::array<math::Vec2, 4> uvs{uvMin, math::Vec2{uvMax.x, uvMin.y}, uvMax,
                                            math::Vec2{uvMin.x, uvMax.y}};
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            world.uiVertices.push_back(
                {.position = points[index] * scale, .uv = uvs[index], .color = color});
        }
        for (const std::uint32_t corner : {0u, 1u, 2u, 0u, 2u, 3u})
        {
            world.uiIndices.push_back(first + corner);
        }
        draw.indexCount += 6;
    }
};

// The corners of a placed element, or its plain rectangle when it is neither turned nor scaled.
[[nodiscard]] bool isPlain(const LaidOutRect& rect) noexcept
{
    return rect.rotation == 0.0f && rect.scale.x == 1.0f && rect.scale.y == 1.0f;
}

[[nodiscard]] math::Vec4 withOpacity(math::Vec4 color, float opacity) noexcept
{
    return math::Vec4{color.x, color.y, color.z, color.w * opacity};
}

[[nodiscard]] math::Vec4 multiply(math::Vec4 color, math::Vec4 tint) noexcept
{
    return math::Vec4{color.x * tint.x, color.y * tint.y, color.z * tint.z, color.w * tint.w};
}

// Draws an image whose borders stay unstretched: the four corners keep their size, the four sides
// stretch one way and the middle stretches both. The borders are fractions of the texture, so a
// border of 0.25 of an image of 64 pixels draws corners of 16 units, whatever the rectangle.
void nineSlice(Builder& builder, render::UiDraw& draw, const LaidOutRect& rect,
               const scene::UiImage& image, math::Vec4 color, math::Vec2 texture)
{
    // The size the borders cover of the image, never past the middle of the rectangle. Without
    // the size of the image the borders fall back on the rectangle, which still draws.
    const math::Vec2 source{texture.x > 0.0f ? texture.x : rect.size().x,
                            texture.y > 0.0f ? texture.y : rect.size().y};
    const float left = std::min(image.border.x * source.x, rect.size().x * 0.5f);
    const float top = std::min(image.border.y * source.y, rect.size().y * 0.5f);
    const float right = std::min(image.border.z * source.x, rect.size().x * 0.5f);
    const float bottom = std::min(image.border.w * source.y, rect.size().y * 0.5f);
    const std::array<float, 4> columns{rect.min.x, rect.min.x + left, rect.max.x - right,
                                       rect.max.x};
    const std::array<float, 4> rows{rect.min.y, rect.min.y + top, rect.max.y - bottom, rect.max.y};
    const std::array<float, 4> columnsUv{0.0f, image.border.x, 1.0f - image.border.z, 1.0f};
    const std::array<float, 4> rowsUv{0.0f, image.border.y, 1.0f - image.border.w, 1.0f};
    for (std::size_t row = 0; row < 3; ++row)
    {
        for (std::size_t column = 0; column < 3; ++column)
        {
            builder.quad(draw, math::Vec2{columns[column], rows[row]},
                         math::Vec2{columns[column + 1], rows[row + 1]},
                         math::Vec2{columnsUv[column], rowsUv[row]},
                         math::Vec2{columnsUv[column + 1], rowsUv[row + 1]}, color);
        }
    }
}

void drawImage(Builder& builder, const DrawContext& context, const LaidOutRect& rect,
               const scene::UiImage& image)
{
    const render::TextureHandle texture =
        image.texture.isValid() && context.textures ? context.textures(image.texture)
                                                    : render::TextureHandle{};
    math::Vec4 color = withOpacity(image.color, rect.opacity);
    if (context.tint)
    {
        color = multiply(color, context.tint(rect.entity));
    }
    if (color.w <= 0.0f)
    {
        return;
    }

    const bool rounded = image.cornerRadius > 0.0f && isPlain(rect);
    const math::Vec4 shape{rect.min.x * builder.scale, rect.min.y * builder.scale,
                           rect.max.x * builder.scale, rect.max.y * builder.scale};
    render::UiDraw& draw =
        builder.batch(rounded ? render::UiDrawKind::RoundedQuad : render::UiDrawKind::Quad, texture,
                      shape, image.cornerRadius * builder.scale, 1.0f);

    const bool sliced = texture.isValid() && (image.border.x > 0.0f || image.border.y > 0.0f ||
                                              image.border.z > 0.0f || image.border.w > 0.0f);
    if (sliced && isPlain(rect))
    {
        const math::Vec2 size =
            context.textureSize ? context.textureSize(image.texture) : math::Vec2{0.0f};
        nineSlice(builder, draw, rect, image, color, size);
        return;
    }
    if (isPlain(rect))
    {
        builder.quad(draw, rect.min, rect.max, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f},
                     color);
    }
    else
    {
        builder.quad(draw, corners(rect), math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f}, color);
    }
}

// The letters and the images of a text that was already laid out, which a field draws too.
void drawTextLayout(Builder& builder, const DrawContext& context, const LaidOutRect& rect,
                    const scene::UiText& text, const TextLayoutResult& letters, const FontRef& font,
                    math::Vec4 color);

void drawText(Builder& builder, const DrawContext& context, const LaidOutRect& rect,
              const scene::UiText& text)
{
    if (!context.fonts || text.text.empty())
    {
        return;
    }
    const FontRef font =
        context.fonts(text.font.isValid() ? text.font : context.defaultFont);
    if (font.data == nullptr || !font.atlas.isValid())
    {
        return;
    }

    TextLayoutResult letters;
    layoutText(*font.data, text.text,
               TextStyle{.size = text.size,
                         .align = text.align,
                         .verticalAlign = text.verticalAlign,
                         .wrap = text.wrap,
                         .lineSpacing = text.lineSpacing,
                         .rich = text.rich},
               rect.min, rect.max, letters);
    drawTextLayout(builder, context, rect, text, letters, font, text.color);
}

// The letters and the images of a text that was already laid out, which a field draws too.
void drawTextLayout(Builder& builder, const DrawContext& context, const LaidOutRect& rect,
                    const scene::UiText& text, const TextLayoutResult& letters, const FontRef& font,
                    math::Vec4 color)
{
    // The images a rich text asked for, each in its own batch since they carry their own texture.
    for (const InlineImage& image : letters.images)
    {
        if (image.index >= text.icons.size() || !context.textures)
        {
            continue;
        }
        const render::TextureHandle texture = context.textures(text.icons[image.index]);
        if (!texture.isValid())
        {
            continue;
        }
        render::UiDraw& draw =
            builder.batch(render::UiDrawKind::Quad, texture, math::Vec4{0.0f}, 0.0f, 1.0f);
        builder.quad(draw, image.min, image.max, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f},
                     withOpacity(math::Vec4{1.0f, 1.0f, 1.0f, 1.0f}, rect.opacity));
    }
    if (letters.glyphs.empty())
    {
        return;
    }

    const float sharpness = textSharpness(*font.data, text.size) * builder.scale;
    const auto drawGlyphs = [&](math::Vec4 color, math::Vec2 shift, bool plain) {
        render::UiDraw& draw =
            builder.batch(render::UiDrawKind::Text, font.atlas, math::Vec4{0.0f}, 0.0f, sharpness);
        for (const GlyphQuad& glyph : letters.glyphs)
        {
            const math::Vec4 tint = plain ? color : multiply(color, glyph.color);
            // A bold face the font does not carry is drawn twice, a hair apart.
            const float lean = glyph.italic ? (glyph.max.y - glyph.min.y) * 0.21f : 0.0f;
            const int passes = glyph.bold && plain == false ? 2 : 1;
            for (int pass = 0; pass < passes; ++pass)
            {
                const math::Vec2 step = shift + math::Vec2{pass * text.size * 0.04f, 0.0f};
                if (lean > 0.0f)
                {
                    const std::array<math::Vec2, 4> corners{
                        math::Vec2{glyph.min.x + lean, glyph.min.y} + step,
                        math::Vec2{glyph.max.x + lean, glyph.min.y} + step,
                        math::Vec2{glyph.max.x, glyph.max.y} + step,
                        math::Vec2{glyph.min.x, glyph.max.y} + step};
                    builder.quad(draw, corners, glyph.uvMin, glyph.uvMax, tint);
                }
                else
                {
                    builder.quad(draw, glyph.min + step, glyph.max + step, glyph.uvMin, glyph.uvMax,
                                 tint);
                }
            }
        }
    };

    if (text.outlineWidth > 0.0f)
    {
        // The outline is the same letters, drawn around the text before it.
        const math::Vec4 outline = withOpacity(text.outlineColor, rect.opacity);
        const float width = text.outlineWidth;
        for (const math::Vec2 shift : {math::Vec2{-width, 0.0f}, math::Vec2{width, 0.0f},
                                       math::Vec2{0.0f, -width}, math::Vec2{0.0f, width}})
        {
            drawGlyphs(outline, shift, true);
        }
    }
    drawGlyphs(withOpacity(color, rect.opacity), math::Vec2{0.0f, 0.0f}, false);
}

// A plain rectangle of one colour, for what a field draws around its letters.
void fill(Builder& builder, math::Vec2 min, math::Vec2 max, math::Vec4 color)
{
    if (color.w <= 0.0f || max.x <= min.x || max.y <= min.y)
    {
        return;
    }
    render::UiDraw& draw = builder.batch(render::UiDrawKind::Quad, render::TextureHandle{},
                                         math::Vec4{0.0f}, 0.0f, 1.0f);
    builder.quad(draw, min, max, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f}, color);
}

// What is selected, one rectangle per line it covers. The stops follow the text, so a line is the
// run of stops that share it.
void drawSelection(Builder& builder, const TextLayoutResult& letters, math::Vec4 color,
                   const EditState& edit)
{
    std::size_t index = 0;
    while (index < letters.stops.size())
    {
        const CaretStop& first = letters.stops[index];
        if (first.offset < edit.selectionMin || first.offset >= edit.selectionMax)
        {
            ++index;
            continue;
        }
        const CaretStop* last = &first;
        std::size_t next = index + 1;
        while (next < letters.stops.size() && letters.stops[next].line == first.line &&
               letters.stops[next].offset <= edit.selectionMax)
        {
            last = &letters.stops[next];
            ++next;
        }
        fill(builder, first.position,
             math::Vec2{last->position.x, last->position.y + last->height}, color);
        index = next;
    }
}

// The filled part of a slider and its handle, over whatever its image drew.
void drawSlider(Builder& builder, const LaidOutRect& rect, const scene::UiSlider& slider)
{
    const float low = std::min(slider.minValue, slider.maxValue);
    const float high = std::max(slider.minValue, slider.maxValue);
    const float amount =
        high > low ? std::clamp((slider.value - low) / (high - low), 0.0f, 1.0f) : 0.0f;
    const float handle = std::max(slider.handleSize, 0.0f) * rect.size().y;
    // The handle stays inside the track, so its middle runs over what is left of it.
    const float middle =
        rect.min.x + handle * 0.5f + amount * std::max(rect.size().x - handle, 0.0f);
    fill(builder, rect.min, math::Vec2{middle, rect.max.y},
         withOpacity(slider.fillColor, rect.opacity));
    if (handle > 0.0f)
    {
        fill(builder, math::Vec2{middle - handle * 0.5f, rect.min.y},
             math::Vec2{middle + handle * 0.5f, rect.max.y},
             withOpacity(slider.handleColor, rect.opacity));
    }
}

// The mark of a toggle that is on, inside its box.
void drawToggle(Builder& builder, const LaidOutRect& rect, const scene::UiToggle& toggle)
{
    if (!toggle.value)
    {
        return;
    }
    const float fraction = std::clamp(toggle.checkSize, 0.0f, 1.0f);
    const math::Vec2 inset{rect.size().x * (1.0f - fraction) * 0.5f,
                           rect.size().y * (1.0f - fraction) * 0.5f};
    fill(builder, math::Vec2{rect.min.x + inset.x, rect.min.y + inset.y},
         math::Vec2{rect.max.x - inset.x, rect.max.y - inset.y},
         withOpacity(toggle.checkColor, rect.opacity));
}

// A field: its text or one dot per letter, its placeholder while it is empty and untouched, and
// the cursor and the selection while it is being edited.
void drawField(Builder& builder, const DrawContext& context, const LaidOutRect& rect,
               const scene::UiText& text, const scene::UiInput& field)
{
    if (!context.fonts)
    {
        return;
    }
    const FontRef font = context.fonts(text.font.isValid() ? text.font : context.defaultFont);
    if (font.data == nullptr || !font.atlas.isValid())
    {
        return;
    }
    const EditState* const edit = context.editing ? context.editing(rect.entity) : nullptr;
    const TextStyle style = fieldStyle(text, field);
    math::Vec2 boxMin{0.0f};
    math::Vec2 boxMax{0.0f};
    fieldBox(field, rect.min, rect.max, boxMin, boxMax);
    TextLayoutResult letters;

    if (text.text.empty())
    {
        // Nothing typed yet: the placeholder says what is expected, and the cursor still stands
        // at the start while the field is being edited.
        if (!field.placeholder.empty())
        {
            layoutText(*font.data, field.placeholder, style, boxMin, boxMax, letters);
            drawTextLayout(builder, context, rect, text, letters, font, field.placeholderColor);
        }
        if (edit != nullptr && edit->caretVisible)
        {
            TextLayoutResult empty;
            layoutText(*font.data, std::string_view{}, style, boxMin, boxMax, empty);
            if (const CaretStop* const stop = caretAt(empty, 0))
            {
                fill(builder, stop->position,
                     math::Vec2{stop->position.x + std::max(text.size * 0.06f, 1.0f),
                                stop->position.y + stop->height},
                     withOpacity(field.caretColor, rect.opacity));
            }
        }
        return;
    }

    const std::string shown = shownText(text.text, field.password);
    layoutText(*font.data, shown, style, boxMin, boxMax, letters);
    if (edit != nullptr && edit->selectionMax > edit->selectionMin)
    {
        drawSelection(builder, letters, withOpacity(field.selectionColor, rect.opacity), *edit);
    }
    drawTextLayout(builder, context, rect, text, letters, font, text.color);
    if (edit != nullptr && edit->caretVisible)
    {
        if (const CaretStop* const stop = caretAt(letters, edit->caret))
        {
            fill(builder, stop->position,
                 math::Vec2{stop->position.x + std::max(text.size * 0.06f, 1.0f),
                            stop->position.y + stop->height},
                 withOpacity(field.caretColor, rect.opacity));
        }
    }
}

} // namespace

void buildDrawList(const scene::Scene& scene, const LayoutResult& layout,
                   const DrawContext& context, render::RenderWorld& world)
{
    Builder builder{.world = world, .scale = layout.scale};
    for (const LaidOutRect& rect : layout.rects)
    {
        if (!rect.visible || rect.opacity <= 0.0f || rect.size().x <= 0.0f ||
            rect.size().y <= 0.0f)
        {
            continue;
        }
        // Everything the element draws is cut the same way, in pixels of the image.
        builder.clip = isClipped(rect.clip) ? rect.clip * layout.scale : math::Vec4{0.0f};
        // An element entirely outside what cuts it is not drawn at all.
        if (isClipped(rect.clip) &&
            (rect.max.x <= rect.clip.x || rect.min.x >= rect.clip.z || rect.max.y <= rect.clip.y ||
             rect.min.y >= rect.clip.w))
        {
            continue;
        }
        if (const scene::UiImage* const image = scene.tryGet<scene::UiImage>(rect.entity))
        {
            drawImage(builder, context, rect, *image);
        }
        if (const scene::UiSlider* const slider = scene.tryGet<scene::UiSlider>(rect.entity))
        {
            drawSlider(builder, rect, *slider);
        }
        if (const scene::UiToggle* const toggle = scene.tryGet<scene::UiToggle>(rect.entity))
        {
            drawToggle(builder, rect, *toggle);
        }
        if (const scene::UiText* const text = scene.tryGet<scene::UiText>(rect.entity))
        {
            if (const scene::UiInput* const field = scene.tryGet<scene::UiInput>(rect.entity))
            {
                drawField(builder, context, rect, *text, *field);
            }
            else
            {
                drawText(builder, context, rect, *text);
            }
        }
    }
    // Batches that ended up empty would draw nothing; they are dropped so the renderer skips them.
    std::erase_if(world.uiDraws, [](const render::UiDraw& draw) { return draw.indexCount == 0; });
}

} // namespace devex::ui
