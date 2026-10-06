#include <devex/platform/Platform.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

// The platform needs a display, as the renderer does.
TEST_CASE("Waiting for events ends at the time given, or as soon as something wakes it", "[platform][gpu]")
{
    using Clock = std::chrono::steady_clock;
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    // What the platform sent as it started, the devices it found among them, until it is quiet.
    for (int round = 0; round < 20 && platform->waitEvents(std::chrono::milliseconds(100)); ++round)
    {
        platform->pollEvents([](const devex::platform::Event&) {});
    }

    // Nothing comes: the time runs out.
    const Clock::time_point before = Clock::now();
    CHECK_FALSE(platform->waitEvents(std::chrono::milliseconds(50)));
    CHECK(Clock::now() - before >= std::chrono::milliseconds(40));

    // Woken from another thread long before.
    const Clock::time_point start = Clock::now();
    std::jthread waker([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        devex::platform::Platform::wake();
    });
    CHECK(platform->waitEvents(std::chrono::seconds(10)));
    CHECK(Clock::now() - start < std::chrono::seconds(5));
    waker.join();

    // The wake waited in the queue: the next poll takes it, and forwards nothing to the application.
    int forwarded = 0;
    CHECK(platform->pollEvents([&](const devex::platform::Event&) { ++forwarded; }));
    CHECK(forwarded == 0);
}
