#include <devex/asset/Addon.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

namespace
{
struct AddonFixture
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        devex::core::pathFromUtf8("devex addons été " + devex::core::Uuid::generate().toString());
    devex::asset::Project project;
    std::filesystem::path source = root / "package";
    AddonFixture()
    {
        const auto made = devex::asset::createProject(root / "project", "Addon test");
        REQUIRE(made);
        project = *made;
        REQUIRE(devex::core::writeTextFile(
            source / "addon.dvxaddon", "[addon format=1 id=\"test.tools\" name=\"Test Tools\" "
                                       "version=\"1.2.3\" author=\"Devex\" license=\"MIT\"]\n"));
    }
    ~AddonFixture()
    {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

void writeMeta(const std::filesystem::path& asset, devex::asset::AssetId id)
{
    REQUIRE(devex::core::writeTextFile(asset, "[scene format=1]\n"));
    auto meta = asset;
    meta += devex::asset::metaExtension;
    REQUIRE(devex::core::writeTextFile(
        meta, devex::asset::writeMetaFile({.id = id, .importer = "scene"})));
}
} // namespace

TEST_CASE("Addons install content and code together without overwriting a project",
          "[asset][addon]")
{
    AddonFixture fixture;
    REQUIRE(devex::core::writeTextFile(fixture.source / "assets" / "scene.dvxscene",
                                       "[scene format=1]\n"));
    REQUIRE(
        devex::core::writeTextFile(fixture.source / "code" / "Tool.cs", "// a reusable script\n"));
    REQUIRE(
        devex::core::writeTextFile(fixture.source / "code" / "obj" / "Generated.cs", "excluded"));
    REQUIRE(devex::core::writeTextFile(fixture.source / ".git" / "config", "excluded"));
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "Player.cs", "original"));
    const auto installed = devex::asset::installAddon(fixture.project, fixture.source);
    REQUIRE(installed);
    CHECK(installed->version == "1.2.3");
    CHECK(std::filesystem::exists(installed->directory / "assets/scene.dvxscene"));
    CHECK(std::filesystem::exists(fixture.project.codeDirectory() / "addons/test.tools/Tool.cs"));
    CHECK_FALSE(std::filesystem::exists(installed->directory / "code"));
    CHECK_FALSE(std::filesystem::exists(installed->directory / ".git"));
    CHECK_FALSE(std::filesystem::exists(fixture.project.codeDirectory() / "addons/test.tools/obj"));
    CHECK(*devex::core::readTextFile(fixture.project.codeDirectory() / "Player.cs") == "original");
    CHECK_FALSE(devex::asset::installAddon(fixture.project, fixture.source));
    const auto packages = devex::asset::installedAddons(fixture.project);
    REQUIRE(packages.size() == 1);
    CHECK(packages[0].error.empty());
    CHECK_FALSE(devex::asset::addonEnabled(fixture.project, "test.tools"));
    fixture.project.addons.push_back({"test.tools", true});
    REQUIRE(devex::asset::saveProject(fixture.project));
    const auto loaded = devex::asset::loadProject(fixture.project.file);
    REQUIRE(loaded);
    CHECK(loaded->addons == fixture.project.addons);
    CHECK(devex::asset::addonEnabled(*loaded, "test.tools"));
}

TEST_CASE("Addon packages validate their ids and report broken installed manifests",
          "[asset][addon]")
{
    for (const auto* id :
         {"../escape", "Test", "con", "nul.txt", "com1", "a/b", "a\\b", "a..b", "a.", ""})
        CHECK_FALSE(devex::asset::validAddonId(id));
    CHECK(devex::asset::validAddonId("devex.scene-tools"));
    AddonFixture fixture;
    REQUIRE(
        devex::core::writeTextFile(fixture.source / "addon.dvxaddon",
                                   "[addon format=1 id=\"../escape\" name=\"Bad\" version=\"1\"]"));
    CHECK_FALSE(devex::asset::installAddon(fixture.project, fixture.source));
    CHECK_FALSE(std::filesystem::exists(fixture.project.assetsDirectory() / "addons"));
    REQUIRE(devex::core::writeTextFile(
        fixture.project.assetsDirectory() / "addons/broken/addon.dvxaddon", "broken"));
    const auto packages = devex::asset::installedAddons(fixture.project);
    REQUIRE(packages.size() == 1);
    CHECK_FALSE(packages[0].error.empty());
    fixture.project.addons = {{"test.tools", true}, {"test.tools", false}};
    CHECK_FALSE(devex::asset::saveProject(fixture.project));
}

