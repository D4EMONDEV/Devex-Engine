#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/Assert.hpp>

#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <basisu/encoder/basisu_bc15_spmd.h>
#include <basisu/encoder/basisu_bc7e_scalar.h>
#include <basisu/encoder/basisu_enc.h>
#include <basisu/encoder/basisu_gpu_texture.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <mutex>

namespace devex::asset {
namespace {

void initializeEncoders()
{
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        basisu::basisu_encoder_init();
        bc7e_scalar::bc7e_compress_block_init();
        basisu::bc_spmd::init();
    });
}

[[nodiscard]] bool isCancelled(const std::atomic<bool>* cancelled) noexcept
{
    return cancelled != nullptr && cancelled->load(std::memory_order_relaxed);
}

// Runs body(row) for every row, in parallel when jobs are available.
void forEachRow(core::JobSystem* jobs, std::uint32_t rows,
                const std::function<void(std::size_t row)>& body)
{
    if (jobs != nullptr)
    {
        jobs->parallelFor(rows, body);
        return;
    }
    for (std::size_t row = 0; row < rows; ++row)
    {
        body(row);
    }
}

class SrgbTable
{
public:
    SrgbTable() noexcept
    {
        for (std::size_t value = 0; value < m_linear.size(); ++value)
        {
            const double channel = static_cast<double>(value) / 255.0;
            m_linear[value] = static_cast<float>(channel <= 0.04045
                                                     ? channel / 12.92
                                                     : std::pow((channel + 0.055) / 1.055, 2.4));
        }
    }

    [[nodiscard]] float toLinear(std::uint8_t value) const noexcept
    {
        return m_linear[value];
    }

    [[nodiscard]] static std::uint8_t toSrgb(float linear) noexcept
    {
        const double clamped = std::clamp(static_cast<double>(linear), 0.0, 1.0);
        const double encoded = clamped <= 0.0031308 ? clamped * 12.92
                                                    : 1.055 * std::pow(clamped, 1.0 / 2.4) - 0.055;
        return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
    }

private:
    std::array<float, 256> m_linear{};
};

[[nodiscard]] const SrgbTable& srgbTable()
{
    static const SrgbTable table;
    return table;
}

