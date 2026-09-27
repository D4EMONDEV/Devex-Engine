#include <devex/asset/Artifact.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/SpriteData.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

using Catch::Approx;
using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::asset::SpriteData;
using devex::asset::SpriteFramesData;

namespace {

// A sheet of 4 by 2 cells of 8 pixels: opaque cells of a color each, except the last two, the last
// one fully transparent.
[[nodiscard]] devex::asset::Image spriteSheet()
{
    devex::asset::Image image{.width = 32, .height = 16, .rgba = std::vector<std::uint8_t>(32 * 16 * 4, 0)};
    for (std::uint32_t y = 0; y < image.height; ++y)
    {
        for (std::uint32_t x = 0; x < image.width; ++x)
        {
            const std::uint32_t cell = (y / 8) * 4 + x / 8;
            std::uint8_t* const pixel = &image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4];
            if (cell == 7)
            {
                continue;
            }
            // The seventh cell has one visible pixel in its middle, the others are full.
            if (cell == 6 && (x % 8 != 4 || y % 8 != 4))
            {
                continue;
            }
            pixel[0] = static_cast<std::uint8_t>(cell * 30);
            pixel[1] = 200;
            pixel[2] = 50;
            pixel[3] = 255;
        }
    }
    return image;
}

// A PNG in a temporary directory, removed at the end of the test.
class TemporaryImage
{
public:
    explicit TemporaryImage(const devex::asset::Image& image)
        : m_directory(std::filesystem::temp_directory_path() / ("devex-sprites-" + devex::core::Uuid::generate().toString()))
    {
        std::filesystem::create_directories(m_directory);
        path = m_directory / "Hero.png";
        const std::vector<std::byte> png = devex::asset::encodePng(image);
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        REQUIRE(file.good());
    }

    ~TemporaryImage()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_directory, ignored);
    }

    TemporaryImage(const TemporaryImage&) = delete;
    TemporaryImage& operator=(const TemporaryImage&) = delete;

    std::filesystem::path path;

private:
    std::filesystem::path m_directory;
};

[[nodiscard]] devex::asset::ImportContext contextFor(const std::filesystem::path& file,
                                                     std::vector<devex::serialization::TextProperty> options)
{
    return devex::asset::ImportContext{
        .source = file,
        .mainId = AssetId::generate(),
        .name = "Hero",
        .options = std::move(options),
    };
}

} // namespace

TEST_CASE("A sprite measures its rectangle in meters and in texture coordinates", "[asset][sprite]")
{
    const SpriteData sprite{.texture = AssetId::generate(),
                            .x = 16,
                            .y = 8,
                            .width = 16,
                            .height = 8,
                            .textureWidth = 64,
                            .textureHeight = 32,
                            .pixelsPerUnit = 16.0f,
                            .border = {2.0f, 2.0f, 2.0f, 2.0f}};
    CHECK(sprite.size().x == Approx(1.0f));
    CHECK(sprite.size().y == Approx(0.5f));
    const devex::math::Vec4 uv = sprite.uvRect();
    CHECK(uv.x == Approx(0.25f));
    CHECK(uv.y == Approx(0.25f));
    CHECK(uv.z == Approx(0.5f));
    CHECK(uv.w == Approx(0.5f));
    CHECK(devex::asset::validate(sprite).has_value());

    SpriteData outside = sprite;
    outside.x = 60;
    CHECK_FALSE(devex::asset::validate(outside).has_value());
    SpriteData wideBorders = sprite;
    wideBorders.border = {10.0f, 0.0f, 10.0f, 0.0f};
    CHECK_FALSE(devex::asset::validate(wideBorders).has_value());
    SpriteData noScale = sprite;
    noScale.pixelsPerUnit = 0.0f;
    CHECK_FALSE(devex::asset::validate(noScale).has_value());
}