TEST_CASE("An addon install refuses links and leaves no partial content", "[asset][addon]")
{
    AddonFixture fixture;
    const auto outside = fixture.root / "outside";
    REQUIRE(devex::core::writeTextFile(outside / "external.txt", "untouched"));
    std::error_code error;
    std::filesystem::create_directory_symlink(outside, fixture.source / "linked", error);
    if (error)
    {
        SUCCEED("Creating links is unavailable on this machine");
        return;
    }
    CHECK_FALSE(devex::asset::installAddon(fixture.project, fixture.source));
    CHECK_FALSE(std::filesystem::exists(fixture.project.assetsDirectory() / "addons/test.tools"));
    CHECK_FALSE(std::filesystem::exists(fixture.project.codeDirectory() / "addons/test.tools"));
    CHECK(*devex::core::readTextFile(outside / "external.txt") == "untouched");
}

TEST_CASE("A new addon is made in place, ready to work on and to share", "[asset][addon]")
{
    CHECK(devex::asset::addonIdFromName("Scene Tools") == "scene-tools");
    CHECK(devex::asset::addonIdFromName("  2D: Tools!") == "addon-2d-tools");
    CHECK(devex::asset::addonIdFromName("CON") == "addon-con");
    CHECK(devex::asset::addonIdFromName("---").empty());

    AddonFixture fixture;
    const devex::asset::AddonDraft draft{.name = "Say \"Hi\" \\ Tools",
                                         .author = "Devex",
                                         .description = "It says \"hi\".",
                                         .license = "MIT"};
    CHECK(devex::asset::addonDraftProblem(fixture.project, {.name = " "}));
    CHECK(devex::asset::addonDraftProblem(fixture.project, {.id = "Bad Id", .name = "A"}));
    CHECK(devex::asset::addonDraftProblem(fixture.project, {.name = "A", .version = ""}));
    CHECK_FALSE(devex::asset::addonDraftProblem(fixture.project, draft));

    const auto made = devex::asset::createAddon(fixture.project, draft);
    REQUIRE(made);
    CHECK(made->id == "say-hi-tools");
    CHECK(made->name == draft.name);
    CHECK(made->description == draft.description);
    CHECK(made->license == "MIT");
    CHECK(std::filesystem::exists(made->directory / "README.md"));
    const auto script = devex::core::readTextFile(fixture.project.codeDirectory() / "addons" /
                                                  made->id / devex::asset::addonExampleScript);
    REQUIRE(script);
    CHECK(script->contains("namespace SayHiToolsAddon;"));
    CHECK(script->contains("[EditorCommand(\"say-hi-tools\", \"Create Marker\")]"));
    CHECK(script->contains("\"Say \\\"Hi\\\" \\\\ Tools Marker\""));
    REQUIRE(devex::asset::installedAddons(fixture.project).size() == 1);

    CHECK(devex::asset::addonDraftProblem(fixture.project, draft));
    CHECK_FALSE(devex::asset::createAddon(fixture.project, draft));

    const auto plain =
        devex::asset::createAddon(fixture.project, {.name = "Plain", .exampleCommand = false});
    REQUIRE(plain);
    CHECK_FALSE(std::filesystem::exists(fixture.project.codeDirectory() / "addons/plain"));
}

