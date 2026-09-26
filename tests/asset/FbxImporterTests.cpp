#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/Importer.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>

using Catch::Approx;
using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::asset::ImportContext;
using devex::asset::ImportedArtifact;
using devex::asset::ModelData;
using devex::math::Vec3;
using devex::math::Vec4;

namespace {

// Made by make_test_files.py, with Blender: Z up, and the axes and units of Blender's exporter.
const std::filesystem::path fbxDirectory = std::filesystem::path{DEVEX_TEST_DATA_DIRECTORY} / "fbx";

[[nodiscard]] ImportContext contextFor(const std::filesystem::path& file, double scale = 1.0)
{
    return ImportContext{
        .source = file,
        .mainId = AssetId::generate(),
        .name = "model",
        // Uncompressed textures keep the test fast in debug builds.
        .options =
            {
                {"compress_textures", devex::serialization::TextValue(false)},
                {"scale", devex::serialization::TextValue(scale)},
            },
    };
}

[[nodiscard]] std::optional<ImportedArtifact> findArtifact(const std::vector<ImportedArtifact>& artifacts,
                                                           AssetType type, std::string_view name = {})
{
    const auto found = std::ranges::find_if(artifacts, [&](const ImportedArtifact& artifact) {
        return artifact.type == type && (name.empty() || artifact.name == name);
    });
    return found != artifacts.end() ? std::optional(*found) : std::nullopt;
}

[[nodiscard]] std::size_t findNode(const ModelData& model, std::string_view name)
{
    const auto found = std::ranges::find(model.nodes, name, &devex::asset::ModelNode::name);
    REQUIRE(found != model.nodes.end());
    return static_cast<std::size_t>(found - model.nodes.begin());
}

// The transform of a node in the space of the model.
[[nodiscard]] devex::math::Mat4 worldOf(const ModelData& model, std::size_t node)
{
    devex::math::Mat4 world{1.0f};
    for (auto index = static_cast<std::int32_t>(node); index >= 0; index = model.nodes[static_cast<std::size_t>(index)].parent)
    {
        const devex::asset::ModelNode& current = model.nodes[static_cast<std::size_t>(index)];
        world = devex::math::composeTrs({current.translation, current.rotation, current.scale}) * world;
    }
    return world;
}

void checkNear(Vec3 actual, Vec3 expected)
{
    CHECK(actual.x == Approx(expected.x).margin(1e-3));
    CHECK(actual.y == Approx(expected.y).margin(1e-3));
    CHECK(actual.z == Approx(expected.z).margin(1e-3));
}

} // namespace

TEST_CASE("An FBX file imports in meters with Y up, whatever its own units and axes", "[asset][fbx]")
{
    ImportContext context = contextFor(fbxDirectory / "crate.fbx");
    const auto result = devex::asset::importFbxFile(context);
    REQUIRE(result.has_value());

    // The model is the main asset and comes first.
    REQUIRE_FALSE(result->artifacts.empty());
    CHECK(result->artifacts.front().id == context.mainId);
    CHECK(result->artifacts.front().type == AssetType::Model);
    const auto model = devex::asset::decodeModel(result->artifacts.front().bytes);
    REQUIRE(model.has_value());
    const std::size_t crate = findNode(*model, "Crate");

    const std::optional<ImportedArtifact> meshArtifact = findArtifact(result->artifacts, AssetType::Mesh);
    REQUIRE(meshArtifact.has_value());
    CHECK(meshArtifact->name == "Crate");
    CHECK(model->nodes[crate].mesh == meshArtifact->id);
    const auto mesh = devex::asset::decodeMesh(meshArtifact->bytes);
    REQUIRE(mesh.has_value());
    // Six faces of four corners each, the corners of a face sharing their normal.
    CHECK(mesh->vertices.size() == 24);
    CHECK(mesh->indices.size() == 36);
    for (const devex::asset::Vertex& vertex : mesh->vertices)
    {
        CHECK(devex::math::length(vertex.normal) == Approx(1.0f).margin(1e-4));
    }

    // The 2 m cube stands 3 m up in Blender, whose vertical is Z.
    const devex::math::Aabb bounds = devex::math::transform(worldOf(*model, crate), mesh->bounds);
    checkNear(bounds.min, Vec3{-1.0f, 2.0f, -1.0f});
    checkNear(bounds.max, Vec3{1.0f, 4.0f, 1.0f});
    // Units and axes go into the geometry and the transforms, not into a scale.
    checkNear(model->nodes[crate].scale, Vec3{1.0f});

    // Embedded images need nothing else.
    CHECK(result->dependencies.empty());
    CHECK(context.subAssets.hasNewEntries());
}

