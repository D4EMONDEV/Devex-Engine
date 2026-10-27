#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/DesktopEntry.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>

TEST_CASE("Desktop entries start their program and name its windows", "[platform][desktop]")
{
    // What an export gives a game to install: found in PATH, with an icon of the theme.
    const devex::platform::DesktopEntry game{.name = "Space $hip", .icon = "Space $hip", .categories = "Game;"};
    CHECK(devex::platform::desktopEntryText(game, "Space $hip") ==
          "[Desktop Entry]\nType=Application\nName=Space $hip\nExec=\"Space \\\\$hip\"\nIcon=Space $hip\n"
          "Terminal=false\nCategories=Game;\nStartupWMClass=Space $hip\n");

    // Quotes keep the spaces of a path, and backslashes escape what they leave special, escaped
    // again as any value; a percent sign is doubled.
    const devex::platform::DesktopEntry editor{.name = "Devex", .opensFiles = true};
    const std::string text = devex::platform::desktopEntryText(editor, "/opt/My \"Games\"/100%/devex-editor");
    CHECK(text.find("\nExec=\"/opt/My \\\\\"Games\\\\\"/100%%/devex-editor\" %f\n") != std::string::npos);
    CHECK(text.find("Icon=") == std::string::npos);
    CHECK(text.find("\nStartupWMClass=devex-editor\n") != std::string::npos);
}

#ifdef __linux__
TEST_CASE("A program is listed among the applications of the desktop", "[platform][desktop]")
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "devex-desktop-entry";
    std::error_code error;
    std::filesystem::remove_all(folder, error);
    const char* const previous = std::getenv("XDG_DATA_HOME");
    const std::string saved = previous != nullptr ? previous : "";
    REQUIRE(setenv("XDG_DATA_HOME", devex::core::toUtf8(folder).c_str(), 1) == 0);

    const devex::platform::DesktopEntry entry{
        .name = "Devex Tests",
        .comment = "Checks the engine",
        .icon = "/opt/devex/bin/resources/icons/devex.png",
        .categories = "Development;",
        .opensFiles = true,
    };
    REQUIRE(devex::platform::registerDesktopEntry(entry).has_value());

    // Named after the executable, as SDL names the windows of the program for the desktop.
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe");
    const std::string name = devex::core::toUtf8(executable.filename());
    const std::filesystem::path file = folder / "applications" / devex::core::pathFromUtf8(name + ".desktop");
    const devex::core::Result<std::string> text = devex::core::readTextFile(file);
    REQUIRE(text.has_value());
    CHECK(text->starts_with("[Desktop Entry]\nType=Application\n"));
    CHECK(text->find("\nName=Devex Tests\n") != std::string::npos);
    CHECK(text->find("\nComment=Checks the engine\n") != std::string::npos);
    CHECK(text->find("\nExec=\"" + devex::core::toUtf8(executable) + "\" %f\n") != std::string::npos);
    CHECK(text->find("\nIcon=/opt/devex/bin/resources/icons/devex.png\n") != std::string::npos);
    CHECK(text->find("\nCategories=Development;\n") != std::string::npos);
    CHECK(text->find("\nStartupWMClass=" + name + "\n") != std::string::npos);

    // The same entry is not written again.
    const std::filesystem::file_time_type written = std::filesystem::last_write_time(file);
    REQUIRE(devex::platform::registerDesktopEntry(entry).has_value());
    CHECK(std::filesystem::last_write_time(file) == written);

    if (saved.empty())
    {
        unsetenv("XDG_DATA_HOME");
    }
    else
    {
        setenv("XDG_DATA_HOME", saved.c_str(), 1);
    }
    std::filesystem::remove_all(folder, error);
}
#endif
