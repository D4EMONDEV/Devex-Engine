#include "GameCodeBuilder.hpp"

#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <system_error>
#include <thread>
#include <utility>

namespace devex::runtime::detail {
namespace {

using namespace std::chrono_literals;

constexpr auto checkInterval = 500ms;
// Editors often save in several steps: a build starts once the folder has been quiet this long.
constexpr auto settleTime = 300ms;

constexpr std::string_view cmakeListsTemplate = R"(# The game module of the project, built by the Devex editor whenever a file of this folder changes.
cmake_minimum_required(VERSION 3.25)
project(Game LANGUAGES CXX)

find_package(Devex CONFIG REQUIRED)

file(GLOB_RECURSE sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp")
devex_add_game_module(SOURCES ${sources})
)";

constexpr std::string_view gameTemplate = R"(// The game module: components hold the state of the game, systems update it every frame.
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>

namespace {

// Turns its entity around the vertical axis.
struct Spinner
{
    // In radians per second.
    float speed = 1.0f;
};
DEVEX_DECLARE_REFLECTION(Spinner);
DEVEX_REFLECT(Spinner)
{
    type.field("speed", &Spinner::speed, {.angle = true});
}

void spin(devex::runtime::SystemContext& context)
{
    const auto seconds = static_cast<float>(context.delta.count());
    for ([[maybe_unused]] auto [entity, spinner, transform] :
         context.scene.view<Spinner, devex::scene::Transform>())
    {
        transform.rotation =
            devex::math::angleAxis(spinner.speed * seconds, devex::math::Vec3{0.0f, 1.0f, 0.0f}) * transform.rotation;
    }
}

} // namespace

DEVEX_GAME_MODULE(game)
{
    game.component<Spinner>();
    game.system("Spin", devex::runtime::SystemPhase::Update, &spin);
}
)";

// One folder per engine configuration: a module only loads into an engine built like it.
[[nodiscard]] std::filesystem::path buildDirectory(const asset::Project& project, std::string_view configuration)
{
    return project.cacheDirectory() / "code" / core::pathFromUtf8(std::string(configuration));
}

[[nodiscard]] bool contains(std::string_view text, std::string_view part)
{
    return text.find(part) != std::string_view::npos;
}

} // namespace

std::filesystem::path GameCodeBuilder::libraryPath(const asset::Project& project, std::string_view configuration)
{
    return buildDirectory(project, configuration) / "bin" / libraryFileName();
}

std::filesystem::path GameCodeBuilder::libraryFileName()
{
#ifdef _WIN32
    return "Game.dll";
#else
    return "libGame.so";
#endif
}

bool GameCodeBuilder::hasCode(const asset::Project& project)
{
    std::error_code error;
    return std::filesystem::exists(project.codeDirectory() / "CMakeLists.txt", error);
}

core::Result<void> GameCodeBuilder::createCode(const asset::Project& project)
{
    if (hasCode(project))
    {
        return core::makeError(core::ErrorCode::AlreadyExists, "the project already has game code");
    }
    if (core::Result<void> written = core::writeTextFile(project.codeDirectory() / "CMakeLists.txt", cmakeListsTemplate);
        !written)
    {
        return written;
    }
    return core::writeTextFile(project.codeDirectory() / "Game.cpp", gameTemplate);
}

core::Result<std::filesystem::path> GameCodeBuilder::createComponent(const asset::Project& project,
                                                                     std::string_view componentName)
{
    constexpr std::string_view componentTemplate = R"(// A component of the game, written in C++. Register it in the DEVEX_GAME_MODULE of the project:
//
//     game.component<{0}>();
//
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>

struct {0}
{{
    // In radians per second.
    float speed = 1.0f;
}};
DEVEX_DECLARE_REFLECTION({0});
DEVEX_REFLECT({0})
{{
    type.field("speed", &{0}::speed, {{.angle = true}});
}}
)";
    std::filesystem::path file = project.codeDirectory() / core::pathFromUtf8(std::string(componentName) + ".cpp");
    std::error_code error;
    if (std::filesystem::exists(file, error))
    {
        return core::makeError(core::ErrorCode::AlreadyExists, "'{}' already exists", core::toUtf8(file.filename()));
    }
    if (core::Result<void> written = core::writeTextFile(file, std::format(componentTemplate, componentName)); !written)
    {
        return std::unexpected(written.error());
    }
    return file;
}

GameCodeBuilder::GameCodeBuilder(asset::Project project, std::filesystem::path devexConfigDirectory,
                                 std::string configuration)
    : m_project(std::move(project))
    , m_devexConfigDirectory(std::move(devexConfigDirectory))
    , m_configuration(std::move(configuration))
{
    m_sources = snapshotSources();
    // Sources newer than the library, or no library at all, need a build right away.
    std::error_code error;
    const std::filesystem::file_time_type built =
        std::filesystem::last_write_time(libraryPath(m_project, m_configuration), error);
    m_buildRequested = error || built < m_sources.newest;
}

GameCodeBuilder::Snapshot GameCodeBuilder::snapshotSources() const
{
    Snapshot snapshot;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator entry(m_project.codeDirectory(), error), end;
         !error && entry != end; entry.increment(error))
    {
        if (entry->is_regular_file(error))
        {
            snapshot.newest = std::max(snapshot.newest, entry->last_write_time(error));
            ++snapshot.files;
        }
    }
    return snapshot;
}

void GameCodeBuilder::requestBuild() noexcept
{
    m_buildRequested = true;
}

