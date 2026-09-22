#include <devex/asset/Artifact.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/asset/import/ThemeFile.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <vector>

using devex::asset::AssetId;
using devex::asset::AssetType;

TEST_CASE("Meshes with submeshes survive encoding", "[asset][artifact]")
{
    devex::asset::MeshData mesh = devex::asset::makeCube();
    const AssetId material = AssetId::generate();
    mesh.submeshes = {{.firstIndex = 0, .indexCount = 12, .material = material},
                      {.firstIndex = 12, .indexCount = 24}};

    const std::vector<std::byte> bytes = devex::asset::encodeMesh(mesh);
    CHECK(devex::asset::artifactType(bytes) == AssetType::Mesh);

    const auto decoded = devex::asset::decodeMesh(bytes);
    REQUIRE(decoded.has_value());
    CHECK(decoded->vertices.size() == mesh.vertices.size());
    CHECK(decoded->vertices[5].uv == mesh.vertices[5].uv);
    CHECK(decoded->indices == mesh.indices);
    REQUIRE(decoded->submeshes.size() == 2);
    CHECK(decoded->submeshes[0].material == material);
    CHECK(decoded->submeshes[1].indexCount == 24);
}

TEST_CASE("Textures, materials and models survive encoding", "[asset][artifact]")
{
    devex::asset::TextureData texture{.format = devex::asset::TextureFormat::Bc7Srgb};
    texture.mips.push_back({.width = 6, .height = 5, .bytes = std::vector<std::byte>(4 * 16)});
    texture.mips.push_back({.width = 3, .height = 2, .bytes = std::vector<std::byte>(16)});
    const auto decodedTexture = devex::asset::decodeTexture(devex::asset::encodeTexture(texture));
    REQUIRE(decodedTexture.has_value());
    CHECK(decodedTexture->format == texture.format);
    CHECK(decodedTexture->mips.size() == 2);

    const devex::asset::MaterialData material{
        .baseColorFactor = {0.5f, 0.25f, 1.0f, 0.75f},
        .baseColorTexture = AssetId::generate(),
        .normalTexture = AssetId::generate(),
        .emissiveFactor = {1.0f, 0.5f, 0.0f},
        .alphaMode = devex::asset::AlphaMode::Mask,
        .doubleSided = true,
    };
    CHECK(devex::asset::decodeMaterial(devex::asset::encodeMaterial(material)) == material);

    devex::asset::ModelData model;
    model.nodes.push_back({.name = "Root"});
    model.nodes.push_back({
        .name = "Child",
        .parent = 0,
        .translation = {1.0f, 2.0f, 3.0f},
        .rotation = devex::math::angleAxis(0.5f, devex::math::Vec3{0.0f, 1.0f, 0.0f}),
        .mesh = AssetId::generate(),
    });
    const auto decodedModel = devex::asset::decodeModel(devex::asset::encodeModel(model));
    REQUIRE(decodedModel.has_value());
    REQUIRE(decodedModel->nodes.size() == 2);
    CHECK(decodedModel->nodes[1].name == "Child");
    CHECK(decodedModel->nodes[1].parent == 0);
    CHECK(decodedModel->nodes[1].rotation == model.nodes[1].rotation);
    CHECK(decodedModel->nodes[1].mesh == model.nodes[1].mesh);
}

TEST_CASE("Corrupt or mismatched cooked data is rejected", "[asset][artifact]")
{
    std::vector<std::byte> bytes = devex::asset::encodeMesh(devex::asset::makePlane());

    // Another type.
    CHECK(devex::asset::decodeTexture(bytes).error().code == devex::core::ErrorCode::Parse);

    // Truncated data.
    const std::vector<std::byte> truncated(bytes.begin(),
                                           bytes.begin() + static_cast<std::ptrdiff_t>(bytes.size() / 2));
    CHECK_FALSE(devex::asset::decodeMesh(truncated).has_value());

    // An older layout version: the version follows the magic and the type.
    bytes[8] = std::byte{0};
    CHECK(devex::asset::decodeMesh(bytes).error().code == devex::core::ErrorCode::Unsupported);

    // Not an asset at all.
    const std::vector<std::byte> text(2, std::byte{0x68});
    CHECK_FALSE(devex::asset::artifactType(text).has_value());
}

TEST_CASE("A theme is read from its file and survives encoding", "[asset][artifact][theme]")
{
    const devex::core::Result<devex::asset::ThemeData> theme =
        devex::asset::parseThemeFile("[theme format=1]\n"
                                     "\n"
                                     "[style name=\"panel\" component=\"UiImage\"]\n"
                                     "color = vec4(0.1, 0.1, 0.12, 0.9)\n"
                                     "corner_radius = 12\n"
                                     "\n"
                                     "[style name=\"panel\" component=\"UiText\"]\n"
                                     "size = 20\n"
                                     "\n"
                                     "[style name=\"title\" component=\"UiText\"]\n"
                                     "size = 34\n");
    REQUIRE(theme.has_value());
    REQUIRE(theme->styles.size() == 2);
    REQUIRE(theme->find("panel") != nullptr);
    // The two sections of the same name are one style.
    REQUIRE(theme->find("panel")->values.size() == 3);
    CHECK(theme->find("panel")->values[0].component == "UiImage");
    CHECK(theme->find("panel")->values[0].field == "color");
    CHECK(theme->find("panel")->values[1].value == "12");
    CHECK(theme->find("panel")->values[2].component == "UiText");
    CHECK(theme->find("nothing") == nullptr);

    const std::vector<std::byte> bytes = devex::asset::encodeTheme(*theme);
    const devex::core::Result<devex::asset::ThemeData> read = devex::asset::decodeTheme(bytes);
    REQUIRE(read.has_value());
    REQUIRE(read->styles.size() == 2);
    CHECK(read->styles[1].name == "title");
    CHECK(read->find("panel")->values[0].value == theme->find("panel")->values[0].value);

    // What was read writes back to a file that reads the same.
    const devex::core::Result<devex::asset::ThemeData> again =
        devex::asset::parseThemeFile(devex::asset::writeThemeFile(*read));
    REQUIRE(again.has_value());
    REQUIRE(again->styles.size() == 2);
    CHECK(again->find("title")->values[0].field == "size");
    CHECK(again->find("panel")->values.size() == 3);
}

TEST_CASE("A theme that names no field or no style is refused", "[asset][artifact][theme]")
{
    // Without the header the file is not a theme at all.
    CHECK_FALSE(devex::asset::parseThemeFile("[style name=\"a\" component=\"UiText\"]\n")
                    .has_value());
    CHECK_FALSE(devex::asset::parseThemeFile("[theme format=1]\n"
                                             "[style component=\"UiText\"]\nsize = 1\n")
                    .has_value());
    // A style that names no component does not know what to write into.
    CHECK_FALSE(devex::asset::parseThemeFile("[theme format=1]\n[style name=\"a\"]\nsize = 1\n")
                    .has_value());
    // The same field twice would make the look of an element a matter of order.
    CHECK_FALSE(devex::asset::parseThemeFile("[theme format=1]\n"
                                             "[style name=\"a\" component=\"UiText\"]\nsize = 1\n"
                                             "[style name=\"a\" component=\"UiText\"]\nsize = 2\n")
                    .has_value());
    CHECK_FALSE(devex::asset::parseThemeFile("[theme format=9]\n").has_value());
}
