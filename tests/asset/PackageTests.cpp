#include <devex/asset/Artifact.hpp>
#include <devex/asset/Package.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

using devex::asset::AssetId;
using devex::asset::AssetInfo;
using devex::asset::AssetType;
using devex::asset::PackageReader;
using devex::asset::PackageWriter;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() / ("devex-package-" + devex::core::Uuid::generate().toString()))
    {
        std::filesystem::create_directories(path);
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};

[[nodiscard]] AssetInfo info(AssetType type, std::string name)
{
    const AssetId id = AssetId::generate();
    return {.id = id, .type = type, .name = std::move(name), .source = id};
}

[[nodiscard]] std::vector<std::byte> noise(std::size_t size)
{
    std::vector<std::byte> bytes(size);
    std::uint32_t state = 0x12345678;
    for (std::byte& byte : bytes)
    {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::byte>(state >> 24);
    }
    return bytes;
}

} // namespace

TEST_CASE("Packages keep assets, settings and icon", "[asset][package]")
{
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path / "Game.dvxpak";

    devex::asset::Project project{.name = "Packed game", .startupScene = "res://assets/scenes/main.dvxscene"};
    project.window.width = 1600;
    project.window.fullscreen = true;
    project.physics.gravity = {0.0f, -3.0f, 0.0f};

    const std::string sceneText = "[scene format=1]\n\n[entity uuid=\"6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23\" name=\"A\"]\n";
    const AssetInfo scene = info(AssetType::Scene, "main");
    // Repetitive data compresses, noise does not, and small data is not worth trying.
    const AssetInfo texture = info(AssetType::Texture, "flat");
    const std::vector<std::byte> flat(100000, std::byte{42});
    const AssetInfo mesh = info(AssetType::Mesh, "noisy");
    const std::vector<std::byte> noisy = noise(5000);
    AssetInfo material = info(AssetType::Material, "tiny");
    material.source = scene.id;
    const std::vector<std::byte> tiny{std::byte{1}, std::byte{2}};

    {
        devex::core::Result<PackageWriter> writer = PackageWriter::create(path);
        REQUIRE(writer.has_value());
        writer->setProject(project);
        writer->setIcon({.width = 2, .height = 1, .rgba = {255, 0, 0, 255, 0, 0, 255, 128}});
        REQUIRE(writer->add(scene, "res://assets/scenes/main.dvxscene", devex::asset::encodeScene(sceneText)));
        REQUIRE(writer->add(texture, "res://assets/textures/flat.png", flat));
        REQUIRE(writer->add(mesh, "", noisy));
        REQUIRE(writer->add(material, "", tiny));
        CHECK(writer->add(material, "", tiny).error().code == devex::core::ErrorCode::AlreadyExists);
        const devex::core::Result<devex::asset::PackageStatistics> statistics = writer->finish();
        REQUIRE(statistics.has_value());
        CHECK(statistics->assets == 4);
        CHECK(statistics->storedBytes < statistics->artifactBytes / 4);
        CHECK(statistics->fileBytes == std::filesystem::file_size(path));
    }
    CHECK_FALSE(std::filesystem::exists(directory.path / "Game.dvxpak.tmp"));

    const devex::core::Result<std::unique_ptr<PackageReader>> reader = PackageReader::open(path);
    REQUIRE(reader.has_value());
    const PackageReader& package = **reader;
    CHECK(package.assetCount() == 4);
    CHECK(package.project().name == "Packed game");
    CHECK(std::filesystem::equivalent(package.project().root, directory.path));
    CHECK(package.project().window.width == 1600);
    CHECK(package.project().window.fullscreen);
    CHECK(package.project().physics.gravity.y == -3.0f);

    CHECK(package.findByPath("res://assets/scenes/main.dvxscene") == scene.id);
    CHECK_FALSE(package.findByPath("res://assets/missing.dvxscene").has_value());
    REQUIRE(package.find(material.id) != nullptr);
    CHECK(package.find(material.id)->source == scene.id);
    CHECK(package.find(texture.id)->name == "flat");
    CHECK(package.find(AssetId::generate()) == nullptr);

    CHECK(package.loadArtifact(texture.id) == flat);
    CHECK(package.loadArtifact(mesh.id) == noisy);
    CHECK(package.loadArtifact(material.id) == tiny);
    CHECK(package.sceneText(scene.id) == sceneText);
    CHECK_FALSE(package.sceneText(texture.id).has_value());
    CHECK(package.loadArtifact(AssetId::generate()).error().code == devex::core::ErrorCode::NotFound);

    const std::vector<AssetInfo> textures = package.assets(AssetType::Texture);
    REQUIRE(textures.size() == 1);
    CHECK(textures.front().id == texture.id);
    const std::vector<AssetInfo> all = package.assets();
    REQUIRE(all.size() == 4);
    CHECK(all.front().name == "flat");
    CHECK(all.back().name == "tiny");

    const devex::core::Result<std::optional<devex::asset::PackageIcon>> icon = package.icon();
    REQUIRE(icon.has_value());
    REQUIRE(icon->has_value());
    CHECK((*icon)->width == 2);
    CHECK((*icon)->rgba[7] == 128);
}

TEST_CASE("Damaged or foreign files are not opened as packages", "[asset][package]")
{
    TemporaryDirectory directory;
    const std::filesystem::path path = directory.path / "Game.dvxpak";
    {
        devex::core::Result<PackageWriter> writer = PackageWriter::create(path);
        REQUIRE(writer.has_value());
        writer->setProject({.name = "Game"});
        REQUIRE(writer->add(info(AssetType::Texture, "noise"), "", noise(4000)));
        REQUIRE(writer->finish());
    }
    const devex::core::Result<std::vector<std::byte>> bytes = devex::core::readBinaryFile(path);
    REQUIRE(bytes.has_value());

    // Cut before the end of the index.
    std::vector<std::byte> truncated(bytes->begin(), bytes->end() - 10);
    REQUIRE(devex::core::writeFileAtomically(directory.path / "truncated.dvxpak", truncated));
    CHECK(PackageReader::open(directory.path / "truncated.dvxpak").error().code == devex::core::ErrorCode::Parse);

    REQUIRE(devex::core::writeTextFile(directory.path / "text.dvxpak", "[project format=1 name=\"Not a package\"]\n"));
    CHECK_FALSE(PackageReader::open(directory.path / "text.dvxpak").has_value());
    CHECK(PackageReader::open(directory.path / "missing.dvxpak").error().code == devex::core::ErrorCode::NotFound);

    // A writer dropped before it finishes leaves nothing behind.
    {
        devex::core::Result<PackageWriter> writer = PackageWriter::create(directory.path / "unfinished.dvxpak");
        REQUIRE(writer.has_value());
        REQUIRE(writer->add(info(AssetType::Texture, "noise"), "", noise(100)));
    }
    CHECK_FALSE(std::filesystem::exists(directory.path / "unfinished.dvxpak"));
    CHECK_FALSE(std::filesystem::exists(directory.path / "unfinished.dvxpak.tmp"));
}
