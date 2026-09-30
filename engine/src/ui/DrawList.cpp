#include <devex/ui/DrawList.hpp>

#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/Color.hpp>
#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
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

    // A rectangle whose corners each have their colour, blended between them: the top left, the top
    // right, the bottom right and the bottom left.
    void gradient(render::UiDraw& draw, math::Vec2 min, math::Vec2 max, const std::array<math::Vec4, 4>& colors)
    {
        const auto first = static_cast<std::uint32_t>(world.uiVertices.size());
        const std::array<math::Vec2, 4> points{min, math::Vec2{max.x, min.y}, max, math::Vec2{min.x, max.y}};
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            world.uiVertices.push_back({.position = points[index] * scale, .uv = math::Vec2{0.0f}, .color = colors[index]});
        }
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

// A point of a placed element as its turn and its scale move it, around its pivot.
[[nodiscard]] math::Vec2 turned(const LaidOutRect& rect, math::Vec2 point) noexcept
{
    const float cosine = std::cos(rect.rotation);
    const float sine = std::sin(rect.rotation);
    const math::Vec2 local{(point.x - rect.pivot.x) * rect.scale.x, (point.y - rect.pivot.y) * rect.scale.y};
    return math::Vec2{rect.pivot.x + local.x * cosine - local.y * sine, rect.pivot.y + local.x * sine + local.y * cosine};
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
// stretch one way and the middle stretches both. The borders are fractions of the texture, or of
// the sprite, so a border of 0.25 of an image of 64 pixels draws corners of 16 units, whatever the
// rectangle. `uv` is the part of the texture the image covers.
void nineSlice(Builder& builder, render::UiDraw& draw, const LaidOutRect& rect,
               const scene::UiImage& image, math::Vec4 color, math::Vec2 texture, math::Vec4 uv)
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
    const auto across = [&](float fraction) { return uv.x + (uv.z - uv.x) * fraction; };
    const auto down = [&](float fraction) { return uv.y + (uv.w - uv.y) * fraction; };
    const std::array<float, 4> columnsUv{across(0.0f), across(image.border.x), across(1.0f - image.border.z), across(1.0f)};
    const std::array<float, 4> rowsUv{down(0.0f), down(image.border.y), down(1.0f - image.border.w), down(1.0f)};
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

void drawImage(Builder& builder, const DrawContext& context, const LaidOutRect& placed,
               const scene::UiImage& image)
{
    // A sprite is a part of its texture; any other asset is looked for among the textures.
    render::TextureHandle texture;
    math::Vec4 uv{0.0f, 0.0f, 1.0f, 1.0f};
    math::Vec2 pixels{0.0f};
    if (image.texture.isValid())
    {
        if (const std::optional<SpriteImage> sprite = context.sprites ? context.sprites(image.texture) : std::nullopt)
        {
            texture = sprite->texture;
            uv = sprite->uv;
            pixels = sprite->size;
        }
        else if (context.textures)
        {
            texture = context.textures(image.texture);
            pixels = texture.isValid() && context.textureSize ? context.textureSize(image.texture) : math::Vec2{0.0f};
        }
    }
    // Kept in its shape, the image takes the middle of its rectangle.
    LaidOutRect rect = placed;
    if (image.preserveAspect && pixels.x > 0.0f && pixels.y > 0.0f && rect.size().x > 0.0f && rect.size().y > 0.0f)
    {
        const float scale = std::min(rect.size().x / pixels.x, rect.size().y / pixels.y);
        const math::Vec2 fitted = pixels * scale;
        const math::Vec2 middle = (rect.min + rect.max) * 0.5f;
        rect.min = middle - fitted * 0.5f;
        rect.max = middle + fitted * 0.5f;
    }
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
        nineSlice(builder, draw, rect, image, color, pixels, uv);
        return;
    }
    if (isPlain(rect))
    {
        builder.quad(draw, rect.min, rect.max, math::Vec2{uv.x, uv.y}, math::Vec2{uv.z, uv.w}, color);
    }
    else
    {
        builder.quad(draw, corners(rect), math::Vec2{uv.x, uv.y}, math::Vec2{uv.z, uv.w}, color);
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
        const math::Vec4 white = withOpacity(math::Vec4{1.0f, 1.0f, 1.0f, 1.0f}, rect.opacity);
        if (isPlain(rect))
        {
            builder.quad(draw, image.min, image.max, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f}, white);
        }
        else
        {
            const std::array<math::Vec2, 4> corners{turned(rect, image.min), turned(rect, math::Vec2{image.max.x, image.min.y}),
                                                    turned(rect, image.max), turned(rect, math::Vec2{image.min.x, image.max.y})};
            builder.quad(draw, corners, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f}, white);
        }
    }
    if (letters.glyphs.empty())
    {
        return;
    }

    // A turned or scaled element turns and scales its letters with it.
    const bool plainRect = isPlain(rect);
    const float sharpness = textSharpness(*font.data, text.size) * builder.scale *
                            (plainRect ? 1.0f : std::sqrt(std::abs(rect.scale.x * rect.scale.y)));
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
                if (lean > 0.0f || !plainRect)
                {
                    std::array<math::Vec2, 4> corners{
                        math::Vec2{glyph.min.x + lean, glyph.min.y} + step,
                        math::Vec2{glyph.max.x + lean, glyph.min.y} + step,
                        math::Vec2{glyph.max.x, glyph.max.y} + step,
                        math::Vec2{glyph.min.x, glyph.max.y} + step};
                    if (!plainRect)
                    {
                        for (math::Vec2& corner : corners)
                        {
                            corner = turned(rect, corner);
                        }
                    }
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

// A triangle of one colour: the arrows of dropdowns, foldouts and sorted columns.
void triangle(Builder& builder, math::Vec2 a, math::Vec2 b, math::Vec2 c, math::Vec4 color)
{
    if (color.w <= 0.0f)
    {
        return;
    }
    render::UiDraw& draw = builder.batch(render::UiDrawKind::Quad, render::TextureHandle{},
                                         math::Vec4{0.0f}, 0.0f, 1.0f);
    builder.quad(draw, std::array<math::Vec2, 4>{a, b, c, c}, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f}, color);
}

