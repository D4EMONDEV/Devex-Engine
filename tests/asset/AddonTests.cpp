#include <devex/asset/Addon.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/asset/import/Zip.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

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

TEST_CASE("An addon says the Devex and the addons it needs, which its install checks", "[asset][addon]")
{
    AddonFixture fixture;
    // An addon the project has, at an older version than the one needed.
    const std::filesystem::path base = fixture.root / "base";
    REQUIRE(devex::core::writeTextFile(base / "addon.dvxaddon", "[addon format=1 id=\"test.base\" name=\"Base\" version=\"1.0.0\"]\n"));
    REQUIRE(devex::asset::installAddon(fixture.project, base));
    REQUIRE(devex::core::writeTextFile(fixture.source / "addon.dvxaddon",
                                       "[addon format=1 id=\"test.tools\" name=\"Test Tools\" version=\"1.2.3\" engine=\"0.1.0\" "
                                       "category=\"Tools\" icon=\"icon.png\" dependencies=\"test.base 1.5, test.ui\"]\n"));
    const auto read = devex::asset::readAddon(fixture.source);
    REQUIRE(read);
    CHECK(read->engine == "0.1.0");
    CHECK(read->category == "Tools");
    CHECK(read->icon == "icon.png");
    CHECK(read->dependencies == std::vector<devex::asset::AddonDependency>{{"test.base", "1.5"}, {"test.ui", ""}});

    // What it needs is shown, and the install goes on: there is no catalog to fetch it from yet.
    const auto preview = devex::asset::previewAddon(fixture.project, fixture.source);
    REQUIRE(preview);
    CHECK(preview->conflicts.empty());
    REQUIRE(preview->dependencies.size() == 2);
    CHECK(preview->dependencies[0].installed == "1.0.0");
    CHECK_FALSE(preview->dependencies[0].satisfied);
    CHECK_FALSE(preview->dependencies[1].installed.has_value());
    const auto installed = devex::asset::installAddon(fixture.project, fixture.source);
    REQUIRE(installed);
    const std::vector<std::string> missing = devex::asset::missingAddonDependencies(fixture.project, *installed);
    REQUIRE(missing.size() == 2);
    CHECK(missing[0].find("test.base 1.5 or later") != std::string::npos);
    CHECK(missing[1].find("test.ui") != std::string::npos);

    // Made for a later Devex: refused, with the reason.
    const std::filesystem::path later = fixture.root / "later";
    REQUIRE(devex::core::writeTextFile(later / "addon.dvxaddon",
                                       "[addon format=1 id=\"test.later\" name=\"Later\" version=\"1.0.0\" engine=\"999.0.0\"]\n"));
    const auto refused = devex::asset::previewAddon(fixture.project, later);
    REQUIRE(refused);
    REQUIRE_FALSE(refused->conflicts.empty());
    CHECK(refused->conflicts.front().find("needs Devex 999.0.0 or later") != std::string::npos);
    CHECK_FALSE(devex::asset::installAddon(fixture.project, later));
    // A dependency that is no addon id makes no manifest.
    REQUIRE(devex::core::writeTextFile(later / "addon.dvxaddon",
                                       "[addon format=1 id=\"test.later\" name=\"Later\" version=\"1.0.0\" dependencies=\"Not An Id\"]\n"));
    CHECK_FALSE(devex::asset::readAddon(later));
}

