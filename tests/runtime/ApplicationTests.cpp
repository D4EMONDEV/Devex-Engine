#include <devex/runtime/Application.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using devex::core::Duration;
using devex::core::Result;
using devex::runtime::ApplicationConfig;

namespace {

const ApplicationConfig testConfig{
    .title = "Devex tests",
    .width = 320,
    .height = 240,
};

// Counts lifecycle calls and quits after a few frames.
class CountingApplication final : public devex::runtime::Application
{
public:
    int startups = 0;
    int updates = 0;
    int shutdowns = 0;
    bool windowMatchedConfig = false;

    Result<void> onStartup() override
    {
        ++startups;
        windowMatchedConfig =
            window().size() == devex::math::Extent2D{testConfig.width, testConfig.height};
        return {};
    }

    void onUpdate(Duration /*frameDelta*/) override
    {
        if (++updates == 3)
        {
            requestQuit();
        }
    }

    void onShutdown() override
    {
        ++shutdowns;
    }
};

class FailingApplication final : public devex::runtime::Application
{
public:
    int updates = 0;
    int shutdowns = 0;

    Result<void> onStartup() override
    {
        return devex::core::makeError(devex::core::ErrorCode::InvalidState, "startup refused");
    }

    void onUpdate(Duration /*frameDelta*/) override
    {
        ++updates;
    }

    void onShutdown() override
    {
        ++shutdowns;
    }
};

} // namespace

TEST_CASE("run drives the lifecycle until the application quits", "[runtime][application]")
{
    CountingApplication application;

    CHECK(devex::runtime::run(application, testConfig) == EXIT_SUCCESS);

    CHECK(application.startups == 1);
    CHECK(application.windowMatchedConfig);
    CHECK(application.updates == 3);
    CHECK(application.shutdowns == 1);
}

TEST_CASE("A failed startup stops before the first frame", "[runtime][application]")
{
    FailingApplication application;

    CHECK(devex::runtime::run(application, testConfig) == EXIT_FAILURE);

    CHECK(application.updates == 0);
    CHECK(application.shutdowns == 0);
}

TEST_CASE("An application can run again after a previous run", "[runtime][application]")
{
    CountingApplication first;
    CountingApplication second;

    CHECK(devex::runtime::run(first, testConfig) == EXIT_SUCCESS);
    CHECK(devex::runtime::run(second, testConfig) == EXIT_SUCCESS);
    CHECK(second.updates == 3);
}