// An arrow in a square of `size` around a point: down, or right when `right`, or up when `up`.
void arrow(Builder& builder, math::Vec2 centre, float size, bool right, bool up, math::Vec4 color)
{
    const float half = size * 0.5f;
    if (right)
    {
        triangle(builder, centre + math::Vec2{-half * 0.6f, -half}, centre + math::Vec2{half * 0.7f, 0.0f},
                 centre + math::Vec2{-half * 0.6f, half}, color);
    }
    else if (up)
    {
        triangle(builder, centre + math::Vec2{-half, half * 0.6f}, centre + math::Vec2{0.0f, -half * 0.7f},
                 centre + math::Vec2{half, half * 0.6f}, color);
    }
    else
    {
        triangle(builder, centre + math::Vec2{-half, -half * 0.6f}, centre + math::Vec2{half, -half * 0.6f},
                 centre + math::Vec2{0.0f, half * 0.7f}, color);
    }
}

// The thumbs of what scrolls, over what it holds, while the content is longer than the element.
void drawScrollbars(Builder& builder, const LaidOutRect& rect, const scene::UiScroll& scroll)
{
    const math::Vec2 size = rect.size();
    const float bar = std::max(scroll.scrollbarSize, 1.0f);
    const math::Vec4 color = withOpacity(scroll.scrollbarColor, rect.opacity);
    if (scroll.vertical && rect.content.y > size.y + 0.5f)
    {
        const float thumb = std::max(size.y * size.y / rect.content.y, bar * 2.0f);
        const float room = std::max(rect.content.y - size.y, 1.0f);
        const float top = rect.min.y + (size.y - thumb) * std::clamp(scroll.offset.y / room, 0.0f, 1.0f);
        fill(builder, math::Vec2{rect.max.x - bar, top}, math::Vec2{rect.max.x - 1.0f, top + thumb}, color);
    }
    if (scroll.horizontal && rect.content.x > size.x + 0.5f)
    {
        const float thumb = std::max(size.x * size.x / rect.content.x, bar * 2.0f);
        const float room = std::max(rect.content.x - size.x, 1.0f);
        const float left = rect.min.x + (size.x - thumb) * std::clamp(scroll.offset.x / room, 0.0f, 1.0f);
        fill(builder, math::Vec2{left, rect.max.y - bar}, math::Vec2{left + thumb, rect.max.y - 1.0f}, color);
    }
}

