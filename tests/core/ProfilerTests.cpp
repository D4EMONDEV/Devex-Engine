#include <devex/core/Profiler.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string_view>
#include <thread>

namespace profiler = devex::core::profiler;
using devex::core::ProfileZone;

namespace {

// The profiler is shared by the whole process: each test starts it afresh and stops it after.
struct Recording
{
    Recording()
    {
        profiler::clear();
        profiler::setPaused(false);
        profiler::setEnabled(true);
    }
    ~Recording()
    {
        profiler::setEnabled(false);
        profiler::clear();
    }
    Recording(const Recording&) = delete;
    Recording& operator=(const Recording&) = delete;
};

void spin(std::chrono::microseconds duration)
{
    const auto until = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < until)
    {
    }
}

[[nodiscard]] const ProfileZone* findZone(const devex::core::ProfileFrame& frame, std::string_view name)
{
    for (const ProfileZone& zone : frame.cpu)
    {
        if (name == zone.name)
        {
            return &zone;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("Zones nest inside the frame they end in", "[core][profiler]")
{
    const Recording recording;
    profiler::beginFrame();
    {
        DEVEX_PROFILE_SCOPE("Update");
        spin(std::chrono::microseconds(200));
        {
            DEVEX_PROFILE_SCOPE("Physics");
            spin(std::chrono::microseconds(200));
        }
    }
    profiler::endFrame();

    const auto frames = profiler::history();
    REQUIRE(frames.size() == 1);
    const devex::core::ProfileFrame& frame = *frames.front();
    REQUIRE(frame.cpu.size() == 2);
    const ProfileZone* const update = findZone(frame, "Update");
    const ProfileZone* const physics = findZone(frame, "Physics");
    REQUIRE(update != nullptr);
    REQUIRE(physics != nullptr);
    CHECK(update->depth == 0);
    CHECK(physics->depth == 1);
    CHECK(physics->begin >= update->begin);
    CHECK(physics->end <= update->end);
    CHECK(update->begin >= frame.begin);
    CHECK(update->end <= frame.end);
    CHECK(update->thread == 0);
}

TEST_CASE("Nothing is measured while the profiler is off, and a pause freezes the history",
          "[core][profiler]")
{
    const Recording recording;
    profiler::setEnabled(false);
    profiler::beginFrame();
    {
        DEVEX_PROFILE_SCOPE("Unseen");
    }
    profiler::endFrame();
    CHECK(profiler::history().empty());

    profiler::setEnabled(true);
    for (int frame = 0; frame < 3; ++frame)
    {
        profiler::beginFrame();
        profiler::endFrame();
    }
    CHECK(profiler::history().size() == 3);
    profiler::setPaused(true);
    profiler::beginFrame();
    profiler::endFrame();
    CHECK(profiler::history().size() == 3);
    profiler::setPaused(false);
}

TEST_CASE("Zones of other threads land in the frame, on their own lane", "[core][profiler]")
{
    const Recording recording;
    profiler::beginFrame();
    std::thread worker([] {
        profiler::nameThread("Loader");
        DEVEX_PROFILE_SCOPE("Decode");
        spin(std::chrono::microseconds(100));
    });
    worker.join();
    profiler::endFrame();

    const auto frames = profiler::history();
    REQUIRE(frames.size() == 1);
    const ProfileZone* const decode = findZone(*frames.front(), "Decode");
    REQUIRE(decode != nullptr);
    CHECK(decode->thread >= 2);
    CHECK(profiler::threadNames().at(decode->thread) == "Loader");
}

TEST_CASE("The GPU times of a frame arrive after it and find it", "[core][profiler]")
{
    const Recording recording;
    profiler::beginFrame();
    const std::uint64_t measured = profiler::currentFrame();
    profiler::endFrame();
    profiler::beginFrame();
    profiler::endFrame();

    profiler::reportGpu(measured, {ProfileZone{.name = "Shadows", .begin = 0, .end = 1000}}, 1000);
    // A frame that is no longer kept is ignored rather than attached to another.
    profiler::reportGpu(measured + 100, {ProfileZone{.name = "Lost", .begin = 0, .end = 10}}, 10);

    const auto frames = profiler::history();
    REQUIRE(frames.size() == 2);
    CHECK(frames[0]->gpuMeasured);
    CHECK(frames[0]->gpuDuration == 1000);
    REQUIRE(frames[0]->gpu.size() == 1);
    CHECK(std::string_view(frames[0]->gpu[0].name) == "Shadows");
    CHECK_FALSE(frames[1]->gpuMeasured);
}

TEST_CASE("A frame sums its zones by place, with the time left to each", "[core][profiler]")
{
    // Render (0-100) holds two Shadow zones (10-30, 40-60); Audio (100-120) stands beside it.
    const std::vector<ProfileZone> zones{
        {.name = "Shadow", .begin = 10, .end = 30, .depth = 1},
        {.name = "Shadow", .begin = 40, .end = 60, .depth = 1},
        {.name = "Render", .begin = 0, .end = 100, .depth = 0},
        {.name = "Audio", .begin = 100, .end = 120, .depth = 0},
    };
    const auto lines = profiler::summarize(zones);
    REQUIRE(lines.size() == 3);
    // The costliest first, and each line right after the one it sits in.
    CHECK(std::string_view(lines[0].name) == "Render");
    CHECK(lines[0].inclusive == 100);
    CHECK(lines[0].exclusive == 60);
    CHECK(lines[0].parent == -1);
    CHECK(std::string_view(lines[1].name) == "Shadow");
    CHECK(lines[1].calls == 2);
    CHECK(lines[1].inclusive == 40);
    CHECK(lines[1].exclusive == 40);
    CHECK(lines[1].parent == 0);
    CHECK(lines[1].depth == 1);
    CHECK(std::string_view(lines[2].name) == "Audio");
    CHECK(lines[2].parent == -1);
}

TEST_CASE("Names built at run time are kept once", "[core][profiler]")
{
    const std::string built = std::string("Move ") + "players";
    const char* const first = profiler::intern(built);
    const char* const second = profiler::intern("Move players");
    CHECK(first == second);
    CHECK(std::string_view(first) == "Move players");
}