TEST_CASE("Sprites and sprite frames survive their cooked files", "[asset][sprite]")
{
    const SpriteData sprite{.texture = AssetId::generate(),
                            .x = 8,
                            .width = 8,
                            .height = 8,
                            .textureWidth = 32,
                            .textureHeight = 16,
                            .pixelsPerUnit = 8.0f,
                            .pivot = {0.5f, 0.0f},
                            .border = {1.0f, 2.0f, 3.0f, 1.0f}};
    const auto decodedSprite = devex::asset::decodeSprite(devex::asset::encodeSprite(sprite));
    REQUIRE(decodedSprite.has_value());
    CHECK(*decodedSprite == sprite);

    const SpriteFramesData frames{.animations = {{.name = "idle", .fps = 6.0f, .loop = true, .frames = {AssetId::generate()}},
                                                 {.name = "jump",
                                                  .fps = 12.0f,
                                                  .loop = false,
                                                  .frames = {AssetId::generate(), AssetId::generate()}}}};
    const auto decodedFrames = devex::asset::decodeSpriteFrames(devex::asset::encodeSpriteFrames(frames));
    REQUIRE(decodedFrames.has_value());
    CHECK(*decodedFrames == frames);
    REQUIRE(decodedFrames->find("jump") != nullptr);
    CHECK(decodedFrames->find("jump")->frames.size() == 2);
    CHECK(decodedFrames->find("run") == nullptr);
}

TEST_CASE("Sprite frames files keep their animations and refuse broken ones", "[asset][sprite]")
{
    const SpriteFramesData frames{.animations = {{.name = "run", .fps = 12.5f, .loop = true,
                                                  .frames = {AssetId::generate(), AssetId::generate()}},
                                                 {.name = "die", .fps = 8.0f, .loop = false}}};
    const std::string text = devex::asset::writeSpriteFramesFile(frames);
    CHECK(text.starts_with("[frames format=1]"));
    const auto parsed = devex::asset::parseSpriteFramesFile(text);
    REQUIRE(parsed.has_value());
    CHECK(*parsed == frames);

    CHECK_FALSE(devex::asset::parseSpriteFramesFile("[curve format=1]\n").has_value());
    CHECK_FALSE(devex::asset::parseSpriteFramesFile("[frames format=99]\n").has_value());
    CHECK_FALSE(devex::asset::parseSpriteFramesFile("[frames format=1]\n\n[animation name=\"a\"]\n\n[animation name=\"a\"]\n")
                    .has_value());
    CHECK_FALSE(devex::asset::parseSpriteFramesFile("[frames format=1]\n\n[animation name=\"a\" fps=0]\n").has_value());
    CHECK_FALSE(
        devex::asset::parseSpriteFramesFile("[frames format=1]\n\n[animation name=\"a\"]\nframes = list(\"not an asset\")\n")
            .has_value());
}

TEST_CASE("A texture cut into a grid imports its visible cells as sprites", "[asset][sprite]")
{
    const TemporaryImage image(spriteSheet());
    using devex::serialization::TextValue;
    devex::asset::ImportContext context =
        contextFor(image.path, {{"sprite_mode", TextValue(std::string("grid"))},
                                {"columns", TextValue(std::int64_t{4})},
                                {"rows", TextValue(std::int64_t{2})},
                                {"pixels_per_unit", TextValue(8.0)},
                                {"pivot", devex::serialization::makeCall("vec2", {TextValue(0.5), TextValue(0.0)})},
                                {"filter", TextValue(std::string("nearest"))},
                                {"compress", TextValue(false)},
                                {"mipmaps", TextValue(false)}});
    const auto result = devex::asset::importTextureFile(context);
    REQUIRE(result.has_value());
    // The texture, then the seven cells with a visible pixel: the last one is left out.
    REQUIRE(result->artifacts.size() == 8);
    CHECK(result->artifacts.front().type == AssetType::Texture);
    const auto texture = devex::asset::decodeTexture(result->artifacts.front().bytes);
    REQUIRE(texture.has_value());
    CHECK(texture->filter == devex::asset::TextureFilter::Nearest);

    const devex::asset::ImportedArtifact& fifth = result->artifacts[5];
    CHECK(fifth.type == AssetType::Sprite);
    CHECK(fifth.name == "Hero_4");
    const auto sprite = devex::asset::decodeSprite(fifth.bytes);
    REQUIRE(sprite.has_value());
    CHECK(sprite->texture == context.mainId);
    CHECK(sprite->x == 0);
    CHECK(sprite->y == 8);
    CHECK(sprite->width == 8);
    CHECK(sprite->height == 8);
    CHECK(sprite->size().x == Approx(1.0f));
    CHECK(sprite->pivot.y == Approx(0.0f));
    // The single visible pixel keeps its cell.
    CHECK(result->artifacts.back().name == "Hero_6");

    // Imported again, each cell keeps its identifier.
    devex::asset::ImportContext again = contextFor(image.path, context.options);
    again.mainId = context.mainId;
    again.subAssets = devex::asset::SubAssetIds(context.subAssets.entries());
    const auto reimported = devex::asset::importTextureFile(again);
    REQUIRE(reimported.has_value());
    for (std::size_t index = 1; index < result->artifacts.size(); ++index)
    {
        CHECK(reimported->artifacts[index].id == result->artifacts[index].id);
    }
}