// The index of a cell among the cells of its row.
[[nodiscard]] std::size_t cellIndex(const scene::Scene& scene, scene::Entity cell)
{
    std::size_t index = 0;
    for (scene::Entity sibling = scene.firstChild(scene.parent(cell)); sibling.isValid() && sibling != cell;
         sibling = scene.nextSibling(sibling))
    {
        if (scene.has<scene::UiRect>(sibling))
        {
            ++index;
        }
    }
    return index;
}

// What the controls draw over their own image: the arrow of a dropdown and of a foldout, the bar of
// a splitter, and the arrow of the column a table is sorted by, with the edges of its header.
void drawControls(Builder& builder, const scene::Scene& scene, const DrawContext& context, const LaidOutRect& rect)
{
    const float height = rect.size().y;
    if (const scene::UiDropdown* const dropdown = scene.tryGet<scene::UiDropdown>(rect.entity))
    {
        arrow(builder, math::Vec2{rect.max.x - height * 0.45f, rect.min.y + height * 0.5f}, height * 0.28f, false,
              false, withOpacity(dropdown->arrowColor, rect.opacity));
    }
    if (const scene::UiFoldout* const foldout = scene.tryGet<scene::UiFoldout>(rect.entity))
    {
        const float size = std::min(height, 28.0f) * 0.32f;
        arrow(builder, math::Vec2{rect.min.x + std::min(height, 28.0f) * 0.5f, rect.min.y + height * 0.5f}, size,
              !foldout->expanded, false, withOpacity(foldout->arrowColor, rect.opacity));
    }
    if (const scene::UiSplitter* const splitter = scene.tryGet<scene::UiSplitter>(rect.entity))
    {
        const auto [barMin, barMax] = splitterBar(rect, *splitter);
        const bool grabbed = context.grabbed && context.grabbed(rect.entity);
        fill(builder, barMin, barMax, withOpacity(grabbed ? splitter->hoverColor : splitter->barColor, rect.opacity));
    }
    const scene::Entity row = scene.parent(rect.entity);
    const scene::UiTableRow* const header = row.isValid() ? scene.tryGet<scene::UiTableRow>(row) : nullptr;
    if (header != nullptr && header->header)
    {
        const scene::UiTable* table = nullptr;
        for (scene::Entity above = scene.parent(row); above.isValid() && table == nullptr; above = scene.parent(above))
        {
            table = scene.tryGet<scene::UiTable>(above);
        }
        if (table != nullptr)
        {
            const math::Vec4 color = withOpacity(table->arrowColor, rect.opacity);
            // The edge the pointer drags, and the way the column is sorted.
            fill(builder, math::Vec2{rect.max.x - 1.0f, rect.min.y + height * 0.2f},
                 math::Vec2{rect.max.x, rect.max.y - height * 0.2f}, withOpacity(color, 0.4f));
            if (table->sortColumn >= 0 && cellIndex(scene, rect.entity) == static_cast<std::size_t>(table->sortColumn))
            {
                arrow(builder, math::Vec2{rect.max.x - height * 0.45f, rect.min.y + height * 0.5f}, height * 0.26f,
                      false, table->sortAscending, color);
            }
        }
    }
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

// A box of one colour with rounded corners, for the handles of a colour picker.
void roundedBox(Builder& builder, math::Vec2 min, math::Vec2 max, float radius, math::Vec4 color)
{
    if (color.w <= 0.0f || max.x <= min.x || max.y <= min.y)
    {
        return;
    }
    const math::Vec4 shape{min.x * builder.scale, min.y * builder.scale, max.x * builder.scale, max.y * builder.scale};
    render::UiDraw& draw =
        builder.batch(render::UiDrawKind::RoundedQuad, render::TextureHandle{}, shape, radius * builder.scale, 1.0f);
    builder.quad(draw, min, max, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f}, color);
}

