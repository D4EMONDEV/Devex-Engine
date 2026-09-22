#include <devex/asset/Package.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/LogFile.hpp>
#include <devex/core/Path.hpp>
#include <devex/platform/CrashHandler.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/Application.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

// Plays a game: a project during its development, or the package of an exported game. It opens
// the startup scene, with the game code.
class Player final : public devex::runtime::Application
{
public:
    devex::core::Result<void> onStartup() override
    {
        const devex::asset::AssetSource* const source = assetSource();
        if (source == nullptr)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "there is no game to play");
        }
        const devex::asset::Project& settings = source->project();
        const std::optional<devex::asset::AssetId> startup = startupScene(*source);
        if (!startup)
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "{} has no scene to play", settings.name);
        }
        if (devex::core::Result<void> loaded = loadScene(*startup); !loaded)
        {
            return loaded;
        }
        DEVEX_LOG_INFO("Playing {}", settings.name);
        return {};
    }

private:
    // The startup scene of the game, or its first scene.
    [[nodiscard]] static std::optional<devex::asset::AssetId> startupScene(const devex::asset::AssetSource& source)
    {
        if (const std::string& startup = source.project().startupScene; !startup.empty())
        {
            return source.findByPath(startup);
        }
        const std::vector<devex::asset::AssetInfo> scenes = source.assets(devex::asset::AssetType::Scene);
        return scenes.empty() ? std::nullopt : std::optional(scenes.front().id);
    }
};

// Where the log and the reports of a crash go: the cache of a project being played, or a folder of
// the user named after an exported game, which can be written to wherever the game is installed.
[[nodiscard]] std::filesystem::path logDirectory(const devex::runtime::ApplicationConfig& config,
                                                 std::string_view executable)
{
    if (!config.project.empty())
    {
        std::error_code error;
        const std::filesystem::path project = std::filesystem::absolute(config.project, error);
        return project.parent_path() / ".devex" / "logs";
    }
    const std::string game = config.package.empty() ? std::string(executable)
                                                    : devex::core::toUtf8(config.package.stem());
    const devex::core::Result<std::filesystem::path> user = devex::platform::userDataDirectory("", game);
    return user ? *user / "logs" : devex::platform::executableDirectory() / "logs";
}

} // namespace

// devex-player <project.dvxproj>: plays a project.
// devex-player --package <game.dvxpak>: plays an exported game.
// Without arguments, plays the package named like the executable, next to it: Game.exe plays Game.dvxpak.
int main(int argc, char** argv)
{
    devex::runtime::ApplicationConfig config{
        .title = "Devex Player",
        .loadGameCode = true,
        .useProjectWindowSettings = true,
    };
    // Arguments come in the system code page, which std::filesystem::path converts.
    if (argc >= 3 && std::string_view(argv[1]) == "--package")
    {
        config.package = std::filesystem::path(argv[2]);
    }
    else if (argc >= 2 && !std::string_view(argv[1]).starts_with("--"))
    {
        config.project = std::filesystem::path(argv[1]);
    }
    else if (argc == 1)
    {
        std::filesystem::path package = std::filesystem::path(argv[0]).stem();
        package += devex::asset::packageExtension;
        config.package = package;
    }
    else
    {
        std::fputs("usage: devex-player [<project.dvxproj> | --package <game.dvxpak>]\n", stderr);
        return 2;
    }

    // The log is kept from the first line, and a crash adds its report and a minidump beside it.
    const std::filesystem::path logs =
        logDirectory(config, devex::core::toUtf8(std::filesystem::path(argv[0]).stem()));
    const std::string logName = config.project.empty() ? "game.log" : "player.log";
    devex::core::Result<std::unique_ptr<devex::core::LogFile>> log = devex::core::LogFile::open(logs / logName);
    devex::platform::installCrashHandler({.directory = logs, .log = log ? log->get() : nullptr});
    if (!log)
    {
        DEVEX_LOG_WARNING("The log is not kept in a file: {}", log.error());
    }

    // A windowed game has no console to say why it cannot start: the first fatal error is shown.
    std::string fatal;
    const devex::core::LogSinkId fatalSink = devex::core::addLogSink([&fatal](const devex::core::LogRecord& record) {
        if (record.level == devex::core::LogLevel::Fatal && fatal.empty())
        {
            fatal = record.message;
        }
    });
    const int result = devex::runtime::run<Player>(config);
    devex::core::removeLogSink(fatalSink);
    if (result != 0 && !fatal.empty() && devex::platform::isWindowedApplication())
    {
        std::string message = fatal;
        if (log)
        {
            // Written the way the file manager writes it, for a player to find it.
            std::filesystem::path shown = (*log)->path();
            const std::u8string native = shown.make_preferred().u8string();
            message += "\n\nLog: " + std::string(native.begin(), native.end());
        }
        devex::platform::showErrorMessage("The game cannot start", message);
    }
    return result;
}