TEST_CASE("A package's conflicts with the project are found before anything is copied",
          "[asset][addon]")
{
    AddonFixture fixture;
    const auto shared = devex::asset::AssetId::generate();
    writeMeta(fixture.source / "assets" / "one.dvxscene", shared);
    REQUIRE(devex::core::writeTextFile(fixture.source / "code" / "Tool.cs", "// a script\n"));
    auto preview = devex::asset::previewAddon(fixture.project, fixture.source);
    REQUIRE(preview);
    CHECK(preview->scripts == 1);
    CHECK_FALSE(preview->installedVersion);
    CHECK(preview->conflicts.empty());

    // An asset of the project with the same identifier.
    writeMeta(fixture.project.assetsDirectory() / "mine.dvxscene", shared);
    preview = devex::asset::previewAddon(fixture.project, fixture.source);
    REQUIRE(preview);
    REQUIRE(preview->conflicts.size() == 1);
    CHECK(preview->conflicts[0].contains("assets/one.dvxscene"));
    CHECK(preview->conflicts[0].contains("res://assets/mine.dvxscene"));
    CHECK_FALSE(devex::asset::installAddon(fixture.project, fixture.source));
    CHECK_FALSE(std::filesystem::exists(fixture.project.assetsDirectory() / "addons/test.tools"));
    std::filesystem::remove(fixture.project.assetsDirectory() / "mine.dvxscene");
    std::filesystem::remove(fixture.project.assetsDirectory() / "mine.dvxscene.dvxmeta");

    // Two assets of the package with one.
    writeMeta(fixture.source / "assets" / "two.dvxscene", shared);
    preview = devex::asset::previewAddon(fixture.project, fixture.source);
    REQUIRE(preview);
    CHECK(preview->conflicts.size() == 1);
    std::filesystem::remove(fixture.source / "assets" / "two.dvxscene.dvxmeta");

    // A second entry point for the game code.
    REQUIRE(devex::core::writeTextFile(fixture.source / "code" / "Module.cpp",
                                       "DEVEX_GAME_MODULE(Other)\n"));
    preview = devex::asset::previewAddon(fixture.project, fixture.source);
    REQUIRE(preview);
    REQUIRE(preview->conflicts.size() == 1);
    CHECK(preview->conflicts[0].contains("code/Module.cpp"));
    std::filesystem::remove(fixture.source / "code" / "Module.cpp");

    // A folder without a manifest says so.
    const auto none = devex::asset::previewAddon(fixture.project, fixture.root);
    REQUIRE_FALSE(none);
    CHECK(none.error().message.contains("addon.dvxaddon"));
}

TEST_CASE("An addon's update replaces its files and keeps its own identifiers", "[asset][addon]")
{
    AddonFixture fixture;
    writeMeta(fixture.source / "assets" / "scene.dvxscene", devex::asset::AssetId::generate());
    REQUIRE(devex::core::writeTextFile(fixture.source / "assets" / "old.txt", "old"));
    REQUIRE(devex::core::writeTextFile(fixture.source / "code" / "Old.cs", "// old\n"));
    CHECK_FALSE(devex::asset::updateAddon(fixture.project, fixture.source));
    const auto installed = devex::asset::installAddon(fixture.project, fixture.source);
    REQUIRE(installed);
    const auto code = fixture.project.codeDirectory() / "addons/test.tools";

    // A new version, with the same asset and identifier, a new script and no old files.
    REQUIRE(devex::core::writeTextFile(
        fixture.source / "addon.dvxaddon",
        "[addon format=1 id=\"test.tools\" name=\"Test Tools\" version=\"2.0.0\"]\n"));
    std::filesystem::remove(fixture.source / "assets" / "old.txt");
    std::filesystem::remove(fixture.source / "code" / "Old.cs");
    REQUIRE(devex::core::writeTextFile(fixture.source / "code" / "New.cs", "// new\n"));
    const auto preview = devex::asset::previewAddon(fixture.project, fixture.source);
    REQUIRE(preview);
    CHECK(preview->installedVersion == "1.2.3");
    CHECK(preview->addon.version == "2.0.0");
    CHECK(preview->conflicts.empty());
    CHECK_FALSE(devex::asset::installAddon(fixture.project, fixture.source));

    const auto updated = devex::asset::updateAddon(fixture.project, fixture.source);
    REQUIRE(updated);
    CHECK(updated->version == "2.0.0");
    CHECK(std::filesystem::exists(installed->directory / "assets/scene.dvxscene"));
    CHECK_FALSE(std::filesystem::exists(installed->directory / "assets/old.txt"));
    CHECK(std::filesystem::exists(code / "New.cs"));
    CHECK_FALSE(std::filesystem::exists(code / "Old.cs"));
    CHECK(devex::asset::readAddon(installed->directory)->version == "2.0.0");
    CHECK(std::filesystem::is_empty(fixture.project.cacheDirectory() / "addon-install"));

    // The installed copy is not a package of itself, and a package need not keep its code.
    CHECK_FALSE(devex::asset::updateAddon(fixture.project, installed->directory));
    CHECK(std::filesystem::exists(code / "New.cs"));
    std::filesystem::remove_all(fixture.source / "code");
    REQUIRE(devex::asset::updateAddon(fixture.project, fixture.source));
    CHECK_FALSE(std::filesystem::exists(code));

    CHECK(devex::asset::addonResourcePaths(fixture.project, "test.tools") ==
          std::vector<std::string>{"res://assets/addons/test.tools"});
    CHECK(devex::asset::addonResourcePaths(fixture.project, "../escape").empty());
    CHECK(devex::asset::addonResourcePaths(fixture.project, "absent").empty());
}

