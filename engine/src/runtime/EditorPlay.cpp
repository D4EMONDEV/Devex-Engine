#include "EditorPlay.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <cstdio>
#include <format>
#include <mutex>
#include <thread>

namespace devex::runtime::detail
{
namespace
{
constexpr std::string_view prefix = "@devex-play ";
using Clock = std::chrono::steady_clock;

void packet(std::string_view value)
{
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    std::fwrite(prefix.data(), 1, prefix.size(), stdout);
    std::fwrite(value.data(), 1, value.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

std::string text(const serialization::TextSection& section, std::string_view key)
{
    const auto* value = section.findAttribute(key);
    const auto* string = value != nullptr ? serialization::asString(*value) : nullptr;
    return string != nullptr ? *string : std::string{};
}

bool flag(const serialization::TextSection& section, std::string_view key)
{
    const auto* value = section.findAttribute(key);
    return value != nullptr && serialization::asBool(*value).value_or(false);
}
} // namespace

PlayProcess::~PlayProcess()
{
    shutdown();
    if (m_prepared)
        cleanSnapshot();
}

platform::ScreenArea playWindowArea(const platform::ScreenArea& area, std::uint32_t index, std::uint32_t count)
{
    const std::uint32_t rows = count > 2 ? 2 : 1;
    const std::uint32_t width = area.width / 2;
    const std::uint32_t height = area.height / rows;
    const std::uint32_t place = index - 1;
    return {.x = area.x + static_cast<int>((place % 2) * width), .y = area.y + static_cast<int>((place / 2) * height), .width = width, .height = height};
}

std::string scenePlayStarts(const asset::Project& project, std::string edited)
{
    return project.startupScene.empty() ? std::move(edited) : project.startupScene;
}

core::Result<void> PlayProcess::start(const asset::Project& project, const scene::Scene& scene,
                                      asset::AssetId source, const std::filesystem::path& player,
                                      bool waitForDebugger)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(player, error))
        return core::makeError(
            core::ErrorCode::NotFound,
            "Build or install devex-player beside the editor to play in a separate process");
    if (core::Result<void> prepared = prepare(project, scene, source, waitForDebugger, {}); !prepared)
        return prepared;
    return launch(project, player);
}

core::Result<void> PlayProcess::prepare(const asset::Project& project, const scene::Scene& scene, asset::AssetId source, bool waitForDebugger,
                                        const PlayInstance& instance)
{
    if (m_process)
        return core::makeError(core::ErrorCode::InvalidState, "a game is already running");
    std::error_code error;
    m_label = instance.count > 1 ? std::format("[Player {}] ", instance.index) : std::string{};
    m_root = std::filesystem::absolute(project.cacheDirectory() / "play", error);
    if (error)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "cannot resolve the project's play cache directory");
    m_directory = m_root / core::Uuid::generate().toString();
    if (auto saved = scene::saveSceneFile(scene, m_directory / "scene.dvxscene"); !saved)
    {
        cleanSnapshot();
        return saved;
    }
    serialization::TextDocument document;
    serialization::TextSection session;
    session.type = "play_session";
    session.attributes = {{"version", std::int64_t{1}},
                          {"scene_asset", source.uuid.toString()},
                          {"wait_for_debugger", waitForDebugger},
                          {"language", project.localization.testLanguage},
                          {"instance", std::int64_t{instance.index}},
                          {"instances", std::int64_t{instance.count}},
                          {"port", std::int64_t{instance.port}}};
    if (instance.area)
    {
        session.attributes.push_back({"window_x", std::int64_t{instance.area->x}});
        session.attributes.push_back({"window_y", std::int64_t{instance.area->y}});
        session.attributes.push_back({"window_width", std::int64_t{instance.area->width}});
        session.attributes.push_back({"window_height", std::int64_t{instance.area->height}});
    }
    document.sections.push_back(std::move(session));
    if (auto saved =
            core::writeTextFile(m_directory / "session.dvx", serialization::writeText(document));
        !saved)
    {
        cleanSnapshot();
        return saved;
    }
    m_prepared = true;
    return {};
}

core::Result<void> PlayProcess::launch(const asset::Project& project, const std::filesystem::path& player)
{
    const std::vector<std::string> arguments{core::toUtf8(player), core::toUtf8(project.file),
                                             "--editor-session", core::toUtf8(m_directory)};
    auto process = platform::Process::startInteractive(arguments, project.root);
    if (!process)
    {
        cleanSnapshot();
        return std::unexpected(process.error());
    }
    m_prepared = false;
    m_process = std::move(*process);
    m_status = {.running = true, .starting = true, .processId = m_process->id()};
    m_stopTime.reset();
    m_requestedPause.reset();
    DEVEX_LOG_INFO("Playing in a separate process ({})", m_status.processId);
    return {};
}