// A colour picker: its square of saturation and value for the hue it shows, its bar of hues, its bar
// of opacity over a checkerboard, and a handle on each. The colours are worked out as the eye sees
// them and drawn linear, the square in cells so that blending between their corners stays close.
void drawColorPicker(Builder& builder, const DrawContext& context, const LaidOutRect& rect, const scene::UiColorPicker& picker)
{
    const ColorPickerParts parts = colorPickerParts(rect, picker);
    const float intensity = std::max({1.0f, picker.color.x, picker.color.y, picker.color.z});
    math::Vec3 hsv{0.0f};
    if (!context.pickerHsv || !context.pickerHsv(rect.entity, hsv))
    {
        hsv = hsvFromRgb(math::Vec3{srgbFromLinear(picker.color.x / intensity), srgbFromLinear(picker.color.y / intensity),
                                    srgbFromLinear(picker.color.z / intensity)});
    }
    const float opacity = rect.opacity;
    const auto shown = [opacity](math::Vec3 color, float alpha) {
        const math::Vec3 rgb = rgbFromHsv(color);
        return math::Vec4{linearFromSrgb(rgb.x), linearFromSrgb(rgb.y), linearFromSrgb(rgb.z), alpha * opacity};
    };
    const auto mix = [](math::Vec2 from, math::Vec2 to, float x, float y) {
        return math::Vec2{from.x + (to.x - from.x) * x, from.y + (to.y - from.y) * y};
    };
    const float handle = std::clamp(picker.barSize * 0.45f, 3.0f, 12.0f);
    const math::Vec4 white{1.0f, 1.0f, 1.0f, opacity};
    const math::Vec4 shade{0.0f, 0.0f, 0.0f, 0.55f * opacity};

    // The square, white to the hue across and down to black.
    constexpr int cells = 8;
    if (parts.squareMax.x > parts.squareMin.x && parts.squareMax.y > parts.squareMin.y)
    {
        render::UiDraw& draw = builder.batch(render::UiDrawKind::Quad, render::TextureHandle{}, math::Vec4{0.0f}, 0.0f, 1.0f);
        for (int row = 0; row < cells; ++row)
        {
            for (int column = 0; column < cells; ++column)
            {
                const float left = static_cast<float>(column) / cells;
                const float right = static_cast<float>(column + 1) / cells;
                const float top = static_cast<float>(row) / cells;
                const float bottom = static_cast<float>(row + 1) / cells;
                builder.gradient(draw, mix(parts.squareMin, parts.squareMax, left, top),
                                 mix(parts.squareMin, parts.squareMax, right, bottom),
                                 {shown({hsv.x, left, 1.0f - top}, 1.0f), shown({hsv.x, right, 1.0f - top}, 1.0f),
                                  shown({hsv.x, right, 1.0f - bottom}, 1.0f), shown({hsv.x, left, 1.0f - bottom}, 1.0f)});
            }
        }
        const math::Vec2 at = mix(parts.squareMin, parts.squareMax, hsv.y, 1.0f - hsv.z);
        roundedBox(builder, at - math::Vec2{handle + 1.0f}, at + math::Vec2{handle + 1.0f}, handle + 1.0f, shade);
        roundedBox(builder, at - math::Vec2{handle}, at + math::Vec2{handle}, handle, white);
        roundedBox(builder, at - math::Vec2{handle - 2.0f}, at + math::Vec2{handle - 2.0f}, handle - 2.0f,
                   shown(hsv, 1.0f));
    }

    // The hues from top to bottom, red at both ends.
    constexpr int hues = 24;
    if (parts.hueMax.x > parts.hueMin.x && parts.hueMax.y > parts.hueMin.y)
    {
        render::UiDraw& draw = builder.batch(render::UiDrawKind::Quad, render::TextureHandle{}, math::Vec4{0.0f}, 0.0f, 1.0f);
        for (int band = 0; band < hues; ++band)
        {
            const float top = static_cast<float>(band) / hues;
            const float bottom = static_cast<float>(band + 1) / hues;
            const math::Vec4 upper = shown({top, 1.0f, 1.0f}, 1.0f);
            const math::Vec4 lower = shown({bottom, 1.0f, 1.0f}, 1.0f);
            builder.gradient(draw, mix(parts.hueMin, parts.hueMax, 0.0f, top), mix(parts.hueMin, parts.hueMax, 1.0f, bottom),
                             {upper, upper, lower, lower});
        }
        const float y = parts.hueMin.y + (parts.hueMax.y - parts.hueMin.y) * std::clamp(hsv.x, 0.0f, 1.0f);
        roundedBox(builder, math::Vec2{parts.hueMin.x - 3.0f, y - 3.0f}, math::Vec2{parts.hueMax.x + 3.0f, y + 3.0f}, 3.0f, shade);
        roundedBox(builder, math::Vec2{parts.hueMin.x - 2.0f, y - 2.0f}, math::Vec2{parts.hueMax.x + 2.0f, y + 2.0f}, 2.0f, white);
    }

    // The opacity, from none to whole, over squares that show through.
    if (picker.alpha && parts.alphaMax.x > parts.alphaMin.x && parts.alphaMax.y > parts.alphaMin.y)
    {
        const float cell = std::max((parts.alphaMax.y - parts.alphaMin.y) * 0.5f, 2.0f);
        const math::Vec4 light{0.8f, 0.8f, 0.8f, opacity};
        const math::Vec4 dark{0.45f, 0.45f, 0.45f, opacity};
        std::size_t index = 0;
        for (float x = parts.alphaMin.x; x < parts.alphaMax.x; x += cell, ++index)
        {
            const float right = std::min(x + cell, parts.alphaMax.x);
            fill(builder, math::Vec2{x, parts.alphaMin.y}, math::Vec2{right, parts.alphaMin.y + cell}, index % 2 == 0 ? light : dark);
            fill(builder, math::Vec2{x, parts.alphaMin.y + cell}, math::Vec2{right, parts.alphaMax.y}, index % 2 == 0 ? dark : light);
        }
        render::UiDraw& draw = builder.batch(render::UiDrawKind::Quad, render::TextureHandle{}, math::Vec4{0.0f}, 0.0f, 1.0f);
        const math::Vec4 clear = shown(hsv, 0.0f);
        const math::Vec4 whole = shown(hsv, 1.0f);
        builder.gradient(draw, parts.alphaMin, parts.alphaMax, {clear, whole, whole, clear});
        const float x = parts.alphaMin.x + (parts.alphaMax.x - parts.alphaMin.x) * std::clamp(picker.color.w, 0.0f, 1.0f);
        roundedBox(builder, math::Vec2{x - 3.0f, parts.alphaMin.y - 3.0f}, math::Vec2{x + 3.0f, parts.alphaMax.y + 3.0f}, 3.0f, shade);
        roundedBox(builder, math::Vec2{x - 2.0f, parts.alphaMin.y - 2.0f}, math::Vec2{x + 2.0f, parts.alphaMax.y + 2.0f}, 2.0f, white);
    }
}

