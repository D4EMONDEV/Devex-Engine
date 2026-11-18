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

TEST_CASE("The licenses of what ships stand beside the programs", "[runtime][export][noweb]")
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

TEST_CASE("A game exported for the web takes the page, the web player, its package and a ZIP", "[runtime][export][noweb]")
{
    TemporaryDirectory directory;
    devex::core::Result<devex::asset::Project> created = devex::asset::createProject(directory.path / "project", "Web & Co");
    REQUIRE(created.has_value());
    const AssetId main = writeAsset(created->assetsDirectory() / "scenes" / "main.dvxscene", "[scene format=1]\n", "scene");
    devex::asset::Project settings = *created;
    settings.startupScene = "res://assets/scenes/main.dvxscene";
    REQUIRE(devex::asset::saveProject(settings));

    // The web player next to the editor, as the installer puts it.
    const std::filesystem::path bin = directory.path / "editor" / "bin";
    const std::filesystem::path player = directory.path / "editor" / "platforms" / "web";
    std::filesystem::create_directories(bin);
    write(player / "index.html", "<!doctype html><title>Devex game</title><script>const devexThreads = false;</script>");
    write(player / "devex-player.js", "// the player");
    write(player / "devex-player.wasm", "wasm");
    write(player / "licenses" / "devex.txt", "MIT");
    REQUIRE(devex::runtime::findWebPlayer(bin).has_value());
    CHECK_FALSE(devex::runtime::findWebPlayer(directory.path / "elsewhere" / "bin").has_value());

    devex::core::JobSystem jobs(2);
    const auto plan = [&]() {
        const devex::core::Result<devex::asset::Project> project = devex::asset::loadProject(settings.file);
        REQUIRE(project.has_value());
        devex::core::Result<std::unique_ptr<devex::asset::AssetDatabase>> database =
            devex::asset::AssetDatabase::open(*project, jobs, {.watchFiles = false});
        REQUIRE(database.has_value());
        (*database)->waitForImports();
        static_cast<void>((*database)->update());
        devex::core::Result<devex::runtime::ExportPlan> made =
            devex::runtime::planExport(**database, EngineBuild{.name = "test", .configuration = "Release", .directory = directory.path});
        REQUIRE(made.has_value());
        return std::move(*made);
    };

    // The web draws with Compatibility: a project of Forward+ is told so.
    devex::runtime::ExportPlan forward = plan();
    const devex::core::Result<void> refused = devex::runtime::prepareWebExport(forward, bin);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("Compatibility") != std::string::npos);

    settings.renderingMethod = devex::asset::RenderingMethod::Compatibility;
    REQUIRE(devex::asset::saveProject(settings));
    devex::runtime::ExportPlan web = plan();
    REQUIRE(devex::runtime::prepareWebExport(web, bin).has_value());
    CHECK(web.output == (created->root / "export" / "web").lexically_normal());
    REQUIRE(devex::runtime::prepareWebExport(web, bin, directory.path / "out" / "web").has_value());
    CHECK(web.webArchive == directory.path / "out" / "Web & Co-web.zip");
    const devex::core::Result<devex::runtime::ExportResult> result = devex::runtime::exportGame(web);
    REQUIRE(result.has_value());
    CHECK(result->executable.empty());
    CHECK(result->archive == web.webArchive);
    const std::filesystem::path out = directory.path / "out" / "web";
    for (const char* file : {"index.html", "devex-player.js", "devex-player.wasm", "game.dvxpak", "licenses/devex.txt"})
    {
        INFO(file);
        CHECK(std::filesystem::is_regular_file(out / file));
    }
    // The page is named after the game, written as HTML writes it, and the package plays its startup scene.
    const devex::core::Result<std::string> page = devex::core::readTextFile(out / "index.html");
    REQUIRE(page.has_value());
    CHECK(page->find("<title>Web &amp; Co</title>") != std::string::npos);
    devex::core::Result<std::unique_ptr<devex::asset::PackageReader>> package = devex::asset::PackageReader::open(out / "game.dvxpak");
    REQUIRE(package.has_value());
    CHECK((*package)->findByPath("res://assets/scenes/main.dvxscene") == main);

    // The ZIP holds every file of the folder at its root, stored as it is: its directory ends it.
    const devex::core::Result<std::vector<std::byte>> zip = devex::core::readBinaryFile(web.webArchive);
    REQUIRE(zip.has_value());
    REQUIRE(zip->size() > 22);
    const std::string bytes(reinterpret_cast<const char*>(zip->data()), zip->size());
    CHECK(bytes.starts_with("PK\x03\x04"));
    const std::size_t end = bytes.size() - 22;
    CHECK(bytes.substr(end, 4) == "PK\x05\x06");
    const auto entries = static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[end + 10]) |
                                                    (static_cast<unsigned char>(bytes[end + 11]) << 8));
    std::size_t files = 0;
    for (std::filesystem::recursive_directory_iterator entry(out), last; entry != last; ++entry)
    {
        files += entry->is_regular_file() ? 1 : 0;
    }
    CHECK(entries == files);
    for (const char* name : {"index.html", "game.dvxpak", "licenses/devex.txt", "devex-export.txt"})
    {
        INFO(name);
        CHECK(bytes.find(std::string("PK\x01\x02")) != std::string::npos);
        CHECK(bytes.find(name) != std::string::npos);
    }
    // A stored entry keeps its bytes: the page lies whole in the ZIP.
    CHECK(bytes.find(*page) != std::string::npos);
    CHECK(page->find("const devexThreads = false;") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(out / "devex-player-threads.wasm"));

    // Thread Support takes the player with threads too, which the page plays where the browser allows it.
    settings.exportSettings.webThreads = true;
    REQUIRE(devex::asset::saveProject(settings));
    devex::runtime::ExportPlan threads = plan();
    const devex::core::Result<void> missing = devex::runtime::prepareWebExport(threads, bin, directory.path / "out" / "threads");
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().message.find("with threads") != std::string::npos);
    write(player / "threads" / "devex-player-threads.js", "// the player with threads");
    write(player / "threads" / "devex-player-threads.wasm", "wasm with threads");
    REQUIRE(devex::runtime::prepareWebExport(threads, bin, directory.path / "out" / "threads").has_value());
    REQUIRE(devex::runtime::exportGame(threads).has_value());
    const std::filesystem::path both = directory.path / "out" / "threads";
    for (const char* file : {"devex-player.js", "devex-player.wasm", "devex-player-threads.js", "devex-player-threads.wasm"})
    {
        INFO(file);
        CHECK(std::filesystem::is_regular_file(both / file));
    }
    const devex::core::Result<std::string> threadsPage = devex::core::readTextFile(both / "index.html");
    REQUIRE(threadsPage.has_value());
    CHECK(threadsPage->find("const devexThreads = true;") != std::string::npos);
}