TEST_CASE("The scale option of an FBX file multiplies the size of the model", "[asset][fbx]")
{
    ImportContext context = contextFor(fbxDirectory / "crate.fbx", 2.0);
    const auto result = devex::asset::importFbxFile(context);
    REQUIRE(result.has_value());
    const auto model = devex::asset::decodeModel(result->artifacts.front().bytes);
    const auto mesh = devex::asset::decodeMesh(findArtifact(result->artifacts, AssetType::Mesh)->bytes);
    REQUIRE(model.has_value());
    REQUIRE(mesh.has_value());

    const devex::math::Aabb bounds = devex::math::transform(worldOf(*model, findNode(*model, "Crate")), mesh->bounds);
    checkNear(bounds.min, Vec3{-2.0f, 4.0f, -2.0f});
    checkNear(bounds.max, Vec3{2.0f, 8.0f, 2.0f});

    ImportContext invalid = contextFor(fbxDirectory / "crate.fbx", 0.0);
    const auto refused = devex::asset::importFbxFile(invalid);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == devex::core::ErrorCode::InvalidArgument);
}

TEST_CASE("FBX materials and embedded images become sub-assets with stable identifiers", "[asset][fbx]")
{
    const std::filesystem::path file = fbxDirectory / "crate.fbx";
    ImportContext first = contextFor(file);
    const auto result = devex::asset::importFbxFile(first);
    REQUIRE(result.has_value());

    const std::optional<ImportedArtifact> painted = findArtifact(result->artifacts, AssetType::Material, "Painted");
    const std::optional<ImportedArtifact> checker = findArtifact(result->artifacts, AssetType::Material, "Checker");
    const std::optional<ImportedArtifact> texture = findArtifact(result->artifacts, AssetType::Texture);
    const std::optional<ImportedArtifact> meshArtifact = findArtifact(result->artifacts, AssetType::Mesh);
    REQUIRE(painted.has_value());
    REQUIRE(checker.has_value());
    REQUIRE(texture.has_value());
    REQUIRE(meshArtifact.has_value());
    CHECK(texture->name == "crate_color.png");

    const auto paintedData = devex::asset::decodeMaterial(painted->bytes);
    REQUIRE(paintedData.has_value());
    CHECK(paintedData->baseColorFactor.r == Approx(0.8f).margin(0.01));
    CHECK(paintedData->baseColorFactor.g == Approx(0.2f).margin(0.01));
    CHECK(paintedData->baseColorFactor.b == Approx(0.1f).margin(0.01));
    CHECK(paintedData->baseColorFactor.a == Approx(1.0f));
    CHECK(paintedData->roughnessFactor == Approx(0.4f).margin(0.05));
    CHECK(paintedData->metallicFactor == Approx(0.0f).margin(0.01));
    CHECK(paintedData->alphaMode == devex::asset::AlphaMode::Opaque);
    CHECK_FALSE(paintedData->baseColorTexture.isValid());

    const auto checkerData = devex::asset::decodeMaterial(checker->bytes);
    REQUIRE(checkerData.has_value());
    CHECK(checkerData->baseColorTexture == texture->id);
    // The texture replaces the color it is connected to.
    CHECK(checkerData->baseColorFactor == Vec4{1.0f});

    const auto image = devex::asset::decodeTexture(texture->bytes);
    REQUIRE(image.has_value());
    CHECK(image->format == devex::asset::TextureFormat::Rgba8Srgb);
    CHECK(image->mips.front().width == 4);
    CHECK(image->mips.size() == 3);

    // Half the faces are painted, the others textured.
    const auto mesh = devex::asset::decodeMesh(meshArtifact->bytes);
    REQUIRE(mesh.has_value());
    REQUIRE(mesh->submeshes.size() == 2);
    CHECK(mesh->submeshes[0].material == painted->id);
    CHECK(mesh->submeshes[1].material == checker->id);
    CHECK(mesh->submeshes[0].indexCount == 18);
    CHECK(mesh->submeshes[1].indexCount == 18);

    // Importing again with the .dvxmeta entries keeps every identifier.
    ImportContext second = contextFor(file);
    second.mainId = first.mainId;
    second.subAssets = devex::asset::SubAssetIds(first.subAssets.entries());
    const auto again = devex::asset::importFbxFile(second);
    REQUIRE(again.has_value());
    CHECK_FALSE(second.subAssets.hasNewEntries());
    CHECK(findArtifact(again->artifacts, AssetType::Texture)->id == texture->id);
    CHECK(findArtifact(again->artifacts, AssetType::Material, "Painted")->id == painted->id);
    CHECK(findArtifact(again->artifacts, AssetType::Mesh)->id == meshArtifact->id);
}