void PlayProcess::cleanSnapshot()
{
    // Only a UUID directory this controller created under its play cache can be removed.
    if (!m_directory.empty() && m_directory.parent_path() == m_root &&
        core::Uuid::parse(core::toUtf8(m_directory.filename())))
    {
        std::error_code error;
        std::filesystem::remove_all(m_directory, error);
    }
}

void PlayProcess::readLine(const std::string& line)
{
    if (line.size() <= 65536 && line.starts_with(prefix))
    {
        const auto parsed = serialization::parseText(std::string_view(line).substr(prefix.size()));
        if (parsed && parsed->sections.size() == 1)
        {
            const auto& section = parsed->sections.front();
            if (section.type == "state")
            {
                m_status.starting = false;
                m_status.paused = flag(section, "paused");
                if (m_requestedPause == m_status.paused)
                    m_requestedPause.reset();
                m_status.debuggerAvailable = flag(section, "debugger");
                m_status.debuggerAttached = flag(section, "attached");
                m_status.waitingForDebugger = flag(section, "waiting");
                return;
            }
            if (section.type == "log")
            {
                const auto* value = section.findAttribute("level");
                const auto level =
                    value != nullptr ? serialization::asInteger(*value) : std::nullopt;
                if (level && *level >= 0 && *level <= static_cast<int>(core::LogLevel::Fatal))
                {
                    core::logMessage(static_cast<core::LogLevel>(*level), m_label + text(section, "message"),
                                     std::source_location::current(), text(section, "category"));
                    return;
                }
            }
        }
    }
    if (!line.empty())
        DEVEX_LOG_INFO_IN("game", "{}{}{}", m_label, std::string_view(line).substr(0, 16384),
                          line.size() > 16384 ? " [long output truncated]" : "");
}

void PlayProcess::update()
{
    if (!m_process)
        return;
    const auto exited = m_process->exitCode();
    const auto lines = m_process->readLines();
    for (const auto& line : lines)
        readLine(line);
    // Drain a terminated child's remaining output over bounded updates before closing its pipe.
    if (exited && lines.empty())
    {
        if (m_status.stopping || *exited == 0)
            DEVEX_LOG_INFO("Stopped the game process (exit {})", *exited);
        else
            DEVEX_LOG_ERROR("The game process exited with code {}. Its logs are in {}", *exited,
                            core::toUtf8(m_directory / "logs"));
        const bool clean = *exited == 0;
        m_process.reset();
        m_status = {};
        m_stopTime.reset();
        m_requestedPause.reset();
        if (clean)
            cleanSnapshot();
        return;
    }
    if (m_stopTime && Clock::now() - *m_stopTime > std::chrono::seconds(2))
    {
        DEVEX_LOG_WARNING("The game did not respond to Stop; terminating its process");
        m_process->kill();
        m_stopTime.reset();
    }
}

