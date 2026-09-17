#include "Icons.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <imgui.h>
#include <imgui_internal.h>

#include <plutosvg/plutosvg.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <cstdio>
#include <memory>
#include <optional>

namespace devex::tools::detail {
namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(Icon::Count)> fileNames{
#define DEVEX_ICON_FILE(name, file) std::string_view(file),
    DEVEX_EDITOR_ICONS(DEVEX_ICON_FILE)
#undef DEVEX_ICON_FILE
};

// Icons are slightly smaller than the line height of the text around them, and centered on it.
constexpr float iconHeightInLines = 0.9f;

struct DocumentDeleter
{
    void operator()(plutosvg_document_t* document) const noexcept
    {
        plutosvg_document_destroy(document);
    }
};

struct SurfaceDeleter
{
    void operator()(plutovg_surface_t* surface) const noexcept
    {
        plutovg_surface_destroy(surface);
    }
};

[[nodiscard]] const IconSet* iconSetOf(const ImFontConfig* source) noexcept
{
    return static_cast<const IconSet*>(source->FontLoaderData);
}

[[nodiscard]] std::optional<Icon> iconOf(const ImFontConfig* source, ImWchar codepoint) noexcept
{
    if (codepoint < firstIconCodepoint || codepoint >= codepointOf(Icon::Count))
    {
        return std::nullopt;
    }
    const auto icon = static_cast<Icon>(codepoint - firstIconCodepoint);
    return iconSetOf(source)->contains(icon) ? std::optional<Icon>(icon) : std::nullopt;
}

bool initializeSource(ImFontAtlas* /*atlas*/, ImFontConfig* source)
{
    // The set travels in FontData, which the atlas leaves alone when it does not own it.
    source->FontLoaderData = source->FontData;
    return source->FontLoaderData != nullptr;
}

void destroySource(ImFontAtlas* /*atlas*/, ImFontConfig* source)
{
    source->FontLoaderData = nullptr;
}

bool containsGlyph(ImFontAtlas* /*atlas*/, ImFontConfig* source, ImWchar codepoint)
{
    return iconOf(source, codepoint).has_value();
}

bool loadGlyph(ImFontAtlas* atlas, ImFontConfig* source, ImFontBaked* baked, void* /*loaderData*/, ImWchar codepoint,
               ImFontGlyph* glyph, float* advance)
{
    const std::optional<Icon> icon = iconOf(source, codepoint);
    if (!icon)
    {
        return false;
    }
    const float size = std::round(baked->Size * iconHeightInLines);
    if (advance != nullptr)
    {
        *advance = size;
        return true;
    }

    const float density = source->RasterizerDensity * baked->RasterizerDensity;
    const auto pixels = static_cast<std::uint32_t>(std::max(1.0f, std::round(size * density)));
    const core::Result<SvgImage> image = renderSvg(iconSetOf(source)->svg(*icon), pixels, pixels);
    glyph->Codepoint = codepoint;
    glyph->AdvanceX = size;
    if (!image)
    {
        DEVEX_LOG_WARNING("Cannot draw the icon '{}': {}", iconFileName(*icon), image.error());
        return true;
    }

    const ImFontAtlasRectId pack = ImFontAtlasPackAddRect(atlas, static_cast<int>(pixels), static_cast<int>(pixels));
    if (pack == ImFontAtlasRectId_Invalid)
    {
        return false;
    }
    ImTextureRect* const rect = ImFontAtlasPackGetRect(atlas, pack);
    const float top = std::round((baked->Size - size) * 0.5f);
    glyph->X0 = 0.0f;
    glyph->Y0 = top;
    glyph->X1 = static_cast<float>(rect->w) / density;
    glyph->Y1 = top + static_cast<float>(rect->h) / density;
    glyph->Visible = true;
    glyph->PackId = pack;

    if (IconSet::isColored(*icon))
    {
        glyph->Colored = true;
        ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, source, glyph, rect, image->rgba.data(), ImTextureFormat_RGBA32,
                                           static_cast<int>(pixels * 4));
    }
    else
    {
        // Monochrome icons take the color of the text: only their coverage is kept.
        std::vector<std::uint8_t> alpha(std::size_t{pixels} * pixels);
        for (std::size_t index = 0; index < alpha.size(); ++index)
        {
            alpha[index] = image->rgba[index * 4 + 3];
        }
        ImFontAtlasBakedSetFontGlyphBitmap(atlas, baked, source, glyph, rect, alpha.data(), ImTextureFormat_Alpha8,
                                           static_cast<int>(pixels));
    }
    return true;
}

} // namespace