[[nodiscard]] std::uint8_t toUnorm8(float value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

// Averages 2x2 texels of the previous level; odd edges repeat their last row or column.
[[nodiscard]] Image downsample(const Image& source, const TextureBuildOptions& options,
                               core::JobSystem* jobs)
{
    Image result;
    result.width = std::max(source.width / 2, 1u);
    result.height = std::max(source.height / 2, 1u);
    result.rgba.resize(static_cast<std::size_t>(result.width) * result.height * 4);
    const bool srgb = options.srgb && !options.normalMap;
    const SrgbTable& table = srgbTable();

    forEachRow(jobs, result.height, [&](std::size_t y) {
        for (std::uint32_t x = 0; x < result.width; ++x)
        {
            std::array<float, 4> sum{};
            for (std::uint32_t dy = 0; dy < 2; ++dy)
            {
                for (std::uint32_t dx = 0; dx < 2; ++dx)
                {
                    const std::size_t sx = std::min(x * 2 + dx, source.width - 1);
                    const std::size_t sy = std::min(static_cast<std::uint32_t>(y) * 2 + dy,
                                                    source.height - 1);
                    const std::uint8_t* const texel = &source.rgba[(sy * source.width + sx) * 4];
                    for (std::size_t channel = 0; channel < 4; ++channel)
                    {
                        const bool colorChannel = srgb && channel < 3;
                        sum[channel] += colorChannel ? table.toLinear(texel[channel])
                                                     : static_cast<float>(texel[channel]) / 255.0f;
                    }
                }
            }

            std::uint8_t* const texel = &result.rgba[(y * result.width + x) * 4];
            if (options.normalMap)
            {
                float nx = sum[0] * 0.5f - 1.0f;
                float ny = sum[1] * 0.5f - 1.0f;
                float nz = sum[2] * 0.5f - 1.0f;
                const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
                if (length > 1e-6f)
                {
                    nx /= length;
                    ny /= length;
                    nz /= length;
                }
                else
                {
                    nx = 0.0f;
                    ny = 0.0f;
                    nz = 1.0f;
                }
                texel[0] = toUnorm8(nx * 0.5f + 0.5f);
                texel[1] = toUnorm8(ny * 0.5f + 0.5f);
                texel[2] = toUnorm8(nz * 0.5f + 0.5f);
                texel[3] = toUnorm8(sum[3] * 0.25f);
                continue;
            }
            for (std::size_t channel = 0; channel < 4; ++channel)
            {
                const float average = sum[channel] * 0.25f;
                texel[channel] = srgb && channel < 3 ? SrgbTable::toSrgb(average) : toUnorm8(average);
            }
        }
    });
    return result;
}

// Copies the 4x4 texels of a block, repeating the last row and column of the image when the
// block extends past it.
void gatherBlock(const Image& image, std::uint32_t blockX, std::uint32_t blockY,
                 std::uint8_t* texels) noexcept
{
    for (std::uint32_t y = 0; y < 4; ++y)
    {
        const std::size_t sourceY = std::min(blockY * 4 + y, image.height - 1);
        for (std::uint32_t x = 0; x < 4; ++x)
        {
            const std::size_t sourceX = std::min(blockX * 4 + x, image.width - 1);
            std::memcpy(texels + (y * 4 + x) * 4, &image.rgba[(sourceY * image.width + sourceX) * 4],
                        4);
        }
    }
}

void initializeBc7Parameters(bc7e_scalar::bc7e_compress_block_params& parameters,
                             TextureQuality quality, bool perceptual)
{
    switch (quality)
    {
    case TextureQuality::Fast:
        bc7e_scalar::bc7e_compress_block_params_init_ultrafast(&parameters, perceptual);
        return;
    case TextureQuality::Normal:
        bc7e_scalar::bc7e_compress_block_params_init_fast(&parameters, perceptual);
        return;
    case TextureQuality::High:
        bc7e_scalar::bc7e_compress_block_params_init_slow(&parameters, perceptual);
        return;
    }
}

[[nodiscard]] TextureMip compressLevel(const Image& level, TextureFormat format,
                                       TextureQuality quality, core::JobSystem* jobs,
                                       const std::atomic<bool>* cancelled)
{
    TextureMip mip{.width = level.width, .height = level.height};
    mip.bytes.resize(mipByteSize(format, level.width, level.height));
    const std::uint32_t blocksWide = (level.width + 3) / 4;
    const std::uint32_t blocksHigh = (level.height + 3) / 4;

    bc7e_scalar::bc7e_compress_block_params bc7Parameters{};
    if (format != TextureFormat::Bc5Unorm)
    {
        initializeBc7Parameters(bc7Parameters, quality, format == TextureFormat::Bc7Srgb);
    }

    forEachRow(jobs, blocksHigh, [&](std::size_t row) {
        if (isCancelled(cancelled))
        {
            return;
        }
        const auto blockY = static_cast<std::uint32_t>(row);
        std::byte* const output = mip.bytes.data() + row * blocksWide * 16;
        std::vector<std::uint8_t> texels(static_cast<std::size_t>(blocksWide) * 16 * 4);
        for (std::uint32_t blockX = 0; blockX < blocksWide; ++blockX)
        {
            gatherBlock(level, blockX, blockY, texels.data() + blockX * 16 * 4);
        }

        if (format == TextureFormat::Bc5Unorm)
        {
            std::vector<basisu::color_rgba> pixels(texels.size() / 4);
            for (std::size_t index = 0; index < pixels.size(); ++index)
            {
                pixels[index].set(texels[index * 4], texels[index * 4 + 1], texels[index * 4 + 2],
                                  texels[index * 4 + 3]);
            }
            basisu::bc_spmd::encode_bc5(output, pixels.data(), blocksWide, true, true);
            return;
        }

        // bc7e reads each texel as a little-endian RGBA word and writes two 64-bit words.
        std::vector<std::uint32_t> words(blocksWide * 16);
        std::memcpy(words.data(), texels.data(), texels.size());
        std::vector<std::uint64_t> blocks(blocksWide * 2);
        bc7e_scalar::bc7e_compress_blocks(blocksWide, blocks.data(), words.data(), &bc7Parameters);
        std::memcpy(output, blocks.data(), blocks.size() * sizeof(std::uint64_t));
    });
    return mip;
}

} // namespace

core::Result<Image> decodeImage(std::span<const std::byte> encoded)
{
    if (encoded.empty() || encoded.size() > static_cast<std::size_t>(INT_MAX))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the image data size is invalid");
    }
    const auto* const data = reinterpret_cast<const stbi_uc*>(encoded.data());
    const auto size = static_cast<int>(encoded.size());

    int width = 0;
    int height = 0;
    int channels = 0;
    if (stbi_info_from_memory(data, size, &width, &height, &channels) == 0)
    {
        return core::makeError(core::ErrorCode::Parse, "unsupported or corrupt image: {}",
                               stbi_failure_reason());
    }
    if (width <= 0 || height <= 0 || static_cast<std::uint32_t>(width) > maxImageSize ||
        static_cast<std::uint32_t>(height) > maxImageSize)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "a {}x{} image exceeds the {}x{} limit", width, height,
                               maxImageSize, maxImageSize);
    }

    stbi_uc* const pixels = stbi_load_from_memory(data, size, &width, &height, &channels, 4);
    if (pixels == nullptr)
    {
        return core::makeError(core::ErrorCode::Parse, "cannot decode the image: {}",
                               stbi_failure_reason());
    }
    Image image{.width = static_cast<std::uint32_t>(width),
                .height = static_cast<std::uint32_t>(height)};
    image.rgba.assign(pixels, pixels + static_cast<std::size_t>(width) * height * 4);
    stbi_image_free(pixels);
    return image;
}

