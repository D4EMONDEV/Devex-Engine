#include <devex/asset/Addon.hpp>
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
