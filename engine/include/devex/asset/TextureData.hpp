#pragma once

#include <devex/core/Error.hpp>

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace devex::asset {

// Pixel formats of cooked textures. The values are stored in cooked files: never reorder them.
enum class TextureFormat : std::uint8_t
{
    Rgba8Unorm = 1,
    // Color in sRGB encoding; sampling returns linear values.
    Rgba8Srgb = 2,
    // Two channels (red, green), for normal maps whose Z is reconstructed.
    Bc5Unorm = 3,
    Bc7Unorm = 4,
    Bc7Srgb = 5,
    // Linear high dynamic range colors, such as environment maps.
    Rgba16Float = 6,
};

[[nodiscard]] std::string_view toString(TextureFormat format) noexcept;
[[nodiscard]] bool isBlockCompressed(TextureFormat format) noexcept;
[[nodiscard]] bool isSrgb(TextureFormat format) noexcept;

// Bytes needed by one mip level of the given size.
[[nodiscard]] std::size_t mipByteSize(TextureFormat format, std::uint32_t width,
                                      std::uint32_t height) noexcept;

struct TextureMip
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::byte> bytes;
};

// A 2D texture with its mip chain, largest level first. Each level halves the previous size,
// rounding down, until the requested number of levels.
struct TextureData
{
    TextureFormat format = TextureFormat::Rgba8Srgb;
    std::vector<TextureMip> mips;
};

[[nodiscard]] core::Result<void> validate(const TextureData& texture);

// Number of levels of a complete mip chain down to 1x1.
[[nodiscard]] std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height) noexcept;

} // namespace devex::asset
