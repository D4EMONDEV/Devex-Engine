#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Error.hpp>

#include <filesystem>
#include <string>

namespace devex::platform {

// What the desktop shows of a program among its applications.
struct DEVEX_API DesktopEntry
{
    // "Devex Engine".
    std::string name;
    // A line on what the program does, shown with its name.
    std::string comment;
    // The image of the icon, a PNG, or the name of an icon of the theme.
    std::filesystem::path icon;
    // Freedesktop categories, each followed by a semicolon: "Development;IDE;".
    std::string categories;
    // The program opens the file the user opened with it, as the editor opens a project.
    bool opensFiles = false;
};

// The text of a desktop entry (freedesktop.org) that starts `executable`, a path or a name the
// system finds in PATH, and knows the windows of the program by its name, as SDL names them.
[[nodiscard]] DEVEX_API std::string desktopEntryText(const DesktopEntry& entry, const std::filesystem::path& executable);

// Lists the running program among the applications of the desktop, which then shows its name and
// icon in the dock and the window switcher: on Linux they come from there, not from the window. The
// entry is $XDG_DATA_HOME/applications/<executable>.desktop, named as the desktop knows the windows of
// the program, and it starts this executable; it is written only when it changes. Elsewhere the
// executable and its windows carry the icon, and nothing is written.
[[nodiscard]] DEVEX_API core::Result<void> registerDesktopEntry(const DesktopEntry& entry);

} // namespace devex::platform