std::string_view toString(TextureQuality quality) noexcept
{
    switch (quality)
    {
    case TextureQuality::Fast:
        return "fast";
    case TextureQuality::Normal:
        return "normal";
    case TextureQuality::High:
        return "high";
    }
    return "unknown";
}

std::optional<TextureQuality> parseTextureQuality(std::string_view text) noexcept
{
    for (const TextureQuality quality :
         {TextureQuality::Fast, TextureQuality::Normal, TextureQuality::High})
    {
        if (toString(quality) == text)
        {
            return quality;
        }
    }
    return std::nullopt;
}

core::Result<TextureData> buildTexture(const Image& image, const TextureBuildOptions& options,
                                       core::JobSystem* jobs, const std::atomic<bool>* cancelled)
{
    if (image.width == 0 || image.height == 0 ||
        image.rgba.size() != static_cast<std::size_t>(image.width) * image.height * 4)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the image is empty or truncated");
    }

    const bool srgb = options.srgb && !options.normalMap;
    TextureData texture;
    if (options.compress)
    {
        initializeEncoders();
        texture.format = options.normalMap ? TextureFormat::Bc5Unorm
                         : srgb            ? TextureFormat::Bc7Srgb
                                           : TextureFormat::Bc7Unorm;
    }
    else
    {
        texture.format = srgb ? TextureFormat::Rgba8Srgb : TextureFormat::Rgba8Unorm;
    }

    const std::uint32_t levels = options.mipmaps ? fullMipCount(image.width, image.height) : 1;
    Image level = image;
    for (std::uint32_t index = 0; index < levels; ++index)
    {
        if (index > 0)
        {
            level = downsample(level, options, jobs);
        }
        if (isCancelled(cancelled))
        {
            return core::makeError(core::ErrorCode::InvalidState, "the import was cancelled");
        }

        if (options.compress)
        {
            texture.mips.push_back(compressLevel(level, texture.format, options.quality, jobs,
                                                 cancelled));
        }
        else
        {
            TextureMip& mip = texture.mips.emplace_back();
            mip.width = level.width;
            mip.height = level.height;
            mip.bytes.resize(level.rgba.size());
            std::memcpy(mip.bytes.data(), level.rgba.data(), level.rgba.size());
        }
    }
    if (isCancelled(cancelled))
    {
        return core::makeError(core::ErrorCode::InvalidState, "the import was cancelled");
    }
    return texture;
}

core::Result<Image> decodeTextureLevel(const TextureData& texture, std::size_t level)
{
    if (core::Result<void> valid = validate(texture); !valid)
    {
        return std::unexpected(valid.error());
    }
    if (level >= texture.mips.size())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the texture has {} levels",
                               texture.mips.size());
    }

    const TextureMip& mip = texture.mips[level];
    Image image{.width = mip.width, .height = mip.height};
    image.rgba.resize(static_cast<std::size_t>(mip.width) * mip.height * 4);
    if (!isBlockCompressed(texture.format))
    {
        std::memcpy(image.rgba.data(), mip.bytes.data(), image.rgba.size());
        return image;
    }

    initializeEncoders();
    const basisu::texture_format format = texture.format == TextureFormat::Bc5Unorm
                                              ? basisu::texture_format::cBC5
                                              : basisu::texture_format::cBC7;
    const std::uint32_t blocksWide = (mip.width + 3) / 4;
    const std::uint32_t blocksHigh = (mip.height + 3) / 4;
    std::array<basisu::color_rgba, 16> pixels{};
    for (std::uint32_t blockY = 0; blockY < blocksHigh; ++blockY)
    {
        for (std::uint32_t blockX = 0; blockX < blocksWide; ++blockX)
        {
            const std::byte* const block = mip.bytes.data() + (blockY * blocksWide + blockX) * 16;
            if (!basisu::unpack_block(format, block, pixels.data(), false))
            {
                return core::makeError(core::ErrorCode::Parse, "invalid block at ({}, {})", blockX,
                                       blockY);
            }
            for (std::uint32_t y = 0; y < 4 && blockY * 4 + y < mip.height; ++y)
            {
                for (std::uint32_t x = 0; x < 4 && blockX * 4 + x < mip.width; ++x)
                {
                    const basisu::color_rgba& pixel = pixels[y * 4 + x];
                    std::uint8_t* const texel =
                        &image.rgba[((blockY * 4 + y) * static_cast<std::size_t>(mip.width) +
                                     blockX * 4 + x) *
                                    4];
                    texel[0] = pixel.r;
                    texel[1] = pixel.g;
                    texel[2] = texture.format == TextureFormat::Bc5Unorm ? 0 : pixel.b;
                    texel[3] = texture.format == TextureFormat::Bc5Unorm ? 255 : pixel.a;
                }
            }
        }
    }
    return image;
}

} // namespace devex::asset
