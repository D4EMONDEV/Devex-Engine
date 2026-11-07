#include <devex/asset/Artifact.hpp>
#include <devex/asset/Package.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/TextureProcessing.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/GameExport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

using devex::asset::AssetId;
using devex::runtime::EngineBuild;

namespace {

class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() / ("devex-export-" + devex::core::Uuid::generate().toString()))
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

void write(const std::filesystem::path& path, std::string_view text)
{
    REQUIRE(devex::core::writeTextFile(path, text));
}

// A fake engine build: a player and the CMake package of a configuration.
void makeEngineBuild(const std::filesystem::path& directory, std::string_view configuration)
{
#ifdef _WIN32
    write(directory / "bin" / "devex-player.exe", "");
#else
    write(directory / "bin" / "devex-player", "");
#endif
    write(directory / "cmake" / "DevexConfig.cmake", std::format("set(DEVEX_BUILD_TYPE \"{}\")\n", configuration));
}

// Writes a source file with its .dvxmeta, so that its identifier is known before the import.
[[nodiscard]] AssetId writeAsset(const std::filesystem::path& file, std::string_view text, std::string_view importer)
{
    const AssetId id = AssetId::generate();
    write(file, text);
    std::filesystem::path meta = file;
    meta += ".dvxmeta";
    write(meta, std::format("[asset format=1 uuid=\"{}\" importer=\"{}\"]\n", id.uuid, importer));
    return id;
}

} // namespace

TEST_CASE("The licenses of what ships stand beside the programs", "[runtime][export]")
{
    const std::filesystem::path licenses = devex::platform::executableDirectory() / "licenses";
    for (const char* const name : {"README.txt", "devex.txt", "sdl3.txt", "joltphysics.txt", "freetype.txt", "curl.txt", "inter.txt", "lucide.txt", "ufbx.txt",
                                   "slang.txt", "dotnet.txt", "dotnet-third-party-notices.txt"})
    {
        CAPTURE(name);
        std::error_code error;
        CHECK(std::filesystem::file_size(licenses / name, error) > 0);
        CHECK_FALSE(error);
    }
    // What does not ship has no license there.
    CHECK_FALSE(std::filesystem::exists(licenses / "catch2.txt"));
    CHECK_FALSE(std::filesystem::exists(licenses / "vcpkg-cmake.txt"));
}

TEST_CASE("A game takes the licenses of what it carries", "[runtime][export]")
{
    TemporaryDirectory directory;
    const std::filesystem::path bin = directory.path / "bin";
    for (const char* const name : {"devex.txt", "sdl3.txt", "curl.txt", "openssl.txt"})
    {
        REQUIRE(devex::core::writeTextFile(bin / "licenses" / name, "text"));
    }
    const std::filesystem::path game = directory.path / "game" / "licenses";
    REQUIRE(devex::runtime::copyGameLicenses(bin, game));
    CHECK(std::filesystem::exists(game / "devex.txt"));
    CHECK(std::filesystem::exists(game / "sdl3.txt"));
    // The editor alone asks the internet for new versions.
    CHECK_FALSE(std::filesystem::exists(game / "curl.txt"));
    CHECK_FALSE(std::filesystem::exists(game / "openssl.txt"));

    // An engine built before they came with it still exports.
    CHECK(devex::runtime::copyGameLicenses(directory.path / "older" / "bin", directory.path / "older" / "game"));
}

TEST_CASE("A game leaves out the libraries only the editor uses", "[runtime][export]")
{
    CHECK(devex::runtime::goesWithExportedGame("SDL3.dll"));
    CHECK(devex::runtime::goesWithExportedGame("z.dll"));
    CHECK(devex::runtime::goesWithExportedGame("devex-engine.dll"));
    CHECK_FALSE(devex::runtime::goesWithExportedGame("libcurl.dll"));
    CHECK_FALSE(devex::runtime::goesWithExportedGame("libcurl-d.dll"));
    CHECK_FALSE(devex::runtime::goesWithExportedGame("devex_test_game_module.dll"));
    CHECK_FALSE(devex::runtime::goesWithExportedGame("devex_tools_tests.dll"));
}

