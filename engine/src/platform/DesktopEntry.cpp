#include <devex/platform/DesktopEntry.hpp>

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>

#include <cstdlib>
#include <span>
#include <string_view>
#include <system_error>

namespace devex::platform {

#ifdef __linux__
namespace {

// A value on one line, its backslashes and control characters escaped.
[[nodiscard]] std::string escapeValue(std::string_view value)
{
    std::string escaped;
    for (const char character : value)
    {
        switch (character)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }
    return escaped;
}

// A path as an argument of Exec, in quotes, with what the quotes leave special escaped. The value is
// escaped again as any other.
[[nodiscard]] std::string execArgument(std::string_view path)
{
    std::string quoted = "\"";
    for (const char character : path)
    {
        if (character == '%')
        {
            quoted += "%%";
            continue;
        }
        if (character == '"' || character == '`' || character == '$' || character == '\\')
        {
            quoted += '\\';
        }
        quoted += character;
    }
    quoted += '"';
    return quoted;
}

[[nodiscard]] core::Result<std::filesystem::path> applicationsDirectory()
{
    if (const char* const data = std::getenv("XDG_DATA_HOME"); data != nullptr && *data != '\0')
    {
        return std::filesystem::path(data) / "applications";
    }
    if (const char* const home = std::getenv("HOME"); home != nullptr && *home != '\0')
    {
        return std::filesystem::path(home) / ".local" / "share" / "applications";
    }
    return core::makeError(core::ErrorCode::Platform, "no folder for the applications of the user");
}

} // namespace

core::Result<void> registerDesktopEntry(const DesktopEntry& entry)
{
    std::error_code error;
    const std::filesystem::path executable = std::filesystem::read_symlink("/proc/self/exe", error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot find the executable: {}", error.message());
    }
    const core::Result<std::filesystem::path> applications = applicationsDirectory();
    if (!applications)
    {
        return std::unexpected(applications.error());
    }

    // SDL names the windows after the executable, as the app_id of Wayland and the class of X11, and
    // the desktop looks for the entry of that name.
    const std::string name = core::toUtf8(executable.filename());
    std::string text = "[Desktop Entry]\nType=Application\n";
    text += "Name=" + escapeValue(entry.name) + "\n";
    if (!entry.comment.empty())
    {
        text += "Comment=" + escapeValue(entry.comment) + "\n";
    }
    text += "Exec=" + escapeValue(execArgument(core::toUtf8(executable))) + " %f\n";
    if (!entry.icon.empty())
    {
        text += "Icon=" + escapeValue(core::toUtf8(entry.icon.lexically_normal())) + "\n";
    }
    text += "Terminal=false\n";
    if (!entry.categories.empty())
    {
        text += "Categories=" + escapeValue(entry.categories) + "\n";
    }
    text += "StartupWMClass=" + escapeValue(name) + "\n";

    const std::filesystem::path file = *applications / core::pathFromUtf8(name + ".desktop");
    if (const core::Result<std::string> written = core::readTextFile(file); written && *written == text)
    {
        return {};
    }
    std::filesystem::create_directories(*applications, error);
    if (error)
    {
        return core::makeError(core::ErrorCode::Platform, "cannot create {}: {}", core::toUtf8(*applications),
                               error.message());
    }
    // The desktop reads the folder as soon as it changes: the entry appears whole.
    return core::writeFileAtomically(file, std::as_bytes(std::span(text)));
}
#else
core::Result<void> registerDesktopEntry(const DesktopEntry& /*entry*/)
{
    return {};
}
#endif

} // namespace devex::platform
