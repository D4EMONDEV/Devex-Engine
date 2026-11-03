#pragma once

#include <devex/asset/Project.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Process.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/tools/ToolsOverlay.hpp>

#include <chrono>
#include <optional>
#include <utility>

namespace devex::runtime::detail
{

// The editor owns this child, its command pipe and a snapshot in a unique cache directory.
class DEVEX_API PlayProcess
{
  public:
    ~PlayProcess();
    [[nodiscard]] core::Result<void> start(const asset::Project& project, const scene::Scene& scene,
                                           asset::AssetId source,
                                           const std::filesystem::path& player,
                                           bool waitForDebugger = false);
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

  private:
    std::filesystem::path m_directory;
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