TEST_CASE("Executables are named after the game", "[runtime][export]")
{
    CHECK(devex::runtime::executableName("My Game") == "My Game");
    CHECK(devex::runtime::executableName("Doom: Eternal?") == "Doom_ Eternal_");
    CHECK(devex::runtime::executableName("  ..  ") == "Game");
    CHECK(devex::runtime::executableName(" Jeu d'\xC3\xA9t\xC3\xA9.") == "Jeu d'\xC3\xA9t\xC3\xA9");
}

TEST_CASE("Engine builds are found next to the running build", "[runtime][export]")
{
    TemporaryDirectory directory;
    const std::filesystem::path builds = directory.path / "out" / "build";
    makeEngineBuild(builds / "x64-debug", "Debug");
    makeEngineBuild(builds / "x64-release", "Release");
    write(builds / "incomplete" / "bin" / "devex-player.exe", "");

    const std::vector<EngineBuild> found = devex::runtime::findEngineBuilds(builds / "x64-release" / "bin" / "");
    REQUIRE(found.size() == 2);
    CHECK(found[0].name == "x64-release");
    CHECK(found[0].configuration == "Release");
    CHECK(found[1].name == "x64-debug");
    CHECK(found[1].cmakeDirectory() == builds / "x64-debug" / "cmake");

    CHECK(devex::runtime::findEngineBuild(found, "debug")->name == "x64-debug");
    CHECK_FALSE(devex::runtime::findEngineBuild(found, "MinSizeRel").has_value());
}

TEST_CASE("Cooked assets name the assets they need", "[runtime][export]")
{
    const AssetId prefab = AssetId::generate();
    const AssetId material = AssetId::generate();
    const std::string scene = std::format(R"([scene format=1]

[entity uuid="6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23" name="Prop"]
prefab = asset("{}")

[override type="MeshRenderer"]
material = asset("{}")
mesh = asset("00000000-0000-0000-0000-000000000001")

[entity uuid="b41e7c02-9d3a-4f6e-8c11-5a2e9b7d0f44" name="Copy"]
prefab = asset("{}")
)",
                                          prefab.uuid, material.uuid, prefab.uuid);
    const devex::core::Result<std::vector<AssetId>> fromScene =
        devex::runtime::assetReferences(devex::asset::AssetType::Scene, devex::asset::encodeScene(scene));
    REQUIRE(fromScene.has_value());
    CHECK(fromScene->size() == 3);
    CHECK(std::ranges::find(*fromScene, prefab) != fromScene->end());
    CHECK(std::ranges::find(*fromScene, material) != fromScene->end());
    CHECK(std::ranges::find(*fromScene, devex::asset::builtin::cubeMesh) != fromScene->end());

    const AssetId texture = AssetId::generate();
    const devex::core::Result<std::vector<AssetId>> fromMaterial = devex::runtime::assetReferences(
        devex::asset::AssetType::Material, devex::asset::encodeMaterial({.normalTexture = texture}));
    REQUIRE(fromMaterial.has_value());
    CHECK(*fromMaterial == std::vector{texture});

    const AssetId mesh = AssetId::generate();
    devex::asset::ModelData model;
    model.nodes.push_back({.name = "Root"});
    model.nodes.push_back({.name = "Body", .parent = 0, .mesh = mesh});
    const devex::core::Result<std::vector<AssetId>> fromModel =
        devex::runtime::assetReferences(devex::asset::AssetType::Model, devex::asset::encodeModel(model));
    REQUIRE(fromModel.has_value());
    CHECK(*fromModel == std::vector{mesh});
}