std::string withIcon(IconText icon, std::string_view label)
{
    return std::format("{}  {}", std::string_view(icon), label);
}

std::string_view iconFileName(Icon icon) noexcept
{
    return fileNames[static_cast<std::size_t>(icon)];
}

core::Result<SvgImage> renderSvg(std::string_view svg, std::uint32_t width, std::uint32_t height)
{
    const std::unique_ptr<plutosvg_document_t, DocumentDeleter> document(
        plutosvg_document_load_from_data(svg.data(), static_cast<int>(svg.size()), -1.0f, -1.0f, nullptr, nullptr));
    if (document == nullptr)
    {
        return core::makeError(core::ErrorCode::Parse, "the SVG document cannot be read");
    }
    const plutovg_color_t white{1.0f, 1.0f, 1.0f, 1.0f};
    const std::unique_ptr<plutovg_surface_t, SurfaceDeleter> surface(plutosvg_document_render_to_surface(
        document.get(), nullptr, static_cast<int>(width), static_cast<int>(height), &white, nullptr, nullptr));
    if (surface == nullptr)
    {
        return core::makeError(core::ErrorCode::Parse, "the SVG document cannot be drawn");
    }

    SvgImage image{.width = width, .height = height};
    image.rgba.resize(std::size_t{width} * height * 4);
    const unsigned char* const data = plutovg_surface_get_data(surface.get());
    const auto stride = static_cast<std::size_t>(plutovg_surface_get_stride(surface.get()));
    for (std::size_t y = 0; y < height; ++y)
    {
        for (std::size_t x = 0; x < width; ++x)
        {
            // Premultiplied ARGB in native byte order: B, G, R, A on little-endian machines.
            const unsigned char* const pixel = data + y * stride + x * 4;
            const std::uint8_t alpha = pixel[3];
            const auto unpremultiply = [alpha](std::uint8_t channel) {
                return alpha == 0 ? std::uint8_t{0}
                                  : static_cast<std::uint8_t>(std::min(255, (channel * 255 + alpha / 2) / alpha));
            };
            std::uint8_t* const out = &image.rgba[(y * width + x) * 4];
            out[0] = unpremultiply(pixel[2]);
            out[1] = unpremultiply(pixel[1]);
            out[2] = unpremultiply(pixel[0]);
            out[3] = alpha;
        }
    }
    return image;
}

IconSet IconSet::load(const std::filesystem::path& directory)
{
    IconSet set;
    std::size_t missing = 0;
    for (std::size_t index = 0; index < set.m_documents.size(); ++index)
    {
        const std::filesystem::path file = directory / core::pathFromUtf8(std::string(fileNames[index]) + ".svg");
        if (core::Result<std::string> text = core::readTextFile(file))
        {
            set.m_documents[index] = std::move(*text);
        }
        else
        {
            ++missing;
        }
    }
    if (missing > 0)
    {
        DEVEX_LOG_WARNING("{} editor icons are missing from {}", missing, core::toUtf8(directory));
    }
    return set;
}

bool IconSet::contains(Icon icon) const noexcept
{
    return icon < Icon::Count && !m_documents[static_cast<std::size_t>(icon)].empty();
}

std::string_view IconSet::svg(Icon icon) const noexcept
{
    return contains(icon) ? std::string_view(m_documents[static_cast<std::size_t>(icon)]) : std::string_view();
}

bool IconSet::isColored(Icon icon) noexcept
{
    return icon == Icon::Logo;
}

const ImFontLoader& iconFontLoader() noexcept
{
    static const ImFontLoader loader = [] {
        ImFontLoader result;
        result.Name = "Devex icons";
        result.FontSrcInit = initializeSource;
        result.FontSrcDestroy = destroySource;
        result.FontSrcContainsGlyph = containsGlyph;
        result.FontBakedLoadGlyph = loadGlyph;
        return result;
    }();
    return loader;
}

ImFontConfig iconFontSource(IconSet& icons)
{
    ImFontConfig source;
    source.MergeMode = true;
    source.FontLoader = &iconFontLoader();
    source.FontData = &icons;
    source.FontDataSize = 1;
    source.FontDataOwnedByAtlas = false;
    std::snprintf(source.Name, sizeof(source.Name), "%s", "Devex icons");
    return source;
}

} // namespace devex::tools::detail
