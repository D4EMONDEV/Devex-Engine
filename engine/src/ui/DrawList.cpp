#include <devex/ui/DrawList.hpp>

#include <devex/scene/Scene.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/ui/TextLayout.hpp>

#include <algorithm>
#include <array>

namespace devex::ui {
namespace {

// Everything one canvas needs while it is turned into triangles.
struct Builder
{
    render::RenderWorld& world;
    float scale = 1.0f;

    // The draw the next triangles join, when they share its texture and its shape.
    [[nodiscard]] render::UiDraw& batch(render::UiDrawKind kind, render::TextureHandle texture,
                                        math::Vec4 rect, float radius, float sharpness)
    {
        const bool mergeable = kind != render::UiDrawKind::RoundedQuad;
        if (!world.uiDraws.empty())
        {
            render::UiDraw& last = world.uiDraws.back();
            if (mergeable && last.kind == kind && last.texture == texture &&
                last.sharpness == sharpness)
            {
                return last;
            }
        }
        world.uiDraws.push_back({.kind = kind,
                                 .texture = texture,
                                 .firstIndex = static_cast<std::uint32_t>(world.uiIndices.size()),
                                 .rect = rect,
                                 .radius = radius,
                                 .sharpness = sharpness});
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
// stretch one way and the middle stretches both. The borders are fractions of the texture, so the
// cut needs no knowledge of how large the texture is.
void nineSlice(Builder& builder, render::UiDraw& draw, const LaidOutRect& rect,
               const scene::UiImage& image, math::Vec4 color)
{
    // Drawn at the size the borders cover of the rectangle, never past its middle.
    const float left = std::min(image.border.x * rect.size().x, rect.size().x * 0.5f);
    const float top = std::min(image.border.y * rect.size().y, rect.size().y * 0.5f);
    const float right = std::min(image.border.z * rect.size().x, rect.size().x * 0.5f);
    const float bottom = std::min(image.border.w * rect.size().y, rect.size().y * 0.5f);
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
        nineSlice(builder, draw, rect, image, color);
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
                         .lineSpacing = text.lineSpacing},
               rect.min, rect.max, letters);
    if (letters.glyphs.empty())
    {
        return;
    }

    const float sharpness = textSharpness(*font.data, text.size) * builder.scale;
    const auto drawGlyphs = [&](math::Vec4 color, math::Vec2 shift) {
        render::UiDraw& draw =
            builder.batch(render::UiDrawKind::Text, font.atlas, math::Vec4{0.0f}, 0.0f, sharpness);
        for (const GlyphQuad& glyph : letters.glyphs)
        {
            builder.quad(draw, glyph.min + shift, glyph.max + shift, glyph.uvMin, glyph.uvMax,
                         color);
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
            drawGlyphs(outline, shift);
        }
    }
    drawGlyphs(withOpacity(text.color, rect.opacity), math::Vec2{0.0f, 0.0f});
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
        if (const scene::UiImage* const image = scene.tryGet<scene::UiImage>(rect.entity))
        {
            drawImage(builder, context, rect, *image);
        }
        if (const scene::UiText* const text = scene.tryGet<scene::UiText>(rect.entity))
        {
            drawText(builder, context, rect, *text);
        }
    }
    // Batches that ended up empty would draw nothing; they are dropped so the renderer skips them.
    std::erase_if(world.uiDraws, [](const render::UiDraw& draw) { return draw.indexCount == 0; });
}

} // namespace devex::ui
