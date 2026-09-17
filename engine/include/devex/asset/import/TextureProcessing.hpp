#pragma once

#include <devex/asset/TextureData.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/JobSystem.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace devex::asset {

// Largest width or height accepted for a source image.
inline constexpr std::uint32_t maxImageSize = 16384;

// Uncompressed 8-bit RGBA pixels, row by row from the top-left corner.
struct Image
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba;
};

// Decodes a PNG, JPEG, TGA or BMP file held in memory.
[[nodiscard]] core::Result<Image> decodeImage(std::span<const std::byte> encoded);

// Resamples an image whose colors are in sRGB, with premultiplied alpha during filtering.
[[nodiscard]] Image resizeImage(const Image& image, std::uint32_t width, std::uint32_t height);

// Encodes the image as a PNG file.
[[nodiscard]] std::vector<std::byte> encodePng(const Image& image);

// Linear RGBA pixels with an unbounded range, row by row from the top-left corner.
struct FloatImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<float> rgba;
};

// True for Radiance .hdr files.
[[nodiscard]] bool isHighDynamicRange(std::span<const std::byte> encoded) noexcept;
[[nodiscard]] core::Result<FloatImage> decodeFloatImage(std::span<const std::byte> encoded);

// A half-float texture with box-filtered mip levels. Values beyond the half-float range are
// clamped.
[[nodiscard]] core::Result<TextureData> buildFloatTexture(const FloatImage& image, bool mipmaps,
                                                          core::JobSystem* jobs = nullptr);

enum class TextureQuality : std::uint8_t
{
    Fast,
    Normal,
    High,
};

[[nodiscard]] std::string_view toString(TextureQuality quality) noexcept;
[[nodiscard]] std::optional<TextureQuality> parseTextureQuality(std::string_view text) noexcept;

struct TextureBuildOptions
{
    // The pixels are colors encoded in sRGB. Ignored for normal maps, which are always linear.
    bool srgb = true;
    // Tangent-space normals: mip levels are renormalized and only X and Y are stored (BC5).
    bool normalMap = false;
    bool mipmaps = true;
    // BC7 for colors and data, BC5 for normal maps; uncompressed RGBA8 otherwise.
    bool compress = true;
    TextureQuality quality = TextureQuality::Normal;
};

// Generates the mip chain and compresses every level. Rows are processed in parallel on the jobs
// when given. Returns an error when cancelled.
[[nodiscard]] core::Result<TextureData> buildTexture(const Image& image,
                                                     const TextureBuildOptions& options,
                                                     core::JobSystem* jobs = nullptr,
                                                     const std::atomic<bool>* cancelled = nullptr);

// Decodes one level back to RGBA8, whatever its format. Channels a format does not store read as
// 0, and alpha as 255.
[[nodiscard]] core::Result<Image> decodeTextureLevel(const TextureData& texture, std::size_t level);

} // namespace devex::asset