// A series of values across the rectangle: a line through them, or a bar for each.
void drawPlot(Builder& builder, const LaidOutRect& rect, const scene::UiPlot& plot)
{
    const std::size_t count = plot.values.size();
    const math::Vec2 size = rect.size();
    const float low = std::min(plot.minValue, plot.maxValue);
    const float high = std::max(plot.minValue, plot.maxValue);
    const float range = high > low ? high - low : 1.0f;
    // How high a value stands, from 0 at the bottom to 1 at the top.
    const auto height = [&](float value) { return std::clamp((value - low) / range, 0.0f, 1.0f); };
    const auto colorOf = [&](std::size_t index) {
        return withOpacity(index < plot.colors.size() ? plot.colors[index] : plot.color, rect.opacity);
    };
    // The places of the values across: those of the first series, which the one behind follows.
    const auto slots = [&](std::size_t values) { return count > 0 ? count : values; };
    const auto across = [&](std::size_t index, std::size_t places) {
        return rect.min.x + (places > 1 ? static_cast<float>(index) / static_cast<float>(places - 1) : 0.5f) * size.x;
    };
    // A hair between bars, while they are wide enough to spare it.
    const auto gapOf = [](float width) { return width > 3.0f ? 1.0f : 0.0f; };

    // The value looked at, lit from the bottom to the top behind the series.
    if (plot.highlighted >= 0 && static_cast<std::size_t>(plot.highlighted) < count)
    {
        const auto index = static_cast<std::size_t>(plot.highlighted);
        const float width = size.x / static_cast<float>(count);
        float left = rect.min.x + static_cast<float>(index) * width;
        float right = left + width - gapOf(width);
        if (plot.kind == scene::UiPlotKind::Line)
        {
            const float half = std::max(width * 0.5f, plot.lineWidth);
            left = across(index, count) - half;
            right = across(index, count) + half;
        }
        fill(builder, math::Vec2{std::max(left, rect.min.x), rect.min.y}, math::Vec2{std::min(right, rect.max.x), rect.max.y},
             withOpacity(plot.highlightColor, rect.opacity));
    }

    const auto series = [&](std::span<const float> values, bool back) {
        const std::size_t places = slots(values.size());
        if (values.empty() || places == 0 || (plot.colors.empty() && plot.color.w <= 0.0f))
        {
            return;
        }
        const auto tint = [&](std::size_t index) { return back ? multiply(colorOf(index), plot.backColor) : colorOf(index); };
        render::UiDraw& draw = builder.batch(render::UiDrawKind::Quad, render::TextureHandle{}, math::Vec4{0.0f}, 0.0f, 1.0f);
        const std::size_t shown = std::min(values.size(), places);
        if (plot.kind == scene::UiPlotKind::Line)
        {
            const auto point = [&](std::size_t index) {
                return math::Vec2{across(index, places), rect.max.y - height(values[index]) * size.y};
            };
            const float half = std::max(plot.lineWidth, 0.5f) * 0.5f;
            if (shown == 1)
            {
                const math::Vec2 at = point(0);
                builder.quad(draw, math::Vec2{rect.min.x, at.y - half}, math::Vec2{rect.max.x, at.y + half}, math::Vec2{0.0f},
                             math::Vec2{1.0f}, tint(0));
            }
            for (std::size_t index = 1; index < shown; ++index)
            {
                // Each segment is a thin quad along it, in the colour of the value it ends at.
                const math::Vec2 from = point(index - 1);
                const math::Vec2 to = point(index);
                const math::Vec2 along = to - from;
                const float length = std::sqrt(along.x * along.x + along.y * along.y);
                if (length <= 0.0f)
                {
                    continue;
                }
                const math::Vec2 side{-along.y / length * half, along.x / length * half};
                builder.quad(draw, std::array<math::Vec2, 4>{from + side, to + side, to - side, from - side}, math::Vec2{0.0f},
                             math::Vec2{1.0f}, tint(index));
            }
            return;
        }
        const float width = size.x / static_cast<float>(places);
        const float gap = gapOf(width);
        const float middle = (rect.min.y + rect.max.y) * 0.5f;
        for (std::size_t index = 0; index < shown; ++index)
        {
            const float left = rect.min.x + static_cast<float>(index) * width;
            const float extent = height(values[index]);
            if (plot.kind == scene::UiPlotKind::Bars)
            {
                builder.quad(draw, math::Vec2{left, rect.max.y - extent * size.y}, math::Vec2{left + width - gap, rect.max.y},
                             math::Vec2{0.0f}, math::Vec2{1.0f}, tint(index));
            }
            else
            {
                const float half = std::max(extent * size.y * 0.5f, 0.5f);
                builder.quad(draw, math::Vec2{left, middle - half}, math::Vec2{left + width - gap, middle + half}, math::Vec2{0.0f},
                             math::Vec2{1.0f}, tint(index));
            }
        }
    };
    series(plot.backValues, true);
    series(plot.values, false);

    // The lines of the values the series are measured against, over them.
    for (const float guide : plot.guides)
    {
        if (guide < low || guide > high)
        {
            continue;
        }
        const float y = rect.max.y - height(guide) * size.y;
        fill(builder, math::Vec2{rect.min.x, y - 0.5f}, math::Vec2{rect.max.x, y + 0.5f}, withOpacity(plot.guideColor, rect.opacity));
    }
    if (plot.marker >= 0.0f)
    {
        const float x = rect.min.x + std::clamp(plot.marker, 0.0f, 1.0f) * size.x;
        const float half = std::max(plot.lineWidth, 1.0f) * 0.5f;
        fill(builder, math::Vec2{x - half, rect.min.y}, math::Vec2{x + half, rect.max.y}, withOpacity(plot.markerColor, rect.opacity));
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
    // What scrolls draws its bars once what it holds is drawn, over it: the elements waiting for
    // the end of their subtree, the innermost last.
    std::vector<std::size_t> waiting;
    const auto drawWaiting = [&](std::size_t upTo) {
        while (!waiting.empty() && subtreeEnd(layout.rects, waiting.back()) <= upTo)
        {
            const LaidOutRect& scrolling = layout.rects[waiting.back()];
            builder.clip = isClipped(scrolling.clip) ? scrolling.clip * layout.scale : math::Vec4{0.0f};
            drawScrollbars(builder, scrolling, scene.get<scene::UiScroll>(scrolling.entity));
            waiting.pop_back();
        }
    };
    for (std::size_t index = 0; index < layout.rects.size(); ++index)
    {
        drawWaiting(index);
        const LaidOutRect& rect = layout.rects[index];
        if (!rect.visible || rect.opacity <= 0.0f || rect.size().x <= 0.0f ||
            rect.size().y <= 0.0f)
        {
            continue;
        }
        // A modal darkens the whole canvas under it.
        if (const scene::UiPopup* const popup = scene.tryGet<scene::UiPopup>(rect.entity);
            popup != nullptr && popup->kind == scene::UiPopupKind::Modal)
        {
            builder.clip = math::Vec4{0.0f};
            fill(builder, math::Vec2{0.0f}, layout.canvasSize, withOpacity(popup->veilColor, rect.opacity));
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
        if (const scene::UiColorPicker* const picker = scene.tryGet<scene::UiColorPicker>(rect.entity))
        {
            drawColorPicker(builder, context, rect, *picker);
        }
        if (const scene::UiPlot* const plot = scene.tryGet<scene::UiPlot>(rect.entity))
        {
            drawPlot(builder, rect, *plot);
        }
        if (const scene::UiText* const text = scene.tryGet<scene::UiText>(rect.entity))
        {
            if (const scene::UiInput* const field = scene.tryGet<scene::UiInput>(rect.entity))
            {
                drawField(builder, context, rect, *text, *field);
            }
            else if (scene.tryGet<scene::UiDropdown>(rect.entity) != nullptr)
            {
                // The chosen option stands off the left edge, and leaves the right to the arrow.
                LaidOutRect inner = rect;
                inner.min.x += rect.size().y * 0.3f;
                inner.max.x = std::max(inner.min.x, rect.max.x - rect.size().y * 0.9f);
                drawText(builder, context, inner, *text);
            }
            else
            {
                drawText(builder, context, rect, *text);
            }
        }
        drawControls(builder, scene, context, rect);
        if (const scene::UiScroll* const scroll = scene.tryGet<scene::UiScroll>(rect.entity);
            scroll != nullptr && scroll->scrollbar)
        {
            waiting.push_back(index);
        }
    }
    drawWaiting(layout.rects.size());
    // Batches that ended up empty would draw nothing; they are dropped so the renderer skips them.
    std::erase_if(world.uiDraws, [](const render::UiDraw& draw) { return draw.indexCount == 0; });
}

void drawOverlayBox(render::RenderWorld& world, math::Vec2 min, math::Vec2 max, math::Vec4 color, float radius)
{
    if (color.w <= 0.0f || max.x <= min.x || max.y <= min.y)
    {
        return;
    }
    Builder builder{.world = world};
    render::UiDraw& draw = builder.batch(radius > 0.0f ? render::UiDrawKind::RoundedQuad : render::UiDrawKind::Quad,
                                         render::TextureHandle{}, math::Vec4{min.x, min.y, max.x, max.y}, radius, 1.0f);
    builder.quad(draw, min, max, math::Vec2{0.0f, 0.0f}, math::Vec2{1.0f, 1.0f}, color);
    std::erase_if(world.uiDraws, [](const render::UiDraw& batch) { return batch.indexCount == 0; });
}

void drawOverlayText(render::RenderWorld& world, const DrawContext& context, math::Vec2 min, math::Vec2 max,
                     const OverlayText& text)
{
    if (!context.fonts || text.text.empty())
    {
        return;
    }
    const FontRef font = context.fonts(text.font.isValid() ? text.font : context.defaultFont);
    if (font.data == nullptr || !font.atlas.isValid())
    {
        return;
    }
    Builder builder{.world = world, .clip = math::Vec4{min.x, min.y, max.x, max.y}};
    TextLayoutResult letters;
    const bool wrap = text.wrapWidth > 0.0f;
    layoutText(*font.data, text.text,
               TextStyle{.size = text.size, .verticalAlign = scene::TextVerticalAlign::Middle, .wrap = wrap}, min,
               wrap ? math::Vec2{min.x + text.wrapWidth, max.y} : max, letters);
    const scene::UiText style{.text = std::string(text.text), .size = text.size, .color = text.color};
    drawTextLayout(builder, context, LaidOutRect{.min = min, .max = max}, style, letters, font, text.color);
    std::erase_if(world.uiDraws, [](const render::UiDraw& batch) { return batch.indexCount == 0; });
}

math::Vec2 measureOverlayText(const DrawContext& context, const OverlayText& text)
{
    if (!context.fonts || text.text.empty())
    {
        return math::Vec2{0.0f};
    }
    const FontRef font = context.fonts(text.font.isValid() ? text.font : context.defaultFont);
    const bool wrap = text.wrapWidth > 0.0f;
    return font.data != nullptr ? measureText(*font.data, text.text, TextStyle{.size = text.size, .wrap = wrap},
                                              wrap ? text.wrapWidth : 0.0f)
                                : math::Vec2{0.0f};
}

DrawListMark markDrawList(const render::RenderWorld& world) noexcept
{
    return DrawListMark{.vertices = world.uiVertices.size(), .draws = world.uiDraws.size()};
}

void placeDrawList(render::RenderWorld& world, DrawListMark mark, math::Vec2 offset, float scale) noexcept
{
    const auto place = [&](math::Vec2 point) { return point * scale + offset; };
    for (std::size_t index = mark.vertices; index < world.uiVertices.size(); ++index)
    {
        world.uiVertices[index].position = place(world.uiVertices[index].position);
    }
    for (std::size_t index = mark.draws; index < world.uiDraws.size(); ++index)
    {
        render::UiDraw& draw = world.uiDraws[index];
        const math::Vec2 rectMin = place(math::Vec2{draw.rect.x, draw.rect.y});
        const math::Vec2 rectMax = place(math::Vec2{draw.rect.z, draw.rect.w});
        draw.rect = math::Vec4{rectMin.x, rectMin.y, rectMax.x, rectMax.y};
        draw.radius *= scale;
        // Fewer pixels for the same letters: their edges soften over fewer of them.
        draw.sharpness *= scale;
        // An empty cut draws the whole image, and stays empty.
        if (isClipped(draw.clip))
        {
            const math::Vec2 clipMin = place(math::Vec2{draw.clip.x, draw.clip.y});
            const math::Vec2 clipMax = place(math::Vec2{draw.clip.z, draw.clip.w});
            draw.clip = math::Vec4{clipMin.x, clipMin.y, clipMax.x, clipMax.y};
        }
    }
}

} // namespace devex::ui
