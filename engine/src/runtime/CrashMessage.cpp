#include <devex/runtime/CrashMessage.hpp>

#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/Platform.hpp>

#include <algorithm>
#include <format>

namespace devex::runtime {

namespace {

constexpr std::string_view crashMark = "*** The process crashed: ";
constexpr std::string_view fatalMark = " fatal ";

[[nodiscard]] std::string_view lineAt(std::string_view text, std::size_t start) noexcept
{
    const std::string_view rest = text.substr(start);
    std::string_view line = rest.substr(0, rest.find('\n'));
    if (line.ends_with('\r'))
    {
        line.remove_suffix(1);
    }
    return line;
}

// What went wrong: a fatal error says it better than the abort that followed it.
[[nodiscard]] std::string_view whatWentWrong(std::string_view log) noexcept
{
    const std::size_t crash = log.rfind(crashMark);
    if (crash == std::string_view::npos)
    {
        return {};
    }
    const std::string_view before = log.substr(0, crash);
    const std::size_t fatal = before.rfind(fatalMark);
    if (fatal != std::string_view::npos)
    {
        // After the level comes the part of the engine, then the message.
        std::string_view line = lineAt(before, fatal + fatalMark.size());
        line.remove_prefix(std::min(line.find_first_not_of(' '), line.size()));
        const std::size_t message = line.find_first_of(' ');
        if (message != std::string_view::npos)
        {
            line.remove_prefix(message);
            line.remove_prefix(std::min(line.find_first_not_of(' '), line.size()));
            return line;
        }
    }
    return lineAt(log, crash + crashMark.size());
}

} // namespace

std::string crashMessageText(std::string_view log, const std::filesystem::path& logFile)
{
    std::string text = "Devex stopped because of an error and had to close.";
    if (const std::string_view what = whatWentWrong(log); !what.empty())
    {
        text += std::format("\n\n{}", what);
    }
    text += "\n\nIf it had copies of your unsaved work, they are offered back when you open the project again.";
    if (!logFile.empty())
    {
        // Written the way the file manager writes it, for the user to find it.
        std::filesystem::path shown = logFile;
        const std::u8string native = shown.make_preferred().u8string();
        text += std::format("\n\nThe report is in this file, which a bug report needs. The next start of Devex keeps it as {}:\n{}",
                            core::toUtf8(shown.filename().replace_extension(".previous" + shown.extension().string())),
                            std::string(native.begin(), native.end()));
    }
    return text;
}

void showCrashMessage(const std::filesystem::path& logFile)
{
    const core::Result<std::string> log = core::readTextFile(logFile);
    const std::string text = crashMessageText(log ? std::string_view(*log) : std::string_view{}, logFile);
    const bool show = platform::askAboutError("Devex has stopped", text, logFile.empty() ? std::string_view{} : std::string_view("Open Log Folder"));
    if (show)
    {
        if (const core::Result<void> opened = platform::Platform::openPath(logFile.parent_path()); !opened)
        {
            DEVEX_LOG_WARNING("{}", opened.error());
        }
    }
}

} // namespace devex::runtime
