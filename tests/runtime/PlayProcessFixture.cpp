#include <devex/core/Log.hpp>
#include <devex/core/LogFile.hpp>
#include <devex/runtime/Application.hpp>

#include <cstdlib>
#include <format>
#include <string>
#include <thread>

namespace
{
class PlayerFixture final : public devex::runtime::Application
{
  public:
    devex::core::Result<void> onStartup() override
    {
        for (auto entity = scene().firstRoot(); entity.isValid();
             entity = scene().nextSibling(entity))
        {
            const std::string name(scene().name(entity));
            m_hang = m_hang || name == "Hang";
            m_crash = m_crash || name == "Crash";
            m_ask = m_ask || name == "Ask";
            DEVEX_LOG_INFO_IN("test-player", "Snapshot: {}", name);
        }
        DEVEX_LOG_WARNING_IN("test-player", "A quoted \"message\"\nwith another line");
        return {};
    }
    void onFixedUpdate(devex::core::Duration) override
    {
        DEVEX_LOG_INFO_IN("test-player", "Tick {}", ++m_ticks);
    }
    void onUpdate(devex::core::Duration) override
    {
        if (m_crash)
            std::_Exit(23); // An abrupt child failure must leave the supervisor alive.
        // Where this game stands among those Play started together, each time it changes, with what
        // it was told when its code asked to connect.
        if (auto* const network = multiplayer(); network != nullptr && network->testInstance() != 0)
        {
            const bool asked = m_ask && network->host(1).has_value();
            const std::string where = std::format("Network instance={} online={} host={} peer={} others={} asked={}", network->testInstance(),
                                                  network->state() == devex::net::MultiplayerState::Online, network->isHost(), network->localPeer(),
                                                  network->peers().size(), asked);
            if (where != m_network)
            {
                m_network = where;
                DEVEX_LOG_INFO_IN("test-player", "{}", where);
            }
        }
        if (m_hang)
            std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    void onShutdown() override { DEVEX_LOG_INFO_IN("test-player", "Shutdown"); }

  private:
    int m_ticks = 0;
    std::string m_network;
    bool m_hang = false;
    bool m_crash = false;
    bool m_ask = false;
};
} // namespace

int main(int argc, char** argv)
{
    if (argc != 4 || std::string_view(argv[2]) != "--editor-session")
        return 2;
    devex::runtime::ApplicationConfig config;
    config.project = std::filesystem::path(argv[1]);
    config.editorSession = std::filesystem::path(argv[3]);
    config.enableRendering = false;
    config.enableTools = false;
    config.enableAudio = false;
    config.workerThreads = 1;
    config.userDirectory = config.editorSession / "user";
    config.maxFrameRate = 120;
    auto log = devex::core::LogFile::open(config.editorSession / "logs/player.log");
    return devex::runtime::run<PlayerFixture>(config);
}