TEST_CASE("The C++ of a game builds for the web with the Emscripten its kit was built with", "[runtime][export][noweb]")
{
    TemporaryDirectory directory;
    const std::filesystem::path player = directory.path / "platforms" / "web";
    // An SDK as emsdk installs it: the toolchain of CMake, and the version with its quotes.
    const auto makeSdk = [&](const std::string& name, std::string_view version) {
        const std::filesystem::path sdk = directory.path / name;
        write(sdk / "upstream" / "emscripten" / "cmake" / "Modules" / "Platform" / "Emscripten.cmake", "");
        write(sdk / "upstream" / "emscripten" / "emscripten-version.txt", std::format("\"{}\"\n", version));
        return sdk;
    };
    const std::filesystem::path older = makeSdk("emsdk-6", "6.0.1");
    const std::filesystem::path needed = makeSdk("emsdk-7", "7.0.0");

    // A web player without its kit cannot build C++.
    CHECK(devex::runtime::findWebTools(player, {needed}).problem().find("no kit") != std::string::npos);

    write(player / "kit" / "cmake" / "DevexConfig.cmake", "set(DEVEX_EMSCRIPTEN_VERSION \"7.0.0\")\n");
    // The SDK of the version of the kit, wherever it comes in the list.
    const devex::runtime::WebTools found = devex::runtime::findWebTools(player, {{}, older, needed});
    CHECK(found.kit == player / "kit");
    CHECK(found.emsdk == needed);
    CHECK(found.neededVersion == "7.0.0");
    CHECK(found.problem().empty());
    // Another version is named, with the commands that install the right one.
    const devex::runtime::WebTools other = devex::runtime::findWebTools(player, {older});
    CHECK(other.emsdk == older);
    CHECK(other.problem().find("6.0.1") != std::string::npos);
    CHECK(other.problem().find("emsdk install 7.0.0 and emsdk activate 7.0.0") != std::string::npos);
    // No SDK: how to install it.
    const std::string missing = devex::runtime::findWebTools(player, {directory.path / "nothing"}).problem();
    CHECK(missing.find("Emscripten 7.0.0, which is not found") != std::string::npos);
    CHECK(missing.find("Editor Settings > Export") != std::string::npos);
}

TEST_CASE("An export packages the scenes and every asset they need", "[runtime][export][noweb]")
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
    REQUIRE(devex::core::writeTextFile(library("android-arm32", "armeabi-v7a"), "library"));
    REQUIRE(devex::core::writeTextFile(library("android-x86_64", "x86_64"), "library"));
    REQUIRE(devex::core::writeTextFile(root / "x64-debug" / "bin" / "devex-editor", "editor"));
    auto project = devex::asset::createProject(root / "game", "Mon Jeu");
    REQUIRE(project);

    const std::vector<devex::runtime::AndroidBuild> builds = devex::runtime::findAndroidBuilds(root / "x64-debug" / "bin");
    REQUIRE(builds.size() == 3);
    CHECK(builds[0].abi == "armeabi-v7a");
    CHECK(builds[1].abi == "arm64-v8a");
    CHECK(builds[2].abi == "x86_64");
    CHECK(builds[2].engine.cmakeDirectory() == root / "android-x86_64" / "cmake");

    // The phones whose Android is 32 bits, beside the others.
    {
        devex::runtime::ExportPlan phones;
        phones.project = *project;
        phones.project.root = root / "game";
        phones.project.exportSettings.android.arm32 = true;
        REQUIRE(devex::runtime::prepareAndroidExport(phones, root / "x64-debug" / "bin"));
        REQUIRE(phones.androidBuilds.size() == 2);
        CHECK(devex::runtime::androidGradleProperties(phones, false).contains("abis=armeabi-v7a,arm64-v8a\n"));
    }

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