void PlayProcess::command(std::string_view command)
{
    if (m_process)
        if (auto sent = m_process->sendLine(command); !sent)
            DEVEX_LOG_WARNING("Cannot control the game process: {}", sent.error());
}
void PlayProcess::pause(bool paused)
{
    command(paused ? "pause" : "resume");
    if (m_status.running)
        m_requestedPause = paused;
}
void PlayProcess::step() { command("step"); }
void PlayProcess::stop()
{
    if (m_process && !m_status.stopping)
    {
        m_status.stopping = true;
        m_stopTime = Clock::now();
        command("stop");
    }
}
void PlayProcess::shutdown()
{
    stop();
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (m_process && Clock::now() < deadline)
    {
        update();
        if (m_process)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    m_process.reset();
    m_status = {};
    m_stopTime.reset();
    m_requestedPause.reset();
}

PlayGroup::~PlayGroup()
{
    shutdown();
}

core::Result<void> PlayGroup::start(const asset::Project& project, const scene::Scene& scene, asset::AssetId source,
                                     const std::filesystem::path& player, bool waitForDebugger, std::uint32_t instances, bool connect,
                                     const platform::ScreenArea& area)
{
    if (m_status.running)
        return core::makeError(core::ErrorCode::InvalidState, "a game is already running");
    std::error_code error;
    if (!std::filesystem::is_regular_file(player, error))
        return core::makeError(
            core::ErrorCode::NotFound,
            "Build or install devex-player beside the editor to play in a separate process");
    shutdown();
    const std::uint32_t count = std::max(instances, 1u);
    const std::uint16_t port = count > 1 && connect ? net::findFreePort() : std::uint16_t{0};
    for (std::uint32_t index = 1; index <= count; ++index)
    {
        const PlayInstance instance{.index = index,
                                    .count = count,
                                    .port = port,
                                    .area = count > 1 && area.width != 0 ? std::optional(playWindowArea(area, index, count)) : std::nullopt};
        auto process = std::make_unique<PlayProcess>();
        // Every snapshot now, so that they are all of the same scene however late they start.
        if (core::Result<void> prepared = process->prepare(project, scene, source, waitForDebugger && index == 1, instance); !prepared)
        {
            m_processes.clear();
            return prepared;
        }
        m_processes.push_back(std::move(process));
    }
    m_project = project;
    m_player = player;
    m_paused.reset();
    if (core::Result<void> launched = m_processes.front()->launch(project, player); !launched)
    {
        m_processes.clear();
        return launched;
    }
    m_launched = 1;
    refreshStatus();
    return {};
}

PlayProcess* PlayGroup::primary() const
{
    for (std::size_t index = 0; index < m_launched; ++index)
    {
        if (m_processes[index]->status().running)
            return m_processes[index].get();
    }
    return nullptr;
}

void PlayGroup::refreshStatus()
{
    const PlayProcess* const first = primary();
    const bool waiting = m_launched < m_processes.size();
    if (first == nullptr)
    {
        m_status = {};
        return;
    }
    m_status = first->status();
    m_status.starting = m_status.starting || waiting;
}

void PlayGroup::update()
{
    for (std::size_t index = 0; index < m_launched; ++index)
        m_processes[index]->update();
    // The next once this one is ready, and its debugger attached if it waits for one.
    if (m_launched != 0 && m_launched < m_processes.size())
    {
        const tools::ExternalPlayStatus& last = m_processes[m_launched - 1]->status();
        if (!last.running || last.stopping)
        {
            // It failed or it stops: the others have no one to join.
            m_processes.resize(m_launched);
        }
        else if (!last.starting && !last.waitingForDebugger)
        {
            if (core::Result<void> launched = m_processes[m_launched]->launch(*m_project, m_player); !launched)
            {
                DEVEX_LOG_ERROR("Cannot start the game process: {}", launched.error());
                m_processes.resize(m_launched);
            }
            else
            {
                if (m_paused)
                    m_processes[m_launched]->pause(*m_paused);
                ++m_launched;
            }
        }
    }
    refreshStatus();
}

void PlayGroup::pause(bool paused)
{
    m_paused = paused;
    for (std::size_t index = 0; index < m_launched; ++index)
        m_processes[index]->pause(paused);
}

bool PlayGroup::pauseRequested() const
{
    const PlayProcess* const first = primary();
    return first != nullptr && first->pauseRequested();
}

void PlayGroup::step()
{
    for (std::size_t index = 0; index < m_launched; ++index)
        m_processes[index]->step();
}

void PlayGroup::stop()
{
    // What is not launched yet stays so.
    m_processes.resize(m_launched);
    for (const auto& process : m_processes)
        process->stop();
    refreshStatus();
}

void PlayGroup::shutdown()
{
    m_processes.resize(m_launched);
    for (const auto& process : m_processes)
        process->stop();
    for (const auto& process : m_processes)
        process->shutdown();
    m_processes.clear();
    m_launched = 0;
    m_status = {};
}

core::Result<PlaySession> readPlaySession(const std::filesystem::path& directory)
{
    const auto contents = core::readTextFile(directory / "session.dvx");
    if (!contents)
        return std::unexpected(contents.error());
    const auto parsed = serialization::parseText(*contents);
    if (!parsed || parsed->sections.size() != 1 || parsed->sections[0].type != "play_session")
        return core::makeError(core::ErrorCode::InvalidArgument, "invalid editor play session");
    const auto& section = parsed->sections.front();
    const auto* version = section.findAttribute("version");
    if (version == nullptr || serialization::asInteger(*version) != 1)
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "unsupported editor play protocol");
    const auto uuid = core::Uuid::parse(text(section, "scene_asset"));
    if (!uuid)
        return core::makeError(core::ErrorCode::InvalidArgument, "invalid play scene identifier");
    const auto number = [&](std::string_view key) {
        const auto* const value = section.findAttribute(key);
        return value != nullptr ? serialization::asInteger(*value).value_or(0) : std::int64_t{0};
    };
    PlaySession session{.source = asset::AssetId{*uuid}, .waitForDebugger = flag(section, "wait_for_debugger"), .language = text(section, "language")};
    session.instance.index = static_cast<std::uint32_t>(std::max<std::int64_t>(number("instance"), 1));
    session.instance.count = static_cast<std::uint32_t>(std::max<std::int64_t>(number("instances"), 1));
    session.instance.port = static_cast<std::uint16_t>(number("port"));
    if (number("window_width") > 0 && number("window_height") > 0)
    {
        session.instance.area = platform::ScreenArea{.x = static_cast<int>(number("window_x")),
                                                     .y = static_cast<int>(number("window_y")),
                                                     .width = static_cast<std::uint32_t>(number("window_width")),
                                                     .height = static_cast<std::uint32_t>(number("window_height"))};
    }
    return session;
}

