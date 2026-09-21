#pragma once

#include <devex/asset/Project.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Error.hpp>
#include <devex/platform/Process.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace devex::runtime::detail {

// Builds the code/ folder of a project into its game module with CMake, in the background, whenever
// a file of the folder changes. Visual Studio is found with vswhere when the environment does not
// already have the compiler.
// Asks the compilers for English diagnostics, whatever the language of the system, so that the
// editor reads them the same way everywhere.
void useEnglishDiagnostics();

class GameCodeBuilder
{
public:
    enum class State : std::uint8_t
    {
        Idle,
        Building,
        Succeeded,
        Failed,
    };

    // The library the code of a project builds to, for a configuration of the engine: by default,
    // that of the running engine.
    [[nodiscard]] static std::filesystem::path libraryPath(const asset::Project& project,
                                                           std::string_view configuration = core::buildType());
    // The file name of game modules: Game.dll on Windows.
    [[nodiscard]] static std::filesystem::path libraryFileName();
    [[nodiscard]] static bool hasCode(const asset::Project& project);

    // One error or warning of a build, at the place the compiler named.
    struct Diagnostic
    {
        std::filesystem::path path;
        int line = 1;
        int column = 1;
        std::string message;
        bool error = true;
    };

    // Reads "file(line,col): error C1234: message" and the clang and MSBuild spellings of it.
    // Localized compilers name the severity in their own language, which is also recognized.
    [[nodiscard]] static std::optional<Diagnostic> parseDiagnostic(const std::string& line);
    // Whether a line of build output reports an error (true) or a warning (false).
    [[nodiscard]] static std::optional<bool> severityOf(const std::string& line);

    struct BuildStatus
    {
        bool needsBuild = false;
        // The cached module belongs to another engine, or predates build tracking.
        bool needsUpdate = false;
        std::string message;
    };
    // Reads build metadata only: never loads or executes the project's DLL.
    [[nodiscard]] static BuildStatus buildStatus(const asset::Project& project,
                                                 const std::filesystem::path& devexConfigDirectory,
                                                 std::string_view configuration = core::buildType());
    // Writes code/CMakeLists.txt and a first source file with an example component and system.
    [[nodiscard]] static core::Result<void> createCode(const asset::Project& project);
    // Writes a component in its own file of the code folder, which the module must register.
    [[nodiscard]] static core::Result<std::filesystem::path> createComponent(const asset::Project& project,
                                                                             std::string_view componentName);

    // devexConfigDirectory holds the DevexConfig.cmake of the engine build, whose configuration
    // ("Debug", "Release") the module is built in.
    GameCodeBuilder(asset::Project project, std::filesystem::path devexConfigDirectory,
                    std::string configuration = std::string(core::buildType()));

    // Checks the sources for changes, starts builds, and forwards the output of a running build to
    // the log. Returns true once when a build has just succeeded.
    [[nodiscard]] bool update();
    void requestBuild() noexcept;
    // Rebuilds in a fresh generated folder, without touching sources or the last successful build.
    void requestRebuild() noexcept;
    [[nodiscard]] bool pending() const noexcept;
    // What the last build reported, in the order the compiler printed it.
    [[nodiscard]] const std::vector<Diagnostic>& diagnostics() const noexcept
    {
        return m_diagnostics;
    }
    // Builds now, whether sources changed or not, and returns once the build is over.
    [[nodiscard]] core::Result<void> buildAndWait();

    [[nodiscard]] State state() const noexcept;
    // The first error of the last failed build, or a summary.
    [[nodiscard]] const std::string& message() const noexcept;

private:
    using Clock = std::chrono::steady_clock;

    struct Snapshot
    {
        std::filesystem::file_time_type newest{};
        std::size_t files = 0;

        bool operator==(const Snapshot&) const = default;
    };

    [[nodiscard]] static Snapshot snapshotSources(const asset::Project& project);
    void readBuildLine(const std::string& line);
    void startBuild();
    void finishBuild(int exitCode);

    asset::Project m_project;
    std::filesystem::path m_devexConfigDirectory;
    std::string m_configuration;
    std::filesystem::path m_buildDirectory;
    std::string m_engineStamp;
    std::optional<platform::Process> m_process;
    State m_state = State::Idle;
    std::string m_message;
    Snapshot m_sources;
    Clock::time_point m_nextCheck{};
    std::optional<Clock::time_point> m_changedAt;
    Clock::time_point m_buildStart{};
    bool m_buildRequested = false;
    bool m_freshBuildRequested = false;
    bool m_symbolFailure = false;
    bool m_retriedSymbols = false;
    std::vector<Diagnostic> m_diagnostics;
};

} // namespace devex::runtime::detail
