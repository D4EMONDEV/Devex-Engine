#include <devex/core/JobSystem.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using devex::core::JobSystem;

TEST_CASE("Scheduled jobs run on worker threads", "[core][jobs]")
{
    JobSystem jobs(3);
    CHECK(jobs.workerCount() == 3);
    CHECK_FALSE(JobSystem::isWorkerThread());

    std::atomic<int> ran{0};
    std::atomic<int> onWorkers{0};
    for (int job = 0; job < 50; ++job)
    {
        jobs.schedule([&] {
            ++ran;
            onWorkers += JobSystem::isWorkerThread() ? 1 : 0;
        });
    }
    jobs.waitIdle();

    CHECK(ran == 50);
    CHECK(onWorkers == 50);
}

TEST_CASE("parallelFor visits every index exactly once", "[core][jobs]")
{
    JobSystem jobs(4);
    std::vector<std::atomic<int>> visits(1000);

    jobs.parallelFor(visits.size(), [&](std::size_t index) { ++visits[index]; });

    for (const std::atomic<int>& count : visits)
    {
        REQUIRE(count == 1);
    }
    jobs.parallelFor(0, [](std::size_t) { FAIL("no index to visit"); });
}

TEST_CASE("Nested parallel loops complete while every worker is busy", "[core][jobs]")
{
    // Each job runs its own loop: waiting for idle workers instead of working would deadlock.
    JobSystem jobs(2);
    std::atomic<int> total{0};
    for (int job = 0; job < 4; ++job)
    {
        jobs.schedule([&] {
            jobs.parallelFor(100, [&](std::size_t) { ++total; });
        });
    }
    jobs.waitIdle();

    CHECK(total == 400);
}

TEST_CASE("Destroying the job system discards jobs that have not started", "[core][jobs]")
{
    std::atomic<int> ran{0};
    {
        JobSystem jobs(1);
        std::atomic<bool> started{false};
        std::atomic<bool> release{false};
        jobs.schedule([&] {
            started = true;
            while (!release)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            ++ran;
        });
        while (!started)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        for (int job = 0; job < 10; ++job)
        {
            jobs.schedule([&] { ++ran; });
        }
        release = true;
    }
    // The running job finished; the queued ones may or may not have started.
    CHECK(ran >= 1);
    CHECK(ran <= 11);
}
