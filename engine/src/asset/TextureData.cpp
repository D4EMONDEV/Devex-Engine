#include <devex/asset/TextureData.hpp>

#include <algorithm>
#include <bit>

namespace devex::asset {

std::string_view toString(TextureFormat format) noexcept
{
    switch (format)
    {
    case TextureFormat::Rgba8Unorm:
        return "rgba8";
    case TextureFormat::Rgba8Srgb:
        return "rgba8-srgb";
    case TextureFormat::Bc5Unorm:
        return "bc5";
    case TextureFormat::Bc7Unorm:
        return "bc7";
    case TextureFormat::Bc7Srgb:
        return "bc7-srgb";
    case TextureFormat::Rgba16Float:
        return "rgba16f";
    }
    return "unknown";
}

bool isBlockCompressed(TextureFormat format) noexcept
{
    return format == TextureFormat::Bc5Unorm || format == TextureFormat::Bc7Unorm ||
           format == TextureFormat::Bc7Srgb;
}

bool isSrgb(TextureFormat format) noexcept
{
    return format == TextureFormat::Rgba8Srgb || format == TextureFormat::Bc7Srgb;
}

std::size_t mipByteSize(TextureFormat format, std::uint32_t width, std::uint32_t height) noexcept
{
    if (isBlockCompressed(format))
    {
        // Every BC format used here stores a 4x4 block in 16 bytes; partial blocks are padded.
        const std::size_t blocksWide = (static_cast<std::size_t>(width) + 3) / 4;
        const std::size_t blocksHigh = (static_cast<std::size_t>(height) + 3) / 4;
        return blocksWide * blocksHigh * 16;
    }
    const std::size_t bytesPerTexel = format == TextureFormat::Rgba16Float ? 8 : 4;
    return static_cast<std::size_t>(width) * height * bytesPerTexel;
}

core::Result<void> validate(const TextureData& texture)
{
    if (texture.mips.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the texture has no mip level");
    }
    const TextureMip& base = texture.mips.front();
    if (base.width == 0 || base.height == 0)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the texture is empty");
    }
    if (texture.mips.size() > fullMipCount(base.width, base.height))
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "{} mip levels exceed the {} of a {}x{} texture",
                               texture.mips.size(), fullMipCount(base.width, base.height),
                               base.width, base.height);
    }

    for (std::size_t level = 0; level < texture.mips.size(); ++level)
    {
        const TextureMip& mip = texture.mips[level];
        const std::uint32_t expectedWidth = std::max(base.width >> level, 1u);
        const std::uint32_t expectedHeight = std::max(base.height >> level, 1u);
        if (mip.width != expectedWidth || mip.height != expectedHeight)
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "mip level {} is {}x{} instead of {}x{}", level, mip.width,
                                   mip.height, expectedWidth, expectedHeight);
        }
        const std::size_t expectedBytes = mipByteSize(texture.format, mip.width, mip.height);
        if (mip.bytes.size() != expectedBytes)
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "mip level {} has {} bytes instead of {}", level,
                                   mip.bytes.size(), expectedBytes);
        }
    }
    return {};
}

std::uint32_t fullMipCount(std::uint32_t width, std::uint32_t height) noexcept
{
    return static_cast<std::uint32_t>(std::bit_width(std::max({width, height, 1u})));
}

} // namespace devex::asset