void EditorPlayClient::startNetwork(net::Multiplayer& multiplayer) const
{
    if (m_instance.count > 1)
        multiplayer.setTestInstance(m_instance.index);
    if (m_instance.port == 0)
        return;
    multiplayer.keepTestSession();
    if (m_instance.index == 1)
    {
        if (core::Result<void> hosted = multiplayer.host(m_instance.port, m_instance.count - 1); !hosted)
            DEVEX_LOG_ERROR("Cannot host the session of the instances: {}", hosted.error());
        else
            DEVEX_LOG_INFO("Hosting the session of {} instances on port {}", m_instance.count, m_instance.port);
        return;
    }
    // In the session before the game starts, as the host is: its Start finds the same on every instance.
    // Failing to reach the host, it tries again rather than tell a game that asked for nothing.
    const auto deadline = Clock::now() + std::chrono::seconds(15);
    while (Clock::now() < deadline && multiplayer.state() != net::MultiplayerState::Online)
    {
        if (multiplayer.state() == net::MultiplayerState::Offline)
        {
            multiplayer.poll();
            if (core::Result<void> joined = multiplayer.join("127.0.0.1", m_instance.port); !joined)
            {
                DEVEX_LOG_ERROR("Cannot join the session of the instances: {}", joined.error());
                return;
            }
        }
        multiplayer.keepAlive();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (multiplayer.state() == net::MultiplayerState::Online)
        DEVEX_LOG_INFO("Joined the session of the instances as player {}", multiplayer.localPeer());
    else
        DEVEX_LOG_ERROR("The instance that hosts did not answer on port {}: this one plays alone", m_instance.port);
}

EditorPlayClient::EditorPlayClient(std::filesystem::path directory)
    : m_directory(std::move(directory))
{
    core::removeLogSink(core::consoleLogSinkId);
    m_logSink = core::addLogSink(
        [](const core::LogRecord& record)
        {
            // A packet is one line even for a multiline exception or a quoted file path.
            std::string message(record.message.substr(0, 16384));
            if (record.message.size() > message.size())
            {
                while (!message.empty() &&
                       (static_cast<unsigned char>(record.message[message.size()]) & 0xc0) == 0x80)
                    message.pop_back();
                message += " [long message truncated]";
            }
            if (record.level >= core::LogLevel::Warning && record.location.line() != 0)
                message +=
                    std::format(" ({}:{})", record.location.file_name(), record.location.line());
            packet(std::format("[log level={} category={} message={}]",
                               static_cast<int>(record.level),
                               serialization::formatValue(std::string(record.category)),
                               serialization::formatValue(message)));
        });
}
EditorPlayClient::~EditorPlayClient() { core::removeLogSink(m_logSink); }

core::Result<scene::Scene> EditorPlayClient::readSnapshot()
{
    core::Result<PlaySession> session = readPlaySession(m_directory);
    if (!session)
        return std::unexpected(session.error());
    m_source = session->source;
    m_waitForDebugger = session->waitForDebugger;
    m_language = std::move(session->language);
    m_instance = session->instance;
    return scene::loadSceneFile(m_directory / "scene.dvxscene");
}
void EditorPlayClient::update()
{
    for (const auto& line : m_input.readLines())
    {
        if (line == "stop")
            m_stopped = true;
        else if (line == "pause")
            m_paused = true;
        else if (line == "resume")
        {
            m_paused = false;
            m_step = false;
        }
        else if (line == "step" && m_paused)
            m_step = true;
    }
    m_stopped = m_stopped || m_input.closed();
}
void EditorPlayClient::report(bool debuggerAvailable, bool debuggerAttached, bool waiting)
{
    std::string state = std::format("[state paused={} debugger={} attached={} waiting={}]",
                                    m_paused, debuggerAvailable, debuggerAttached, waiting);
    if (state != m_report)
    {
        m_report = std::move(state);
        packet(m_report);
    }
}

} // namespace devex::runtime::detail
