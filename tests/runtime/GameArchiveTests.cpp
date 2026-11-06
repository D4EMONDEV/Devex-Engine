#include "runtime/GameArchive.hpp"
#include "runtime/GameCodeBuilder.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/runtime/GameExport.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <string>

namespace {

struct Folder
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("devex-archive-" + devex::core::Uuid::generate().toString());

    ~Folder()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

struct Entry
{
    std::string name;
    std::string mode;
    std::string group;
    char type = 0;
    std::string content;
};

// What a tar file says, read the way the format is written, with its checksums checked.
[[nodiscard]] std::map<std::string, Entry> readTar(const std::filesystem::path& file)
{
    const auto bytes = devex::core::readTextFile(file);
    REQUIRE(bytes);
    REQUIRE(bytes->size() % 10240 == 0);
    std::map<std::string, Entry> entries;
    for (std::size_t at = 0; at + 512 <= bytes->size();)
    {
        const std::string header = bytes->substr(at, 512);
        if (header[0] == '\0')
        {
            break;
        }
        unsigned sum = 0;
        for (std::size_t index = 0; index < 512; ++index)
        {
            sum += index >= 148 && index < 156 ? ' ' : static_cast<unsigned char>(header[index]);
        }
        CHECK(sum == std::stoul(header.substr(148, 6), nullptr, 8));
        CHECK(header.substr(257, 5) == "ustar");
        const auto text = [&](std::size_t offset, std::size_t size) {
            const std::string field = header.substr(offset, size);
            return field.substr(0, field.find('\0'));
        };
        const std::string prefix = text(345, 155);
        const std::string name = (prefix.empty() ? "" : prefix + "/") + text(0, 100);
        const std::size_t size = std::stoull(text(124, 12), nullptr, 8);
        entries[name] = {.name = name, .mode = text(100, 8), .group = text(297, 32), .type = header[156], .content = bytes->substr(at + 512, size)};
        at += 512 + (size + 511) / 512 * 512;
    }
    return entries;
}

} // namespace

TEST_CASE("A game for Linux keeps in its archive what is executable", "[runtime][export]")
{
    const Folder folder;
    REQUIRE(devex::core::writeTextFile(folder.path / "game" / "Game", std::string("\x7F") + "ELF program"));
    REQUIRE(devex::core::writeTextFile(folder.path / "game" / "managed" / "libcoreclr.so", std::string("\x7F") + "ELF library"));
    REQUIRE(devex::core::writeTextFile(folder.path / "game" / "game.dvxpak", "a package"));
    const std::string deep(50, 'd');
    const std::string longPath = deep + "/" + deep + "/file.txt";
    REQUIRE(devex::core::writeTextFile(folder.path / "game" / devex::core::pathFromUtf8(longPath), "far away"));

    const std::filesystem::path tar = folder.path / "out" / "Game.tar";
    REQUIRE(devex::runtime::detail::writeTar(folder.path / "game", "Game", tar));
    const auto entries = readTar(tar);
    REQUIRE(entries.contains("Game/"));
    CHECK(entries.at("Game/").type == '5');
    REQUIRE(entries.contains("Game/Game"));
    CHECK(entries.at("Game/Game").mode == "0000755");
    CHECK(entries.at("Game/Game").type == '0');
    CHECK(entries.at("Game/Game").group == "root");
    CHECK(entries.at("Game/managed/libcoreclr.so").mode == "0000755");
    CHECK(entries.at("Game/game.dvxpak").mode == "0000644");
    CHECK(entries.at("Game/game.dvxpak").content == "a package");
    CHECK(entries.at("Game/" + longPath).content == "far away");

    // A path too long for the format is refused, rather than cut.
    const std::string tooLong(120, 'x');
    REQUIRE(devex::core::writeTextFile(folder.path / "again" / devex::core::pathFromUtf8(tooLong), "no"));
    CHECK_FALSE(devex::runtime::detail::writeTar(folder.path / "again", "Game", folder.path / "out" / "Again.tar"));
}

TEST_CASE("A module that registers nothing does not stop an export for the other system", "[runtime][export]")
{
    const Folder folder;
    auto project = devex::asset::createProject(folder.path / "game", "Mon Jeu");
    REQUIRE(project);
    using Builder = devex::runtime::detail::GameCodeBuilder;
    const std::filesystem::path module = project->codeDirectory() / "Game.cpp";

    SECTION("Without C++ at all")
    {
        CHECK(Builder::moduleRegistersNothing(*project));
    }
    SECTION("The module of a new project, and a component nobody registers")
    {
        REQUIRE(devex::core::writeTextFile(module, "// Register here\n#include <devex/runtime/Game.hpp>\n\nDEVEX_GAME_MODULE(game)\n{\n    static_cast<void>(game);\n}\n"));
        REQUIRE(devex::core::writeTextFile(project->codeDirectory() / "Test.cpp", "//     game.component<Test>();\nstruct Test {};\n"));
        CHECK(Builder::moduleRegistersNothing(*project));
    }
    SECTION("A comment does not count as code")
    {
        REQUIRE(devex::core::writeTextFile(module, "DEVEX_GAME_MODULE(game)\n{\n    // game.component<Test>();\n    /* game.system(); */\n}\n"));
        CHECK(Builder::moduleRegistersNothing(*project));
    }
    SECTION("Registering a component does")
    {
        REQUIRE(devex::core::writeTextFile(module, "DEVEX_GAME_MODULE(game)\n{\n    game.component<Test>();\n}\n"));
        CHECK_FALSE(Builder::moduleRegistersNothing(*project));
    }
    SECTION("Calling something does, even if it may do nothing")
    {
        REQUIRE(devex::core::writeTextFile(module, "DEVEX_GAME_MODULE(mine)\n{\n    registerAll(mine);\n}\n"));
        CHECK_FALSE(Builder::moduleRegistersNothing(*project));
    }
}

TEST_CASE("The engine of the other system is looked for beside the editor, in the version of the editor", "[runtime][export]")
{
    const Folder folder;
    const std::filesystem::path bin = folder.path / "install" / "bin";
    if (devex::asset::otherDesktopSystem().empty())
    {
        return;
    }
    const std::string system(devex::asset::otherDesktopSystem());
    const std::filesystem::path engine = folder.path / "install" / "platforms" / (system == "linux" ? "linux-x64" : "windows-x64");
    auto found = devex::runtime::findOtherDesktopEngine(bin);
    REQUIRE_FALSE(found);
    CHECK(found.error().message.find("not next to the editor") != std::string::npos);

    REQUIRE(devex::core::writeTextFile(engine / "bin" / (system == "linux" ? "devex-player" : "devex-player.exe"), "player"));
    const auto config = [&](const std::string& version) {
        return std::format("set(DEVEX_BUILD_TYPE \"Release\")\nset(DEVEX_ENGINE_VERSION \"{}\")\n", version);
    };
    REQUIRE(devex::core::writeTextFile(engine / "cmake" / "DevexConfig.cmake", config("0.0.1")));
    found = devex::runtime::findOtherDesktopEngine(bin);
    REQUIRE_FALSE(found);
    CHECK(found.error().message.find("0.0.1") != std::string::npos);

    REQUIRE(devex::core::writeTextFile(engine / "cmake" / "DevexConfig.cmake", config(std::string(devex::core::version()))));
    found = devex::runtime::findOtherDesktopEngine(bin);
    REQUIRE(found);
    CHECK(found->configuration == "Release");
}
