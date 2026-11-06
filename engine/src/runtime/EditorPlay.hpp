#pragma once

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/net/Multiplayer.hpp>
#include <devex/platform/Process.hpp>
#include <devex/platform/Window.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::runtime::detail
{

// What makes one game among those Play starts together.
struct DEVEX_API PlayInstance
{
    // From 1, out of how many.
    std::uint32_t index = 1;
    std::uint32_t count = 1;
    // Where the first hosts and the others join; 0 leaves it to the game.
    std::uint16_t port = 0;
    // The place of its window, when it does not take the settings of the project.
    std::optional<platform::ScreenArea> area;
};

// What the editor tells a game it starts, in the session file of its snapshot.
struct DEVEX_API PlaySession
{
    asset::AssetId source;
    bool waitForDebugger = false;
    std::string language;
    PlayInstance instance;
};
[[nodiscard]] DEVEX_API core::Result<PlaySession> readPlaySession(const std::filesystem::path& directory);

// The scene Play starts, by its resource path: the startup scene of the project when it has one, whatever the
// editor shows, and otherwise the scene being edited, which is empty when that scene was never saved.
[[nodiscard]] DEVEX_API std::string scenePlayStarts(const asset::Project& project, std::string edited);

// The part of an area one game of several has: two side by side, three or four in a grid.
[[nodiscard]] DEVEX_API platform::ScreenArea playWindowArea(const platform::ScreenArea& area, std::uint32_t index, std::uint32_t count);

// The editor owns this child, its command pipe and a snapshot in a unique cache directory.
class DEVEX_API PlayProcess
{
  public:
    ~PlayProcess();
    [[nodiscard]] core::Result<void> start(const asset::Project& project, const scene::Scene& scene,
                                           asset::AssetId source,
                                           const std::filesystem::path& player,
                                           bool waitForDebugger = false);
    // The same in two steps: the snapshot of the scene as it is now, and the game, later.
    [[nodiscard]] core::Result<void> prepare(const asset::Project& project, const scene::Scene& scene, asset::AssetId source, bool waitForDebugger,
                                             const PlayInstance& instance);
    [[nodiscard]] core::Result<void> launch(const asset::Project& project, const std::filesystem::path& player);
    void update();
    void pause(bool paused);
    [[nodiscard]] bool pauseRequested() const { return m_requestedPause.value_or(m_status.paused); }
    void step();
    void stop();
    void shutdown();
    [[nodiscard]] const tools::ExternalPlayStatus& status() const { return m_status; }
    [[nodiscard]] const std::filesystem::path& directory() const { return m_directory; }

  private:
    void command(std::string_view command);
    void readLine(const std::string& line);
    void cleanSnapshot();
    std::optional<platform::Process> m_process;
    tools::ExternalPlayStatus m_status;
    std::filesystem::path m_root;
    std::filesystem::path m_directory;
    std::optional<std::chrono::steady_clock::time_point> m_stopTime;
    std::optional<bool> m_requestedPause;
    // What its logs start with, among several games.
    std::string m_label;
    // Its snapshot is waiting for a game that may never start.
    bool m_prepared = false;
};

// The games Play starts together, seen by the editor as one: Pause and Stop are for all, and it runs while
// one does. Each is launched once the one before it is ready, so that the host is there for the others.
// Their status is that of the first still running, whom a debugger waits on.
class DEVEX_API PlayGroup
{
  public:
    PlayGroup() = default;
    ~PlayGroup();
    PlayGroup(const PlayGroup&) = delete;
    PlayGroup& operator=(const PlayGroup&) = delete;

    // With one instance, a single game as usual. With several, they share an area of the screen, connected
    // to one another when it says so.
    [[nodiscard]] core::Result<void> start(const asset::Project& project, const scene::Scene& scene, asset::AssetId source,
                                           const std::filesystem::path& player, bool waitForDebugger, std::uint32_t instances, bool connect,
                                           const platform::ScreenArea& area);
    void update();
    void pause(bool paused);
    [[nodiscard]] bool pauseRequested() const;
    void step();
    void stop();
    void shutdown();
    [[nodiscard]] const tools::ExternalPlayStatus& status() const { return m_status; }

  private:
    [[nodiscard]] PlayProcess* primary() const;
    void refreshStatus();
    std::vector<std::unique_ptr<PlayProcess>> m_processes;
    std::size_t m_launched = 0;
    std::optional<asset::Project> m_project;
    std::filesystem::path m_player;
    std::optional<bool> m_paused;
    tools::ExternalPlayStatus m_status;
};

// The child reads commands without blocking its event loop. Closing the parent closes its pipe.
class DEVEX_API EditorPlayClient
{
  public:
    explicit EditorPlayClient(std::filesystem::path directory);
    ~EditorPlayClient();
    [[nodiscard]] core::Result<scene::Scene> readSnapshot();
    void update();
    void report(bool debuggerAvailable, bool debuggerAttached, bool waiting);
    [[nodiscard]] bool paused() const { return m_paused; }
    [[nodiscard]] bool stopped() const { return m_stopped; }
    [[nodiscard]] bool takeStep() { return std::exchange(m_step, false); }
    [[nodiscard]] bool waitForDebugger() const { return m_waitForDebugger; }
    [[nodiscard]] asset::AssetId source() const { return m_source; }
    [[nodiscard]] const std::string& language() const { return m_language; }
    // Takes its place among the games Play started together.
    void startNetwork(net::Multiplayer& multiplayer) const;

  private:
    std::filesystem::path m_directory;
    PlayInstance m_instance;
    platform::ProcessInput m_input;
    core::LogSinkId m_logSink;
    asset::AssetId m_source;
    std::string m_language;
    std::string m_report;
    bool m_paused = false;
    bool m_stopped = false;
    bool m_step = false;
    bool m_waitForDebugger = false;
};

} // namespace devex::runtime::detail