TEST_CASE("ZIPs read as most tools write them, and nothing in them leaves their folder", "[asset][addon][zip]")
{
    AddonFixture fixture;
    const auto text = [](const devex::asset::ZipEntry& entry) {
        return std::string(reinterpret_cast<const char*>(entry.bytes.data()), entry.bytes.size());
    };
    // Stored, as Devex writes them.
    REQUIRE(devex::core::writeTextFile(fixture.root / "folder" / "a.txt", "alpha"));
    REQUIRE(devex::core::writeTextFile(fixture.root / "folder" / "sub" / "b.txt", "beta"));
    REQUIRE(devex::asset::writeZip(fixture.root / "folder", fixture.root / "stored.zip"));
    const auto stored = devex::asset::readZip(fixture.root / "stored.zip");
    REQUIRE(stored);
    REQUIRE(stored->size() == 2);
    CHECK((*stored)[0].path == "a.txt");
    CHECK(text((*stored)[0]) == "alpha");
    CHECK((*stored)[1].path == "sub/b.txt");
    CHECK(text((*stored)[1]) == "beta");

    // Compressed with deflate, with an entry for its folder and an empty file, as Python's zipfile writes it.
    constexpr std::array<std::uint8_t, 339> deflated{0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xAD, 0x1A, 0x71, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x70, 0x61, 0x63, 0x6B, 0x2F, 0x03, 0x00, 0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xAD, 0x1A, 0x71, 0x5D, 0x45, 0x4F, 0x4F, 0xA6, 0x13, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x70, 0x61, 0x63, 0x6B, 0x2F, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x2E, 0x74, 0x78, 0x74, 0xF3, 0x48, 0xCD, 0xC9, 0xC9, 0xD7, 0x51, 0x70, 0x49, 0x2D, 0x4B, 0xAD, 0x50, 0x54, 0xF0, 0xA0, 0x39, 0x0F, 0x00, 0x50, 0x4B, 0x03, 0x04, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xAD, 0x1A, 0x71, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x70, 0x61, 0x63, 0x6B, 0x2F, 0x65, 0x6D, 0x70, 0x74, 0x79, 0x2E, 0x74, 0x78, 0x74, 0x03, 0x00, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xAD, 0x1A, 0x71, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0xFD, 0x41, 0x00, 0x00, 0x00, 0x00, 0x70, 0x61, 0x63, 0x6B, 0x2F, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xAD, 0x1A, 0x71, 0x5D, 0x45, 0x4F, 0x4F, 0xA6, 0x13, 0x00, 0x00, 0x00, 0x70, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x25, 0x00, 0x00, 0x00, 0x70, 0x61, 0x63, 0x6B, 0x2F, 0x68, 0x65, 0x6C, 0x6C, 0x6F, 0x2E, 0x74, 0x78, 0x74, 0x50, 0x4B, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x00, 0x00, 0x08, 0x00, 0xAD, 0x1A, 0x71, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x64, 0x00, 0x00, 0x00, 0x70, 0x61, 0x63, 0x6B, 0x2F, 0x65, 0x6D, 0x70, 0x74, 0x79, 0x2E, 0x74, 0x78, 0x74, 0x50, 0x4B, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x03, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x92, 0x00, 0x00, 0x00, 0x00, 0x00};
    const std::vector<std::byte> zip(reinterpret_cast<const std::byte*>(deflated.data()),
                                     reinterpret_cast<const std::byte*>(deflated.data()) + deflated.size());
    REQUIRE(devex::core::writeFileAtomically(fixture.root / "deflated.zip", zip));
    REQUIRE(devex::asset::extractZip(fixture.root / "deflated.zip", fixture.root / "out"));
    std::string hello;
    for (int i = 0; i < 8; ++i)
    {
        hello += "Hello, Devex! ";
    }
    CHECK(*devex::core::readTextFile(fixture.root / "out" / "pack" / "hello.txt") == hello);
    CHECK(std::filesystem::is_regular_file(fixture.root / "out" / "pack" / "empty.txt"));

    // A byte changed in what is compressed is seen.
    std::vector<std::byte> damaged = zip;
    damaged[67 + 14 + 5] ^= std::byte{0x5A};
    REQUIRE(devex::core::writeFileAtomically(fixture.root / "damaged.zip", damaged));
    CHECK_FALSE(devex::asset::readZip(fixture.root / "damaged.zip"));

    // A path that climbs out of the folder is refused.
    const devex::core::Result<std::vector<std::byte>> bytes = devex::core::readBinaryFile(fixture.root / "stored.zip");
    REQUIRE(bytes);
    std::string climbing(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    for (std::size_t at = climbing.find("a.txt"); at != std::string::npos; at = climbing.find("a.txt", at + 5))
    {
        climbing.replace(at, 5, "../ab");
    }
    REQUIRE(devex::core::writeTextFile(fixture.root / "climbing.zip", climbing));
    const auto refused = devex::asset::readZip(fixture.root / "climbing.zip");
    REQUIRE_FALSE(refused);
    CHECK(refused.error().message.find("leaves its folder") != std::string::npos);
    CHECK_FALSE(devex::asset::readZip(fixture.root / "folder" / "a.txt"));
}