TEST_CASE("An export packages the scenes and every asset they need", "[runtime][export]")
{
    TemporaryDirectory directory;
    devex::core::Result<devex::asset::Project> created = devex::asset::createProject(directory.path / "project", "Export test");
    REQUIRE(created.has_value());
    const std::filesystem::path assets = created->assetsDirectory();

    devex::asset::Image image{.width = 8, .height = 8};
    image.rgba.assign(8 * 8 * 4, 200);
    const std::vector<std::byte> png = devex::asset::encodePng(image);
    REQUIRE(devex::core::writeFileAtomically(assets / "textures" / "icon.png", png));
    const AssetId icon = AssetId::generate();
    write(assets / "textures" / "icon.png.dvxmeta", std::format("[asset format=1 uuid=\"{}\" importer=\"texture\"]\n", icon.uuid));

    const AssetId used = writeAsset(assets / "materials" / "used.dvxmat",
                                    std::format("[material format=1]\nbase_color_texture = asset(\"{}\")\n", icon.uuid), "material");
    const AssetId unused = writeAsset(assets / "materials" / "unused.dvxmat", "[material format=1]\n", "material");
    const AssetId data = writeAsset(assets / "data" / "extra.dvxmat", "[material format=1]\n", "material");
    const AssetId prefab = writeAsset(assets / "prefabs" / "prop.dvxscene",
                                      "[scene format=1]\n\n[entity uuid=\"8a1f3c5e-7b9d-4e2f-a1c3-5e7b9d1f3a5c\" name=\"Prop\"]\n\n"
                                      "[component type=\"Transform\"]\n",
                                      "scene");
    const AssetId main = writeAsset(
        assets / "scenes" / "main.dvxscene",
        std::format("[scene format=1]\n\n[entity uuid=\"2c4e6a8c-1e3a-4c5e-9a7c-3e5a7c9e1a3c\" name=\"Floor\"]\n\n"
                    "[component type=\"MeshRenderer\"]\nmesh = asset(\"00000000-0000-0000-0000-000000000003\")\n"
                    "material = asset(\"{}\")\n\n"
                    "[entity uuid=\"4e6a8c2e-3a5c-4e7a-8c9e-5a7c9e1a3c5e\" name=\"Prop\"]\nprefab = asset(\"{}\")\n",
                    used.uuid, prefab.uuid),
        "scene");

    devex::asset::Project settings = *created;
    const AssetId global = writeAsset(
        assets / "globals" / "State.dvxscene",
        "[scene format=1]\n[entity uuid=\"45dc3dab-771f-41e8-ab49-0f2982dd811d\" name=\"State\"]\n",
        "scene");
    settings.globals = {{"State", "res://assets/globals/State.dvxscene"},
                        {"Disabled", "res://assets/globals/NotImported.dvxscene", false}};
    settings.startupScene = "res://assets/scenes/main.dvxscene";
    settings.exportSettings.includeFolders = {"res://assets/data"};
    settings.window.icon = icon;
    REQUIRE(devex::asset::saveProject(settings));

    devex::core::JobSystem jobs(2);
    const devex::core::Result<devex::asset::Project> project = devex::asset::loadProject(settings.file);
    REQUIRE(project.has_value());
    devex::core::Result<std::unique_ptr<devex::asset::AssetDatabase>> database =
        devex::asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
    REQUIRE(database.has_value());
    (*database)->waitForImports();
    static_cast<void>((*database)->update());

    const EngineBuild engine{.name = "test", .configuration = "Release", .directory = directory.path / "engine"};
    devex::core::Result<devex::runtime::ExportPlan> plan = devex::runtime::planExport(**database, engine);
    REQUIRE(plan.has_value());
    CHECK(plan->scenes == std::vector{main, global});
    // Named after the system the game is exported for.
    CHECK(plan->output == (created->root / devex::asset::ExportSettings{}.output).lexically_normal());
    REQUIRE(plan->icon.has_value());
    plan->packageOnly = true;
    plan->output = directory.path / "game";

    std::vector<float> fractions;
    const devex::core::Result<devex::runtime::ExportResult> result = devex::runtime::exportGame(
        *plan, [&fractions](const devex::runtime::ExportProgress& progress) { fractions.push_back(progress.fraction); });
    REQUIRE(result.has_value());
    CHECK(result->assets == 6);
    CHECK(result->executable.empty());
    REQUIRE_FALSE(fractions.empty());
    CHECK(fractions.back() == 1.0f);
    CHECK(std::ranges::is_sorted(fractions));
    CHECK(std::filesystem::exists(directory.path / "game" / "devex-export.txt"));

    {
        const devex::core::Result<std::unique_ptr<devex::asset::PackageReader>> package =
            devex::asset::PackageReader::open(directory.path / "game" / "Export test.dvxpak");
        REQUIRE(package.has_value());
        const devex::asset::PackageReader& reader = **package;
        for (const AssetId expected : {main, global, prefab, used, icon, data})
        {
            CHECK(reader.find(expected) != nullptr);
        }
        CHECK(reader.find(unused) == nullptr);
        CHECK(reader.project().name == "Export test");
        CHECK(reader.project().globals == settings.globals);
        CHECK(reader.findByPath(reader.project().startupScene) == main);
        CHECK(reader.project().exportSettings == devex::asset::ExportSettings{});
        const auto packagedIcon = reader.icon();
        REQUIRE(packagedIcon.has_value());
        REQUIRE(packagedIcon->has_value());
        CHECK((*packagedIcon)->width == 256);
    }

    // A previous export is replaced; other folders are left alone.
    CHECK(devex::runtime::exportGame(*plan).has_value());
    write(directory.path / "documents" / "notes.txt", "not a game");
    plan->output = directory.path / "documents";
    CHECK_FALSE(devex::runtime::exportGame(*plan).has_value());
    CHECK(std::filesystem::exists(directory.path / "documents" / "notes.txt"));

    // Exported scenes and folders must exist.
    devex::asset::Project broken = (*database)->project();
    broken.exportSettings.includeFolders = {"res://assets/missing"};
    REQUIRE((*database)->updateProject(broken));
    CHECK_FALSE(devex::runtime::planExport(**database, engine).has_value());
    broken.exportSettings.includeFolders.clear();
    broken.startupScene = "res://assets/scenes/missing.dvxscene";
    REQUIRE((*database)->updateProject(broken));
    CHECK_FALSE(devex::runtime::planExport(**database, engine).has_value());
}

