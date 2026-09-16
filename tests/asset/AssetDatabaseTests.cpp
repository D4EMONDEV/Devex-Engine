#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using devex::asset::AssetChange;
using devex::asset::AssetDatabase;
using devex::asset::AssetEvent;
using devex::asset::AssetId;
using devex::asset::AssetType;
using devex::asset::ImportStatus;

namespace {

const std::filesystem::path dataDirectory{DEVEX_TEST_DATA_DIRECTORY};

// A project in a temporary directory, removed at the end of the test.
class TemporaryProject
{
public:
    TemporaryProject()
        : m_root(std::filesystem::temp_directory_path() /
                 ("devex-project-" + devex::core::Uuid::generate().toString()))
    {
        project = *devex::asset::createProject(m_root, "Test");
    }

    ~TemporaryProject()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_root, ignored);
    }

    TemporaryProject(const TemporaryProject&) = delete;
    TemporaryProject& operator=(const TemporaryProject&) = delete;

    [[nodiscard]] std::filesystem::path assets() const
    {
        return project.assetsDirectory();
    }

    void write(const std::string& relative, std::string_view text) const
    {
        REQUIRE(devex::core::writeTextFile(assets() / relative, text));
    }

    void copy(const std::filesystem::path& source, const std::string& relative) const
    {
        std::filesystem::create_directories((assets() / relative).parent_path());
        std::filesystem::copy_file(source, assets() / relative,
                                   std::filesystem::copy_options::overwrite_existing);
    }

    devex::asset::Project project;

private:
    std::filesystem::path m_root;
};

[[nodiscard]] std::vector<AssetEvent> settle(AssetDatabase& database)
{
    database.waitForImports();
    return database.update();
}

[[nodiscard]] bool contains(const std::vector<AssetEvent>& events, AssetId id, AssetChange change)
{
    return std::ranges::any_of(events, [&](const AssetEvent& event) {
        return event.id == id && event.change == change;
    });
}

constexpr std::string_view redMaterial = "[material format=1]\nbase_color = vec4(1, 0, 0, 1)\n";
constexpr std::string_view blueMaterial = "[material format=1]\nbase_color = vec4(0, 0, 1, 1)\n";

} // namespace

TEST_CASE("New files get a .dvxmeta and are imported in the background", "[asset][database]")
{
    TemporaryProject project;
    project.write("materials/red.dvxmat", redMaterial);
    project.copy(dataDirectory / "checker.png", "textures/checker.png");
    project.write("notes.txt", "not an asset");
    devex::core::JobSystem jobs(2);

    auto database = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());
    const std::vector<AssetEvent> events = settle(**database);

    REQUIRE((*database)->sources().size() == 2);
    const auto material = (*database)->findByPath("res://assets/materials/red.dvxmat");
    const auto texture = (*database)->findByPath("res://assets/textures/checker.png");
    REQUIRE(material.has_value());
    REQUIRE(texture.has_value());
    CHECK(contains(events, *material, AssetChange::Imported));
    CHECK(contains(events, *texture, AssetChange::Imported));
    CHECK(std::filesystem::exists(project.assets() / "textures/checker.png.dvxmeta"));
    CHECK_FALSE(std::filesystem::exists(project.assets() / "notes.txt.dvxmeta"));

    CHECK((*database)->find(*texture)->type == AssetType::Texture);
    CHECK((*database)->sourceOf(*texture)->status == ImportStatus::Ready);
    const auto bytes = (*database)->loadArtifact(*material);
    REQUIRE(bytes.has_value());
    CHECK(devex::asset::decodeMaterial(*bytes)->baseColorFactor == devex::math::Vec4{1, 0, 0, 1});
}

TEST_CASE("The cache avoids importing unchanged files again", "[asset][database]")
{
    TemporaryProject project;
    project.write("red.dvxmat", redMaterial);
    devex::core::JobSystem jobs(1);
    AssetId id;
    {
        auto database = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
        REQUIRE(database.has_value());
        static_cast<void>(settle(**database));
        id = *(*database)->findByPath("res://assets/red.dvxmat");
    }

    auto reopened = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
    REQUIRE(reopened.has_value());
    CHECK((*reopened)->pendingImports() == 0);
    // Cached assets are available immediately, with the identifier kept in the .dvxmeta.
    REQUIRE((*reopened)->find(id) != nullptr);
    CHECK((*reopened)->loadArtifact(id).has_value());
}

TEST_CASE("Changed files are imported again under the same identifiers", "[asset][database]")
{
    TemporaryProject project;
    project.write("color.dvxmat", redMaterial);
    devex::core::JobSystem jobs(1);
    auto database = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());
    static_cast<void>(settle(**database));
    const AssetId id = *(*database)->findByPath("res://assets/color.dvxmat");

    // The size differs, so the change is seen even within the file time resolution.
    project.write("color.dvxmat", std::string(blueMaterial) + "# changed\n");
    (*database)->refresh();
    const std::vector<AssetEvent> events = settle(**database);

    CHECK(contains(events, id, AssetChange::Imported));
    CHECK(devex::asset::decodeMaterial(*(*database)->loadArtifact(id))->baseColorFactor ==
          devex::math::Vec4{0, 0, 1, 1});

    // A forced import reports the asset again even without a change.
    REQUIRE((*database)->reimport(id));
    CHECK(contains(settle(**database), id, AssetChange::Imported));
}

