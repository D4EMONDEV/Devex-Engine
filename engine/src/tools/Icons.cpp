#include "Icons.hpp"

#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

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
        if (index == static_cast<std::size_t>(Icon::Logo))
        {
            const auto bytes = core::readBinaryFile(directory / "devex.png");
            if (bytes)
            {
                if (auto image = asset::decodeImage(*bytes))
                {
                    set.m_logo = {.width = image->width, .height = image->height, .rgba = std::move(image->rgba)};
                    continue;
                }
            }
            ++missing;
            continue;
        }
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
    if (icon == Icon::Logo)
    {
        return !m_logo.rgba.empty();
    }
    return icon < Icon::Count && !m_documents[static_cast<std::size_t>(icon)].empty();
}

std::string_view IconSet::svg(Icon icon) const noexcept
{
    return contains(icon) ? std::string_view(m_documents[static_cast<std::size_t>(icon)]) : std::string_view();
}

core::Result<SvgImage> IconSet::render(Icon icon, std::uint32_t width, std::uint32_t height) const
{
    if (icon != Icon::Logo)
    {
        return renderSvg(svg(icon), width, height);
    }
    if (m_logo.rgba.empty() || width == 0 || height == 0)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the logo or its requested size is empty");
    }
    const double scale = std::min(static_cast<double>(width) / m_logo.width,
                                  static_cast<double>(height) / m_logo.height);
    const auto fittedWidth = std::clamp(static_cast<std::uint32_t>(std::round(m_logo.width * scale)), 1u, width);
    const auto fittedHeight = std::clamp(static_cast<std::uint32_t>(std::round(m_logo.height * scale)), 1u, height);
    const asset::Image fitted = asset::resizeImage(
        {.width = m_logo.width, .height = m_logo.height, .rgba = m_logo.rgba}, fittedWidth, fittedHeight);
    SvgImage image{.width = width, .height = height, .rgba = std::vector<std::uint8_t>(std::size_t{width} * height * 4)};
    const std::size_t left = (width - fittedWidth) / 2;
    const std::size_t top = (height - fittedHeight) / 2;
    for (std::size_t row = 0; row < fittedHeight; ++row)
    {
        std::copy_n(fitted.rgba.data() + row * fittedWidth * 4, std::size_t{fittedWidth} * 4,
                    image.rgba.data() + ((top + row) * width + left) * 4);
    }
    return image;
}

bool IconSet::isColored(Icon icon) noexcept
{
    return icon == Icon::Logo;
}

} // namespace devex::tools::detail