TEST_CASE("The export for Android keeps its settings with the project and its key apart", "[runtime][export][android]")
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("devex-android-" + devex::core::Uuid::generate().toString());
    auto project = devex::asset::createProject(root, "Android test");
    REQUIRE(project);
    // Nothing is written while the settings are the default ones.
    REQUIRE(devex::asset::saveProject(*project));
    CHECK_FALSE(devex::core::readTextFile(project->file)->contains("export_android"));

    project->exportSettings.platform = "android";
    project->exportSettings.android = {.output = "out/phone",
                                       .package = "com.studio.game",
                                       .versionName = "2.1",
                                       .versionCode = 7,
                                       .orientation = "portrait",
                                       .arm64 = false,
                                       .x86_64 = true,
                                       .format = "aab",
                                       .release = true,
                                       .install = true};
    REQUIRE(devex::asset::saveProject(*project));
    const auto loaded = devex::asset::loadProject(project->file);
    REQUIRE(loaded);
    CHECK(loaded->exportSettings == project->exportSettings);

    // The key stays out of the project file and out of version control.
    CHECK(devex::asset::readAndroidSigning(*project) == devex::asset::AndroidSigning{});
    const devex::asset::AndroidSigning signing{.keystore = root / "key \"store\".jks", .alias = "game", .storePassword = "se\"cret", .keyPassword = "other"};
    REQUIRE(devex::asset::writeAndroidSigning(*project, signing));
    CHECK(devex::asset::readAndroidSigning(*project) == signing);
    CHECK_FALSE(devex::core::readTextFile(project->file)->contains("cret"));
    CHECK(*devex::core::readTextFile(project->cacheDirectory() / ".gitignore") == "*\n");

    CHECK(devex::asset::validAndroidPackage("com.studio.game_2"));
    for (const std::string_view wrong : {"game", "com..game", ".com.game", "com.game.", "com.2game", "com.ga me", ""})
    {
        CHECK_FALSE(devex::asset::validAndroidPackage(wrong));
    }
    CHECK(devex::asset::androidPackageFromName("Mon Jeu 2!") == "com.devex.monjeu2");
    CHECK(devex::asset::androidPackageFromName("2D") == "com.devex.game2d");
    CHECK(devex::asset::validAndroidPackage(devex::asset::androidPackageFromName("??")));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