TEST_CASE("An exported addon installs in another project as it was", "[asset][addon]")
{
    AddonFixture fixture;
    const auto made = devex::asset::createAddon(fixture.project, {.name = "Scene Tools"});
    REQUIRE(made);
    writeMeta(made->directory / "assets" / "scene.dvxscene", devex::asset::AssetId::generate());
    REQUIRE(devex::core::writeTextFile(fixture.project.codeDirectory() / "addons/scene-tools/bin/x",
                                       "a build output"));

    const auto outside = fixture.root / "packages";
    CHECK_FALSE(devex::asset::exportAddon(fixture.project, "absent", outside));
    CHECK_FALSE(devex::asset::exportAddon(fixture.project, "scene-tools",
                                          fixture.project.assetsDirectory() / "packages"));
    const auto exported = devex::asset::exportAddon(fixture.project, "scene-tools", outside);
    REQUIRE(exported);
    CHECK(*exported == outside / "scene-tools");
    CHECK(std::filesystem::exists(*exported / "addon.dvxaddon"));
    CHECK(std::filesystem::exists(*exported / "assets/scene.dvxscene.dvxmeta"));
    CHECK(std::filesystem::exists(*exported / "code" / devex::asset::addonExampleScript));
    CHECK_FALSE(std::filesystem::exists(*exported / "code/bin"));
    CHECK_FALSE(devex::asset::exportAddon(fixture.project, "scene-tools", outside));

    const auto other = devex::asset::createProject(fixture.root / "other", "Other");
    REQUIRE(other);
    const auto installed = devex::asset::installAddon(*other, *exported);
    REQUIRE(installed);
    CHECK(installed->name == "Scene Tools");
    CHECK(std::filesystem::exists(installed->directory / "assets/scene.dvxscene"));
    CHECK(std::filesystem::exists(other->codeDirectory() / "addons/scene-tools" /
                                  devex::asset::addonExampleScript));
}

TEST_CASE("Commands naming an addon the project does not have are found", "[asset][addon]")
{
    AddonFixture fixture;
    REQUIRE(devex::asset::installAddon(fixture.project, fixture.source));
    const std::vector<devex::asset::AddonCommand> commands{
        {.addon = "test.tools", .id = "a", .name = "Known"},
        {.addon = "test.tool", .id = "b", .name = "Misspelled"}};
    const auto unknown = devex::asset::commandsWithoutAddon(fixture.project, commands);
    REQUIRE(unknown.size() == 1);
    CHECK(unknown[0].name == "Misspelled");
}