TEST_CASE("OBJ files import with their .mtl and the images beside them", "[asset][fbx]")
{
    const std::filesystem::path file = fbxDirectory / "crate.obj";
    ImportContext context = contextFor(file);
    const auto result = devex::asset::importFbxFile(context);
    REQUIRE(result.has_value());

    const auto model = devex::asset::decodeModel(result->artifacts.front().bytes);
    REQUIRE(model.has_value());
    const std::optional<ImportedArtifact> meshArtifact = findArtifact(result->artifacts, AssetType::Mesh);
    REQUIRE(meshArtifact.has_value());
    const auto mesh = devex::asset::decodeMesh(meshArtifact->bytes);
    REQUIRE(mesh.has_value());
    const auto node = std::ranges::find(model->nodes, meshArtifact->id, &devex::asset::ModelNode::mesh);
    REQUIRE(node != model->nodes.end());
    const devex::math::Aabb bounds = devex::math::transform(
        worldOf(*model, static_cast<std::size_t>(node - model->nodes.begin())), mesh->bounds);
    checkNear(bounds.min, Vec3{-1.0f, 2.0f, -1.0f});
    checkNear(bounds.max, Vec3{1.0f, 4.0f, 1.0f});

    const std::optional<ImportedArtifact> painted = findArtifact(result->artifacts, AssetType::Material, "Painted");
    const std::optional<ImportedArtifact> checker = findArtifact(result->artifacts, AssetType::Material, "Checker");
    const std::optional<ImportedArtifact> texture = findArtifact(result->artifacts, AssetType::Texture);
    REQUIRE(painted.has_value());
    REQUIRE(checker.has_value());
    REQUIRE(texture.has_value());
    CHECK(devex::asset::decodeMaterial(painted->bytes)->baseColorFactor.r == Approx(0.8f).margin(0.01));
    CHECK(devex::asset::decodeMaterial(checker->bytes)->baseColorTexture == texture->id);

    // The .mtl and the image import the model again when they change.
    std::vector<std::string> dependencies;
    for (const std::filesystem::path& dependency : result->dependencies)
    {
        dependencies.push_back(dependency.filename().string());
    }
    std::ranges::sort(dependencies);
    CHECK(dependencies == std::vector<std::string>{"crate.mtl", "crate_color.png"});

    const auto found = devex::asset::findFbxDependencies(file);
    REQUIRE(found.has_value());
    CHECK(found->size() == 2);
    const auto none = devex::asset::findFbxDependencies(fbxDirectory / "crate.fbx");
    REQUIRE(none.has_value());
    CHECK(none->empty());
}