TEST_CASE("The Android builds of the engine are found next to the editor, and tell Gradle about the game", "[runtime][export][android]")
{
    const std::filesystem::path root = std::filesystem::temp_directory_path() / ("devex-android-builds-" + devex::core::Uuid::generate().toString());
    const auto library = [&](std::string_view build, std::string_view abi) {
        return root / build / "android" / "app" / "src" / "main" / "jniLibs" / abi / "libdevex-engine.so";
    };
    REQUIRE(devex::core::writeTextFile(library("android-arm64", "arm64-v8a"), "library"));
    REQUIRE(devex::core::writeTextFile(library("android-x86_64", "x86_64"), "library"));
    REQUIRE(devex::core::writeTextFile(root / "x64-debug" / "bin" / "devex-editor", "editor"));
    auto project = devex::asset::createProject(root / "game", "Mon Jeu");
    REQUIRE(project);

    const std::vector<devex::runtime::AndroidBuild> builds = devex::runtime::findAndroidBuilds(root / "x64-debug" / "bin");
    REQUIRE(builds.size() == 2);
    CHECK(builds[0].abi == "arm64-v8a");
    CHECK(builds[1].abi == "x86_64");
    CHECK(builds[1].engine.cmakeDirectory() == root / "android-x86_64" / "cmake");

    devex::runtime::ExportPlan plan;
    plan.project = *project;
    plan.project.root = root / "game";
    plan.project.exportSettings.android.x86_64 = true;
    plan.project.exportSettings.android.arm64 = false;
    REQUIRE(devex::runtime::prepareAndroidExport(plan, root / "x64-debug" / "bin"));
    REQUIRE(plan.androidBuilds.size() == 1);
    CHECK(plan.androidBuilds[0].abi == "x86_64");
    CHECK(plan.platform == devex::runtime::ExportPlatform::Android);
    CHECK(plan.output == (root / "game" / "export" / "android").lexically_normal());

    std::string text = devex::runtime::androidGradleProperties(plan, false);
    CHECK(text.contains("applicationId=com.devex.monjeu\n"));
    CHECK(text.contains("gameName=Mon Jeu\n"));
    CHECK(text.contains("abis=x86_64\n"));
    CHECK(text.contains("orientation=sensorLandscape\n"));
    CHECK(text.contains("icon=@android:drawable/sym_def_app_icon\n"));
    CHECK_FALSE(text.contains("keystore"));

    // A release needs its key, which then reaches Gradle, with its backslashes kept.
    plan.project.exportSettings.android.release = true;
    CHECK_FALSE(devex::runtime::prepareAndroidExport(plan, root / "x64-debug" / "bin"));
    plan.signing = {.keystore = "key.jks", .alias = "game", .storePassword = "a\\b", .keyPassword = "c"};
    text = devex::runtime::androidGradleProperties(plan, true);
    CHECK(text.contains("storePassword=a\\\\b\n"));
    CHECK(text.contains("keyAlias=game\n"));
    CHECK(text.contains("icon=@mipmap/ic_launcher\n"));

    // Without a build for the chosen architectures, the export says so.
    plan.project.exportSettings.android.release = false;
    plan.project.exportSettings.android.x86_64 = false;
    const auto none = devex::runtime::prepareAndroidExport(plan, root / "x64-debug" / "bin");
    REQUIRE_FALSE(none);
    CHECK(none.error().message.contains("android-arm64"));

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

