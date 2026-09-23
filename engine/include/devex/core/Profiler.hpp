#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Where the time of a frame goes: named zones that nest, recorded on every thread and gathered by
// frame, with the time the GPU spent on each pass of the frame beside them. The editor shows the
// last frames in its Profiler panel.
namespace devex::core {

// A span of time spent in a named place: on the CPU in nanoseconds since the profiler started,
// on the GPU in nanoseconds since the GPU began the frame.
struct ProfileZone
{
    // Lives as long as the process: a literal, or a name from profiler::intern.
    const char* name = "";
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    // 0 for a zone opened outside any other on its thread.
    std::uint16_t depth = 0;
    // 0 for the thread that runs the frames, then the workers in the order they were named.
    std::uint16_t thread = 0;

    [[nodiscard]] std::uint64_t duration() const noexcept
    {
        return end - begin;
    }
};

struct ProfileFrame
{
    std::uint64_t index = 0;
    std::uint64_t begin = 0;
    std::uint64_t end = 0;
    // In the order they ended, which puts a zone after the zones it contains.
    std::vector<ProfileZone> cpu;
    // The passes of the frame on the GPU, known a few frames after the CPU finished it.
    std::vector<ProfileZone> gpu;
    std::uint64_t gpuDuration = 0;
    bool gpuMeasured = false;

    [[nodiscard]] std::uint64_t duration() const noexcept
    {
        return end - begin;
    }
};

// One line of the table of a frame: every zone of that name at that place in the nesting, summed.
struct ProfileSummary
{
    const char* name = "";
    std::uint16_t depth = 0;
    std::uint16_t thread = 0;
    // The time in the zone, and the time left once the zones inside it are taken out.
    std::uint64_t inclusive = 0;
    std::uint64_t exclusive = 0;
    std::uint32_t calls = 0;
    // Index of the line it sits in, or -1 at the top.
    std::int32_t parent = -1;
};

namespace profiler {

// Recording is off until turned on, as the tools do while their Profiler panel is open; off, a zone
// costs one test of a flag.
void setEnabled(bool enabled) noexcept;
[[nodiscard]] bool isEnabled() noexcept;

// While paused, frames are still measured but no longer kept, which freezes the history.
void setPaused(bool paused) noexcept;
[[nodiscard]] bool isPaused() noexcept;

// Called by the thread that runs the frames. A frame gathers the zones that end between the two.
void beginFrame();
void endFrame();
// The frame being measured, which the GPU timings of its passes are reported against.
[[nodiscard]] std::uint64_t currentFrame() noexcept;

// Any thread. Zones nest on their own thread; prefer ProfileScope, which cannot forget to end.
void beginZone(const char* name);
void endZone();

// The GPU timings of a frame, reported once the GPU has finished it. Ignored when the frame is no
// longer kept.
void reportGpu(std::uint64_t frame, std::vector<ProfileZone> passes, std::uint64_t duration);

// A stable copy of a name built at run time, such as the name of a system or of a C# zone.
[[nodiscard]] const char* intern(std::string_view name);

// Gives the calling thread a name and its lane in the timeline; unnamed threads share one.
void nameThread(std::string_view name);
[[nodiscard]] std::vector<std::string> threadNames();

// The frames kept, oldest first: 300 at most, about five seconds.
[[nodiscard]] std::vector<std::shared_ptr<const ProfileFrame>> history();
void clear();

// The lines of the table of a frame, parents before their children, the costliest first among
// siblings.
[[nodiscard]] std::vector<ProfileSummary> summarize(std::span<const ProfileZone> zones);

} // namespace profiler

// Measures the lifetime of a scope: DEVEX_PROFILE_SCOPE("Physics").
class ProfileScope
{
public:
    explicit ProfileScope(const char* name) noexcept;
    ~ProfileScope();
    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    bool m_active = false;
};

} // namespace devex::core

#define DEVEX_PROFILE_JOIN_INNER(a, b) a##b
#define DEVEX_PROFILE_JOIN(a, b) DEVEX_PROFILE_JOIN_INNER(a, b)
// Measures the rest of the enclosing scope under a name, which must outlive the process: a literal,
// or a name from core::profiler::intern.
#define DEVEX_PROFILE_SCOPE(name) \
    const ::devex::core::ProfileScope DEVEX_PROFILE_JOIN(devexProfileScope, __LINE__)(name)