TEST_CASE("A skinned FBX file imports its skeleton, its skin and its animations", "[asset][fbx]")
{
    ImportContext context = contextFor(fbxDirectory / "walker.fbx");
    const auto result = devex::asset::importFbxFile(context);
    REQUIRE(result.has_value());
    const auto model = devex::asset::decodeModel(result->artifacts.front().bytes);
    REQUIRE(model.has_value());
    REQUIRE(devex::asset::validate(*model).has_value());

    const std::size_t hip = findNode(*model, "Hip");
    const std::size_t knee = findNode(*model, "Knee");
    const std::size_t leg = findNode(*model, "Leg");
    CHECK(model->nodes[knee].parent == static_cast<std::int32_t>(hip));
    // The hip stands 2 m up and the knee 1 m, in Blender's vertical which is Z.
    checkNear(Vec3{worldOf(*model, hip)[3]}, Vec3{0.0f, 2.0f, 0.0f});
    checkNear(Vec3{worldOf(*model, knee)[3]}, Vec3{0.0f, 1.0f, 0.0f});

    // The scale of 100 Blender gives its objects is baked into their meshes and translations.
    checkNear(model->nodes[leg].scale, Vec3{1.0f});
    checkNear(model->nodes[findNode(*model, "Rig")].scale, Vec3{1.0f});

    REQUIRE(model->skins.size() == 1);
    CHECK(model->nodes[leg].skin == 0);
    std::vector<std::int32_t> joints = model->skins[0].joints;
    std::ranges::sort(joints);
    CHECK(joints == std::vector<std::int32_t>{static_cast<std::int32_t>(hip), static_cast<std::int32_t>(knee)});

    const auto mesh = devex::asset::decodeMesh(findArtifact(result->artifacts, AssetType::Mesh)->bytes);
    REQUIRE(mesh.has_value());
    REQUIRE(devex::asset::isSkinned(*mesh));
    REQUIRE(mesh->inverseBind.size() == 2);

    // At rest, the bones put every vertex where the mesh itself puts it.
    const devex::math::Mat4 legWorld = worldOf(*model, leg);
    for (std::size_t vertex = 0; vertex < mesh->vertices.size(); ++vertex)
    {
        const Vec4 position{mesh->vertices[vertex].position, 1.0f};
        const devex::asset::VertexSkin& skin = mesh->skin[vertex];
        Vec4 skinned{0.0f};
        for (std::size_t slot = 0; slot < 4; ++slot)
        {
            const std::size_t joint = skin.joints[slot];
            const auto node = static_cast<std::size_t>(model->skins[0].joints[joint]);
            skinned += skin.weights[static_cast<int>(slot)] * (worldOf(*model, node) * mesh->inverseBind[joint] * position);
        }
        checkNear(Vec3{skinned}, Vec3{legWorld * position});
    }
    // The foot on the ground, the top at the hip.
    const devex::math::Aabb bounds = devex::math::transform(legWorld, mesh->bounds);
    checkNear(bounds.min, Vec3{-0.2f, 0.0f, -0.2f});
    checkNear(bounds.max, Vec3{0.2f, 2.0f, 0.2f});

    // The knee bends by 45 degrees over the second the animation lasts.
    REQUIRE(model->animations.size() == 1);
    const std::optional<ImportedArtifact> clipArtifact = findArtifact(result->artifacts, AssetType::AnimationClip);
    REQUIRE(clipArtifact.has_value());
    CHECK(clipArtifact->id == model->animations[0]);
    CHECK(clipArtifact->name.find("Walk") != std::string::npos);
    const auto clip = devex::asset::decodeAnimation(clipArtifact->bytes);
    REQUIRE(clip.has_value());
    CHECK(clip->duration == Approx(1.0f).margin(0.01));
    const auto kneeJoint = std::ranges::find(clip->joints, std::string("Knee"));
    REQUIRE(kneeJoint != clip->joints.end());
    const auto rotation = std::ranges::find_if(clip->channels, [&](const devex::asset::AnimationChannel& channel) {
        return channel.path == devex::asset::AnimationPath::Rotation &&
               channel.joint == static_cast<std::uint32_t>(kneeJoint - clip->joints.begin());
    });
    REQUIRE(rotation != clip->channels.end());
    const auto keyAt = [&](std::size_t key) {
        const float* const values = rotation->values.data() + key * 4;
        return devex::math::Quat{values[3], values[0], values[1], values[2]};
    };
    const float cosine = std::abs(devex::math::dot(keyAt(0), keyAt(rotation->times.size() - 1)));
    CHECK(2.0f * std::acos(std::min(cosine, 1.0f)) == Approx(devex::math::radians(45.0f)).margin(1e-3));
}

TEST_CASE("Importing a missing FBX file reports an error", "[asset][fbx]")
{
    ImportContext context = contextFor(fbxDirectory / "missing.fbx");
    const auto result = devex::asset::importFbxFile(context);

    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == devex::core::ErrorCode::NotFound);
}

TEST_CASE("FBX and OBJ files have importers of their own", "[asset][importer]")
{
    CHECK(devex::asset::findImporterForExtension(".FBX")->name == "fbx");
    CHECK(devex::asset::findImporterForExtension(".obj")->name == "obj");
    CHECK(devex::asset::findImporterForExtension(".fbx")->mainType == AssetType::Model);
    CHECK(devex::asset::findImporterForExtension(".fbx")->findDependencies != nullptr);
}
