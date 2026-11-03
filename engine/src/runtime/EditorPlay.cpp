#include "EditorPlay.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <cstdio>
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

PlayProcess::~PlayProcess() { shutdown(); }

core::Result<void> PlayProcess::start(const asset::Project& project, const scene::Scene& scene,
                                      asset::AssetId source, const std::filesystem::path& player,
                                      bool waitForDebugger)
{
    if (m_process)
        return core::makeError(core::ErrorCode::InvalidState, "a game is already running");
    std::error_code error;
    if (!std::filesystem::is_regular_file(player, error))
        return core::makeError(
            core::ErrorCode::NotFound,
            "Build or install devex-player beside the editor to play in a separate process");
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
                          {"language", project.localization.testLanguage}};
    document.sections.push_back(std::move(session));
    if (auto saved =
            core::writeTextFile(m_directory / "session.dvx", serialization::writeText(document));
        !saved)
    {
        cleanSnapshot();
        return saved;
    }
    const std::vector<std::string> arguments{core::toUtf8(player), core::toUtf8(project.file),
                                             "--editor-session", core::toUtf8(m_directory)};
    auto process = platform::Process::startInteractive(arguments, project.root);
    if (!process)
    {
        cleanSnapshot();
        return std::unexpected(process.error());
    }
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
                    core::logMessage(static_cast<core::LogLevel>(*level), text(section, "message"),
                                     std::source_location::current(), text(section, "category"));
                    return;
                }
            }
        }
    }
    if (!line.empty())
        DEVEX_LOG_INFO_IN("game", "{}{}", std::string_view(line).substr(0, 16384),
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
    const auto contents = core::readTextFile(m_directory / "session.dvx");
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
    m_source = asset::AssetId{*uuid};
    m_waitForDebugger = flag(section, "wait_for_debugger");
    m_language = text(section, "language");
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