TEST_CASE("Deleted files remove their assets but keep their .dvxmeta", "[asset][database]")
{
    TemporaryProject project;
    project.copy(dataDirectory / "triangle.gltf", "models/triangle.gltf");
    devex::core::JobSystem jobs(1);
    auto database = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());
    static_cast<void>(settle(**database));

    const AssetId model = *(*database)->findByPath("res://assets/models/triangle.gltf");
    const std::vector<AssetId> assets = (*database)->sourceOf(model)->assets;
    REQUIRE(assets.size() == 2);
    const std::filesystem::path artifact = (*database)->artifactPath(assets[1]);
    CHECK(std::filesystem::exists(artifact));

    std::filesystem::remove(project.assets() / "models/triangle.gltf");
    (*database)->refresh();
    const std::vector<AssetEvent> events = settle(**database);

    CHECK(contains(events, model, AssetChange::Removed));
    CHECK(contains(events, assets[1], AssetChange::Removed));
    CHECK((*database)->find(assets[1]) == nullptr);
    CHECK_FALSE(std::filesystem::exists(artifact));
    // Restoring the file finds its identifiers again.
    CHECK(std::filesystem::exists(project.assets() / "models/triangle.gltf.dvxmeta"));
    project.copy(dataDirectory / "triangle.gltf", "models/triangle.gltf");
    (*database)->refresh();
    static_cast<void>(settle(**database));
    CHECK((*database)->sourceOf(model)->assets == assets);
}

TEST_CASE("A file copied with its .dvxmeta receives new identifiers", "[asset][database]")
{
    TemporaryProject project;
    project.write("a.dvxmat", redMaterial);
    devex::core::JobSystem jobs(1);
    auto database = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());
    static_cast<void>(settle(**database));
    const AssetId original = *(*database)->findByPath("res://assets/a.dvxmat");

    std::filesystem::copy_file(project.assets() / "a.dvxmat", project.assets() / "b.dvxmat");
    std::filesystem::copy_file(project.assets() / "a.dvxmat.dvxmeta",
                               project.assets() / "b.dvxmat.dvxmeta");
    (*database)->refresh();
    static_cast<void>(settle(**database));

    const auto copy = (*database)->findByPath("res://assets/b.dvxmat");
    REQUIRE(copy.has_value());
    CHECK(*copy != original);
    CHECK((*database)->findByPath("res://assets/a.dvxmat") == original);
}

TEST_CASE("Failed imports keep the previous assets and report the error", "[asset][database]")
{
    TemporaryProject project;
    project.write("m.dvxmat", redMaterial);
    devex::core::JobSystem jobs(1);
    auto database = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());
    static_cast<void>(settle(**database));
    const AssetId id = *(*database)->findByPath("res://assets/m.dvxmat");

    project.write("m.dvxmat", "[material format=1]\nbase_color = 12\n");
    (*database)->refresh();
    static_cast<void>(settle(**database));

    const auto source = (*database)->sourceOf(id);
    REQUIRE(source.has_value());
    CHECK(source->status == ImportStatus::Failed);
    CHECK_FALSE(source->error.empty());
    REQUIRE((*database)->find(id) != nullptr);
    CHECK(devex::asset::decodeMaterial(*(*database)->loadArtifact(id))->baseColorFactor ==
          devex::math::Vec4{1, 0, 0, 1});
}

TEST_CASE("External glTF files are copied into the project with their dependencies",
          "[asset][database]")
{
    TemporaryProject project;
    devex::core::JobSystem jobs(2);
    auto database = AssetDatabase::open(project.project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());

    const auto added = (*database)->addFile(dataDirectory / "textured" / "quad.gltf",
                                            "res://assets/imported");
    REQUIRE(added.has_value());
    CHECK(std::filesystem::exists(project.assets() / "imported/quad.gltf"));
    CHECK(std::filesystem::exists(project.assets() / "imported/quad_color.png"));
    static_cast<void>(settle(**database));

    const auto source = (*database)->sourceOf(*added);
    REQUIRE(source.has_value());
    CHECK(source->status == ImportStatus::Ready);
    CHECK((*database)->find(*added)->type == AssetType::Model);
    // Model, mesh, material and texture.
    CHECK(source->assets.size() == 4);

    CHECK_FALSE((*database)->addFile(dataDirectory / "checker.png", "res://elsewhere").has_value());
}

TEST_CASE("Watched folders import changes without an explicit refresh", "[asset][database]")
{
    TemporaryProject project;
    project.write("watched.dvxmat", redMaterial);
    devex::core::JobSystem jobs(1);
    auto database = AssetDatabase::open(project.project, jobs,
                                        {.watchFiles = true, .settleTime = std::chrono::milliseconds(50)});
    REQUIRE(database.has_value());
    static_cast<void>(settle(**database));
    const AssetId id = *(*database)->findByPath("res://assets/watched.dvxmat");

    project.write("watched.dvxmat", std::string(blueMaterial) + "# edited outside\n");
    bool reimported = false;
    for (int attempt = 0; attempt < 200 && !reimported; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        (*database)->waitForImports();
        reimported = contains((*database)->update(), id, AssetChange::Imported);
    }
    CHECK(reimported);
}
