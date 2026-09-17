#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

using devex::asset::Image;
using devex::asset::TextureBuildOptions;
using devex::asset::TextureFormat;

namespace {

const std::filesystem::path dataDirectory{DEVEX_TEST_DATA_DIRECTORY};

[[nodiscard]] Image gradient(std::uint32_t width, std::uint32_t height)
{
    Image image{.width = width, .height = height};
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            image.rgba.push_back(static_cast<std::uint8_t>(x * 255 / (width - 1)));
            image.rgba.push_back(static_cast<std::uint8_t>(y * 255 / (height - 1)));
            image.rgba.push_back(static_cast<std::uint8_t>((x * 7 + y * 3) % 256));
            image.rgba.push_back(255);
        }
    }
    return image;
}

// Peak signal-to-noise ratio over the color channels, in decibels.
[[nodiscard]] double psnr(const Image& expected, const Image& actual)
{
    double squaredError = 0.0;
    std::size_t samples = 0;
    for (std::size_t index = 0; index < expected.rgba.size(); ++index)
    {
        if (index % 4 == 3)
        {
            continue;
        }
        const double difference =
            static_cast<double>(expected.rgba[index]) - static_cast<double>(actual.rgba[index]);
        squaredError += difference * difference;
        ++samples;
    }
    const double meanSquaredError = squaredError / static_cast<double>(samples);
    return meanSquaredError == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / meanSquaredError);
}

} // namespace

TEST_CASE("PNG files decode to RGBA pixels", "[asset][texture]")
{
    const auto bytes = devex::core::readBinaryFile(dataDirectory / "checker.png");
    REQUIRE(bytes.has_value());
    const auto image = devex::asset::decodeImage(*bytes);
    REQUIRE(image.has_value());
    CHECK(image->width == 64);
    CHECK(image->height == 32);
    // The top-left cell is white, the next one black.
    CHECK(image->rgba[0] == 255);
    CHECK(image->rgba[16 * 4] == 0);

    const std::vector<std::byte> garbage(64, std::byte{0x42});
    CHECK(devex::asset::decodeImage(garbage).error().code == devex::core::ErrorCode::Parse);
}

TEST_CASE("sRGB mip levels average light, not encoded values", "[asset][texture]")
{
    // Two black and two white texels average to 50 % light, which sRGB encodes as 188.
    const Image image{.width = 2,
                      .height = 2,
                      .rgba = {0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255}};
    const auto texture = devex::asset::buildTexture(image, {.compress = false});
    REQUIRE(texture.has_value());
    CHECK(texture->format == TextureFormat::Rgba8Srgb);
    REQUIRE(texture->mips.size() == 2);
    CHECK(texture->mips[1].bytes[0] == std::byte{188});

    const auto linear = devex::asset::buildTexture(image, {.srgb = false, .compress = false});
    REQUIRE(linear.has_value());
    CHECK(linear->format == TextureFormat::Rgba8Unorm);
    CHECK(linear->mips[1].bytes[0] == std::byte{128});
}

TEST_CASE("BC7 textures keep a complete mip chain and close colors", "[asset][texture]")
{
    devex::core::JobSystem jobs(2);
    const Image image = gradient(37, 20);

    const auto texture = devex::asset::buildTexture(image, TextureBuildOptions{}, &jobs);
    REQUIRE(texture.has_value());
    CHECK(texture->format == TextureFormat::Bc7Srgb);
    CHECK(devex::asset::validate(*texture).has_value());
    REQUIRE(texture->mips.size() == 6);
    CHECK(texture->mips.back().width == 1);
    CHECK(texture->mips.back().height == 1);

    const auto decoded = devex::asset::decodeTextureLevel(*texture, 0);
    REQUIRE(decoded.has_value());
    CHECK(psnr(image, *decoded) > 35.0);
}

TEST_CASE("Normal maps compress to BC5 and stay normalized", "[asset][texture]")
{
    // Normals tilted along X, alternating direction every texel: their average points out.
    Image image{.width = 8, .height = 8};
    for (std::uint32_t texel = 0; texel < 64; ++texel)
    {
        const std::uint8_t x = ((texel % 8) % 2 == 0) ? 218 : 38;
        image.rgba.insert(image.rgba.end(), {x, 128, 218, 255});
    }

    const auto texture = devex::asset::buildTexture(image, {.normalMap = true});
    REQUIRE(texture.has_value());
    CHECK(texture->format == TextureFormat::Bc5Unorm);
    const auto level = devex::asset::decodeTextureLevel(*texture, 1);
    REQUIRE(level.has_value());
    // X averages to zero once renormalized; Z is not stored.
    CHECK(std::abs(static_cast<int>(level->rgba[0]) - 128) <= 3);
    CHECK(std::abs(static_cast<int>(level->rgba[1]) - 128) <= 3);
}

TEST_CASE("Cancelled texture builds stop with an error", "[asset][texture]")
{
    const std::atomic<bool> cancelled{true};
    const auto texture =
        devex::asset::buildTexture(gradient(16, 16), TextureBuildOptions{}, nullptr, &cancelled);
    CHECK_FALSE(texture.has_value());
}

TEST_CASE("Radiance HDR images build half-float textures", "[asset][texture]")
{
    // A 2x2 image whose pixels are all (2, 1, 0.5), in flat RGBE.
    std::string file = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
    for (int pixel = 0; pixel < 4; ++pixel)
    {
        file += std::string{static_cast<char>(128), static_cast<char>(64), static_cast<char>(32),
                            static_cast<char>(130)};
    }
    const std::span<const std::byte> bytes = std::as_bytes(std::span(file.data(), file.size()));
    REQUIRE(devex::asset::isHighDynamicRange(bytes));
    CHECK_FALSE(devex::asset::isHighDynamicRange(
        std::as_bytes(std::span(std::string_view("not an image").data(), 12))));

    const auto image = devex::asset::decodeFloatImage(bytes);
    REQUIRE(image.has_value());
    CHECK(image->width == 2);
    CHECK(image->rgba[0] == 2.0f);
    CHECK(image->rgba[2] == 0.5f);

    const auto texture = devex::asset::buildFloatTexture(*image, true);
    REQUIRE(texture.has_value());
    CHECK(texture->format == TextureFormat::Rgba16Float);
    REQUIRE(texture->mips.size() == 2);
    CHECK(devex::asset::validate(*texture).has_value());
    // 2.0 and 1.0 as half floats, little-endian.
    CHECK(texture->mips[1].bytes[1] == std::byte{0x40});
    CHECK(texture->mips[1].bytes[3] == std::byte{0x3C});
}
