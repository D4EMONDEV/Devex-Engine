#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>

using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::asset::ImportContext;
using devex::asset::ImportedArtifact;
using devex::math::Vec3;

namespace {

const std::filesystem::path dataDirectory{DEVEX_TEST_DATA_DIRECTORY};

[[nodiscard]] ImportContext contextFor(const std::filesystem::path& file)
{
    return ImportContext{
        .source = file,
        .mainId = AssetId::generate(),
        .name = "model",
        // Uncompressed textures keep the test fast in debug builds.
        .options = {{"compress_textures", devex::serialization::TextValue(false)}},
    };
}

[[nodiscard]] std::optional<ImportedArtifact> findArtifact(
    const std::vector<ImportedArtifact>& artifacts, AssetType type)
{
    const auto found = std::ranges::find(artifacts, type, &ImportedArtifact::type);
    return found != artifacts.end() ? std::optional(*found) : std::nullopt;
}

} // namespace

TEST_CASE("A glTF file imports as a model whose nodes refer to its meshes", "[asset][gltf]")
{
    ImportContext context = contextFor(dataDirectory / "triangle.gltf");
    const auto result = devex::asset::importGltfFile(context);
    REQUIRE(result.has_value());

    // The model is the main asset and comes first.
    REQUIRE(result->artifacts.size() == 2);
    CHECK(result->artifacts.front().id == context.mainId);
    CHECK(result->artifacts.front().type == AssetType::Model);

    const auto model = devex::asset::decodeModel(result->artifacts.front().bytes);
    REQUIRE(model.has_value());
    REQUIRE(model->nodes.size() == 2);
    CHECK(model->nodes[0].name == "Offset");
    CHECK(model->nodes[0].translation == Vec3{2.0f, 0.0f, 0.0f});
    CHECK(model->nodes[1].parent == 0);
    CHECK(model->nodes[1].translation == Vec3{0.0f, 3.0f, 0.0f});

    const std::optional<ImportedArtifact> meshArtifact = findArtifact(result->artifacts, AssetType::Mesh);
    REQUIRE(meshArtifact.has_value());
    CHECK(model->nodes[1].mesh == meshArtifact->id);
    CHECK(meshArtifact->name == "Triangle");
    const auto mesh = devex::asset::decodeMesh(meshArtifact->bytes);
    REQUIRE(mesh.has_value());
    REQUIRE(mesh->vertices.size() == 3);
    CHECK(mesh->vertices[1].position == Vec3{1.0f, 0.0f, 0.0f});
    // The file has no normals: they are computed from the counter-clockwise winding.
    CHECK(mesh->vertices[0].normal == Vec3{0.0f, 0.0f, 1.0f});

    // Embedded data only: nothing else to watch.
    CHECK(result->dependencies.empty());
    CHECK(context.subAssets.hasNewEntries());
}

TEST_CASE("glTF materials and textures become sub-assets with stable identifiers", "[asset][gltf]")
{
    const std::filesystem::path file = dataDirectory / "textured" / "quad.gltf";
    ImportContext first = contextFor(file);
    const auto result = devex::asset::importGltfFile(first);
    REQUIRE(result.has_value());

    const std::optional<ImportedArtifact> textureArtifact =
        findArtifact(result->artifacts, AssetType::Texture);
    const std::optional<ImportedArtifact> materialArtifact =
        findArtifact(result->artifacts, AssetType::Material);
    const std::optional<ImportedArtifact> meshArtifact = findArtifact(result->artifacts, AssetType::Mesh);
    REQUIRE(textureArtifact.has_value());
    REQUIRE(materialArtifact.has_value());
    REQUIRE(meshArtifact.has_value());
    CHECK(textureArtifact->name == "quad_color.png");
    CHECK(materialArtifact->name == "Painted");

    const auto material = devex::asset::decodeMaterial(materialArtifact->bytes);
    REQUIRE(material.has_value());
    CHECK(material->baseColorTexture == textureArtifact->id);
    CHECK(material->alphaMode == devex::asset::AlphaMode::Mask);
    CHECK(material->doubleSided);

    const auto mesh = devex::asset::decodeMesh(meshArtifact->bytes);
    REQUIRE(mesh.has_value());
    REQUIRE(mesh->submeshes.size() == 1);
    CHECK(mesh->submeshes[0].material == materialArtifact->id);

    const auto texture = devex::asset::decodeTexture(textureArtifact->bytes);
    REQUIRE(texture.has_value());
    CHECK(texture->format == devex::asset::TextureFormat::Rgba8Srgb);
    CHECK(texture->mips.size() == 4);

    // The external image is a dependency.
    REQUIRE(result->dependencies.size() == 1);
    CHECK(result->dependencies[0].filename() == "quad_color.png");

    // Importing again with the .dvxmeta entries keeps every identifier.
    ImportContext second = contextFor(file);
    second.mainId = first.mainId;
    second.subAssets = devex::asset::SubAssetIds(first.subAssets.entries());
    const auto again = devex::asset::importGltfFile(second);
    REQUIRE(again.has_value());
    CHECK_FALSE(second.subAssets.hasNewEntries());
    CHECK(findArtifact(again->artifacts, AssetType::Texture)->id == textureArtifact->id);
    CHECK(findArtifact(again->artifacts, AssetType::Material)->id == materialArtifact->id);
    CHECK(findArtifact(again->artifacts, AssetType::Mesh)->id == meshArtifact->id);
}

TEST_CASE("Importing a missing glTF file reports an error", "[asset][gltf]")
{
    ImportContext context = contextFor(dataDirectory / "missing.gltf");
    const auto result = devex::asset::importGltfFile(context);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == devex::core::ErrorCode::NotFound);
}

TEST_CASE("Importers are found by extension regardless of case", "[asset][importer]")
{
    CHECK(devex::asset::findImporterForExtension(".PNG")->name == "texture");
    CHECK(devex::asset::findImporterForExtension(".glb")->mainType == AssetType::Model);
    CHECK(devex::asset::findImporterForExtension(".dvxmat")->name == "material");
    CHECK(devex::asset::findImporterForExtension(".DvxScene")->mainType == AssetType::Scene);
    CHECK(devex::asset::findImporterForExtension(".txt") == nullptr);
}
