#pragma once

#include <devex/asset/Project.hpp>
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

    // The library the code of a project builds to, for the configuration of the engine.
    [[nodiscard]] static std::filesystem::path libraryPath(const asset::Project& project);
    [[nodiscard]] static bool hasCode(const asset::Project& project);
    // Writes code/CMakeLists.txt and a first source file with an example component and system.
    [[nodiscard]] static core::Result<void> createCode(const asset::Project& project);

    // devexConfigDirectory holds the DevexConfig.cmake of the engine build.
    GameCodeBuilder(asset::Project project, std::filesystem::path devexConfigDirectory);

    // Checks the sources for changes, starts builds, and forwards the output of a running build to
    // the log. Returns true once when a build has just succeeded.
    [[nodiscard]] bool update();
    void requestBuild() noexcept;

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

    [[nodiscard]] Snapshot snapshotSources() const;
    void startBuild();
    void finishBuild(int exitCode);

    asset::Project m_project;
    std::filesystem::path m_devexConfigDirectory;
    std::optional<platform::Process> m_process;
    State m_state = State::Idle;
    std::string m_message;
    Snapshot m_sources;
    Clock::time_point m_nextCheck{};
    std::optional<Clock::time_point> m_changedAt;
    Clock::time_point m_buildStart{};
    bool m_buildRequested = false;
};

} // namespace devex::runtime::detail