TEST_CASE("A single sprite takes its whole texture, whose clear pixels take the color of their neighbours",
          "[asset][sprite]")
{
    // An opaque red pixel in a clear image.
    devex::asset::Image picture{.width = 4, .height = 4, .rgba = std::vector<std::uint8_t>(64, 0)};
    picture.rgba[(1 * 4 + 1) * 4 + 0] = 255;
    picture.rgba[(1 * 4 + 1) * 4 + 3] = 255;
    const TemporaryImage image(picture);
    using devex::serialization::TextValue;
    devex::asset::ImportContext context = contextFor(image.path, {{"sprite_mode", TextValue(std::string("single"))},
                                                                  {"compress", TextValue(false)},
                                                                  {"mipmaps", TextValue(false)}});
    const auto result = devex::asset::importTextureFile(context);
    REQUIRE(result.has_value());
    REQUIRE(result->artifacts.size() == 2);
    CHECK(result->artifacts[1].name == "Hero");
    const auto sprite = devex::asset::decodeSprite(result->artifacts[1].bytes);
    REQUIRE(sprite.has_value());
    CHECK(sprite->width == 4);
    CHECK(sprite->pixelsPerUnit == Approx(100.0f));
    CHECK(sprite->pivot.x == Approx(0.5f));

    const auto texture = devex::asset::decodeTexture(result->artifacts[0].bytes);
    REQUIRE(texture.has_value());
    const auto level = devex::asset::decodeTextureLevel(*texture, 0);
    REQUIRE(level.has_value());
    // A clear neighbour is red, still clear.
    const std::uint8_t* const neighbour = &level->rgba[(1 * 4 + 2) * 4];
    CHECK(neighbour[0] == 255);
    CHECK(neighbour[3] == 0);

    // Unknown modes are refused.
    devex::asset::ImportContext wrong = contextFor(image.path, {{"sprite_mode", TextValue(std::string("strips"))}});
    CHECK_FALSE(devex::asset::importTextureFile(wrong).has_value());
}

TEST_CASE("Sorting layers are ranked around Default and kept by the project", "[asset][project][sprite]")
{
    devex::asset::SortingSettings sorting{.layers = {"Background", "Default", "Characters", "Foreground"}};
    CHECK(sorting.rank("Background") == -1);
    CHECK(sorting.rank("Default") == 0);
    CHECK(sorting.rank("Foreground") == 2);
    CHECK(sorting.rank("") == 0);
    CHECK(sorting.rank("Unknown") == 0);

    devex::asset::Project project;
    project.name = "Layers";
    project.sorting = sorting;
    const std::string text = devex::asset::writeProjectText(project);
    CHECK(text.find("[sorting_layer name=\"Background\"]") != std::string::npos);
    const auto parsed = devex::asset::parseProject(text, std::filesystem::path("C:/Layers/Layers.dvxproj"));
    REQUIRE(parsed.has_value());
    CHECK(parsed->sorting == sorting);

    // Default is added when a file forgets it, in front of the others; the default settings are
    // not written.
    const auto forgotten = devex::asset::parseProject("[project format=1 name=\"A\"]\n\n[sorting_layer name=\"Sky\"]\n",
                                                      std::filesystem::path("C:/A/A.dvxproj"));
    REQUIRE(forgotten.has_value());
    CHECK(forgotten->sorting.layers == std::vector<std::string>{"Default", "Sky"});
    project.sorting = {};
    CHECK(devex::asset::writeProjectText(project).find("sorting_layer") == std::string::npos);
}