core::Result<void> GameCodeBuilder::buildAndWait()
{
    m_buildRequested = false;
    m_changedAt.reset();
    startBuild();
    while (m_process)
    {
        static_cast<void>(update());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (m_state != State::Succeeded)
    {
        return core::makeError(core::ErrorCode::InvalidState, "the game code does not build: {}", m_message);
    }
    return {};
}

GameCodeBuilder::State GameCodeBuilder::state() const noexcept
{
    return m_state;
}

const std::string& GameCodeBuilder::message() const noexcept
{
    return m_message;
}

bool GameCodeBuilder::update()
{
    const Clock::time_point now = Clock::now();
    if (now >= m_nextCheck)
    {
        m_nextCheck = now + checkInterval;
        if (Snapshot sources = snapshotSources(); sources != m_sources)
        {
            m_sources = sources;
            m_changedAt = now;
        }
    }
    if (m_changedAt && now - *m_changedAt >= settleTime)
    {
        m_changedAt.reset();
        m_buildRequested = true;
    }

    if (m_process)
    {
        for (const std::string& line : m_process->readLines())
        {
            if (contains(line, ": error") || contains(line, "error C") || contains(line, "error LNK") ||
                contains(line, "CMake Error") || line.starts_with("FAILED:") || line.starts_with("error:"))
            {
                DEVEX_LOG_ERROR("{}", line);
                if (m_message.empty())
                {
                    m_message = line;
                }
            }
            else if (contains(line, ": warning") || contains(line, "CMake Warning"))
            {
                DEVEX_LOG_WARNING("{}", line);
            }
            else if (!line.empty())
            {
                DEVEX_LOG_DEBUG("{}", line);
            }
        }
        if (const std::optional<int> exitCode = m_process->exitCode())
        {
            // The last lines may arrive with the exit.
            for (const std::string& line : m_process->readLines())
            {
                DEVEX_LOG_DEBUG("{}", line);
            }
            m_process.reset();
            finishBuild(*exitCode);
            return m_state == State::Succeeded;
        }
        return false;
    }

    if (m_buildRequested)
    {
        m_buildRequested = false;
        startBuild();
    }
    return false;
}

void GameCodeBuilder::startBuild()
{
#ifdef _WIN32
    const std::filesystem::path build = buildDirectory(m_project, m_configuration);
    // The folder is configured again when the engine it was configured for changes.
    const std::filesystem::path engineStamp = build / "devex-engine.txt";
    const std::string engine = m_devexConfigDirectory.generic_string();
    std::error_code error;
    const core::Result<std::string> configuredFor = core::readTextFile(engineStamp);
    const bool configure = !std::filesystem::exists(build / "CMakeCache.txt", error) || !configuredFor ||
                           *configuredFor != engine;
    static_cast<void>(core::writeTextFile(engineStamp, engine));
    const std::string script = std::format(R"(@echo off
setlocal
if defined VCToolsInstallDir goto build
set "vswhere=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%vswhere%" goto missing
for /f "usebackq delims=" %%i in (`"%vswhere%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "vs=%%i"
if not defined vs goto missing
call "%vs%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
:build
if "{4}" == "configure" (
    cmake -S "{1}" -B "{0}" -G Ninja -DCMAKE_BUILD_TYPE={2} "-DDevex_DIR={3}"
    if errorlevel 1 exit /b 1
)
cmake --build "{0}"
exit /b %errorlevel%
:missing
echo error: building game code needs Visual Studio with its C++ tools
exit /b 1
)",
                                           core::toUtf8(build), core::toUtf8(m_project.codeDirectory()),
                                           m_configuration, engine, configure ? "configure" : "build");
    const std::filesystem::path scriptPath = m_project.cacheDirectory() / "code" / core::pathFromUtf8(std::format("build-{}.cmd", m_configuration));
    if (core::Result<void> written = core::writeTextFile(scriptPath, script); !written)
    {
        m_state = State::Failed;
        m_message = written.error().message;
        DEVEX_LOG_ERROR("Cannot build the game code: {}", written.error());
        return;
    }
    const std::array<std::string, 4> arguments{"cmd.exe", "/d", "/c", core::toUtf8(scriptPath)};
    core::Result<platform::Process> process = platform::Process::start(arguments, m_project.root);
#else
    core::Result<platform::Process> process =
        core::makeError(core::ErrorCode::Unsupported, "building game code is only supported on Windows for now");
#endif
    if (!process)
    {
        m_state = State::Failed;
        m_message = process.error().message;
        DEVEX_LOG_ERROR("Cannot build the game code: {}", process.error());
        return;
    }
    m_process = std::move(*process);
    m_state = State::Building;
    m_message.clear();
    m_buildStart = Clock::now();
    DEVEX_LOG_INFO("Building the game code...");
}

void GameCodeBuilder::finishBuild(int exitCode)
{
    const double seconds = std::chrono::duration<double>(Clock::now() - m_buildStart).count();
    if (exitCode == 0)
    {
        m_state = State::Succeeded;
        m_message = std::format("built in {:.1f} s", seconds);
        DEVEX_LOG_INFO("Game code built in {:.1f} s", seconds);
        return;
    }
    m_state = State::Failed;
    if (m_message.empty())
    {
        m_message = std::format("the build failed with code {}", exitCode);
    }
    DEVEX_LOG_ERROR("Game code build failed: see the errors above");
}

} // namespace devex::runtime::detail
