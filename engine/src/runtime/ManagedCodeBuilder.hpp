#pragma once

#include "GameCodeBuilder.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Error.hpp>
#include <devex/platform/Process.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace devex::runtime::detail {

// Builds the C# files of a project into its managed game assembly with the .NET SDK, in the
// background, whenever one of them changes. The project file it needs is generated, so that the
// folder only holds the code the game is written in.
class ManagedCodeBuilder
{
public:
    // What the last build reported, parsed like the C++ one.
    [[nodiscard]] const std::vector<GameCodeBuilder::Diagnostic>& diagnostics() const noexcept
    {
        return m_diagnostics;
    }

    enum class State : std::uint8_t
    {
        Idle,
        Building,
        Succeeded,
        Failed,
    };

    // Where the assembly of the project's C# code is built.
    [[nodiscard]] static std::filesystem::path assemblyPath(const asset::Project& project);
    // C# files the project compiles besides its own, such as the views of its C++ components.
    [[nodiscard]] static std::filesystem::path generatedDirectory(const asset::Project& project);
    // True when the code folder holds at least one .cs file.
    [[nodiscard]] static bool hasCode(const asset::Project& project);
    // Writes a first C# component into the code folder, next to the C++ code when there is some.
    [[nodiscard]] static core::Result<std::filesystem::path> createScript(const asset::Project& project,
                                                                          std::string_view componentName);

    // Where a build goes and the generated C# files it compiles, when not the editor's.
    struct Target
    {
        // bin/ and obj/ go there.
        std::filesystem::path buildDirectory;
        std::filesystem::path generatedDirectory;
    };

    // devexManagedDirectory holds Devex.Managed.dll, which the game code is compiled against. Without
    // a target, the builder is the editor's: it builds into the cache of the project and keeps the
    // project file for code editors.
    ManagedCodeBuilder(asset::Project project, std::filesystem::path devexManagedDirectory,
                       std::optional<Target> target = std::nullopt);

    // The assembly this builder builds.
    [[nodiscard]] std::filesystem::path builtAssembly() const;

    // Checks the sources for changes, starts builds and forwards the output of a running build to
    // the log. Returns true once when a build has just succeeded.
    [[nodiscard]] bool update();
    void requestBuild() noexcept;
    // Builds now and returns once the build is over, as the export does.
    [[nodiscard]] core::Result<void> buildAndWait();

    [[nodiscard]] State state() const noexcept;
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
    [[nodiscard]] core::Result<void> writeProjectFile() const;
    void startBuild();
    void finishBuild(int exitCode);

    asset::Project m_project;
    std::filesystem::path m_managedDirectory;
    Target m_target;
    bool m_editorTarget = true;
    std::optional<platform::Process> m_process;
    State m_state = State::Idle;
    std::string m_message;
    Snapshot m_sources;
    Clock::time_point m_nextCheck{};
    std::optional<Clock::time_point> m_changedAt;
    Clock::time_point m_buildStart{};
    bool m_buildRequested = false;
    std::vector<GameCodeBuilder::Diagnostic> m_diagnostics;
};

} // namespace devex::runtime::detail
