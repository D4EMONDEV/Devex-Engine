#include <devex/core/Profiler.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <tuple>
#include <unordered_set>

namespace devex::core {
namespace {

constexpr std::size_t keptFrames = 300;
// The lanes every timeline starts with: the thread that runs the frames, then the threads that
// were never named.
constexpr std::uint16_t frameThread = 0;
constexpr std::uint16_t unnamedThreads = 1;

struct OpenZone
{
    const char* name;
    std::uint64_t begin;
};

// Each thread keeps the zones it opened; only finished zones reach the shared frame.
struct ThreadState
{
    std::vector<OpenZone> open;
    std::uint16_t lane = unnamedThreads;
};

thread_local ThreadState t_thread;

struct State
{
    std::atomic<bool> enabled{false};
    std::atomic<bool> paused{false};
    std::atomic<std::uint64_t> frameIndex{0};
    std::chrono::steady_clock::time_point epoch = std::chrono::steady_clock::now();

    std::mutex mutex;
    std::shared_ptr<ProfileFrame> current;
    std::deque<std::shared_ptr<ProfileFrame>> history;
    std::vector<std::string> threads{"Main", "Other threads"};

    std::mutex namesMutex;
    // Nodes do not move when the set grows, so the text of a name keeps its address.
    std::unordered_set<std::string> names;
};

State& state()
{
    static State instance;
    return instance;
}

[[nodiscard]] std::uint64_t now() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - state().epoch)
            .count());
}

} // namespace

namespace profiler {

void setEnabled(bool enabled) noexcept
{
    state().enabled.store(enabled, std::memory_order_relaxed);
}

bool isEnabled() noexcept
{
    return state().enabled.load(std::memory_order_relaxed);
}

void setPaused(bool paused) noexcept
{
    state().paused.store(paused, std::memory_order_relaxed);
}

bool isPaused() noexcept
{
    return state().paused.load(std::memory_order_relaxed);
}

void beginFrame()
{
    if (!isEnabled())
    {
        return;
    }
    State& shared = state();
    t_thread.lane = frameThread;
    auto frame = std::make_shared<ProfileFrame>();
    frame->index = shared.frameIndex.fetch_add(1, std::memory_order_relaxed) + 1;
    frame->begin = now();
    const std::scoped_lock lock(shared.mutex);
    shared.current = std::move(frame);
}

void endFrame()
{
    State& shared = state();
    const std::scoped_lock lock(shared.mutex);
    if (!shared.current)
    {
        return;
    }
    shared.current->end = now();
    if (!isPaused())
    {
        shared.history.push_back(std::move(shared.current));
        while (shared.history.size() > keptFrames)
        {
            shared.history.pop_front();
        }
    }
    shared.current.reset();
}

std::uint64_t currentFrame() noexcept
{
    return state().frameIndex.load(std::memory_order_relaxed);
}

void beginZone(const char* name)
{
    if (!isEnabled())
    {
        return;
    }
    t_thread.open.push_back({name, now()});
}

void endZone()
{
    if (t_thread.open.empty())
    {
        return;
    }
    const OpenZone open = t_thread.open.back();
    t_thread.open.pop_back();
    const ProfileZone zone{
        .name = open.name,
        .begin = open.begin,
        .end = now(),
        .depth = static_cast<std::uint16_t>(t_thread.open.size()),
        .thread = t_thread.lane,
    };
    State& shared = state();
    const std::scoped_lock lock(shared.mutex);
    // A zone that ends between two frames belongs to none.
    if (shared.current)
    {
        shared.current->cpu.push_back(zone);
    }
}

void reportGpu(std::uint64_t frame, std::vector<ProfileZone> passes, std::uint64_t duration)
{
    State& shared = state();
    const std::scoped_lock lock(shared.mutex);
    // Recent frames are at the back, and the GPU reports a frame a few frames late.
    for (auto kept = shared.history.rbegin(); kept != shared.history.rend(); ++kept)
    {
        if ((*kept)->index == frame)
        {
            // Those who read the history may hold the frame: they keep the copy they have.
            auto measured = std::make_shared<ProfileFrame>(**kept);
            measured->gpu = std::move(passes);
            measured->gpuDuration = duration;
            measured->gpuMeasured = true;
            *kept = std::move(measured);
            return;
        }
        if ((*kept)->index < frame)
        {
            break;
        }
    }
    if (shared.current && shared.current->index == frame)
    {
        shared.current->gpu = std::move(passes);
        shared.current->gpuDuration = duration;
        shared.current->gpuMeasured = true;
    }
}

const char* intern(std::string_view name)
{
    State& shared = state();
    const std::scoped_lock lock(shared.namesMutex);
    return shared.names.emplace(name).first->c_str();
}

void nameThread(std::string_view name)
{
    State& shared = state();
    const std::scoped_lock lock(shared.mutex);
    shared.threads.emplace_back(name);
    t_thread.lane = static_cast<std::uint16_t>(shared.threads.size() - 1);
}

std::vector<std::string> threadNames()
{
    State& shared = state();
    const std::scoped_lock lock(shared.mutex);
    return shared.threads;
}

std::vector<std::shared_ptr<const ProfileFrame>> history()
{
    State& shared = state();
    const std::scoped_lock lock(shared.mutex);
    return {shared.history.begin(), shared.history.end()};
}

void clear()
{
    State& shared = state();
    const std::scoped_lock lock(shared.mutex);
    shared.history.clear();
}

std::vector<ProfileSummary> summarize(std::span<const ProfileZone> zones)
{
    // Walked in the order the zones began on each thread, a zone sits in the last open one that
    // is shallower and has not ended yet.
    std::vector<const ProfileZone*> sorted;
    sorted.reserve(zones.size());
    for (const ProfileZone& zone : zones)
    {
        sorted.push_back(&zone);
    }
    std::ranges::sort(sorted, {}, [](const ProfileZone* zone) {
        return std::tuple{zone->thread, zone->begin, zone->depth};
    });

    struct Node
    {
        ProfileSummary summary;
        std::uint64_t inChildren = 0;
        std::vector<std::int32_t> children;
    };
    std::vector<Node> nodes;
    std::map<std::tuple<std::int32_t, std::string_view, std::uint16_t>, std::int32_t> found;
    std::vector<std::pair<const ProfileZone*, std::int32_t>> open;
    std::uint16_t thread = 0xFFFF;
    for (const ProfileZone* zone : sorted)
    {
        if (zone->thread != thread)
        {
            open.clear();
            thread = zone->thread;
        }
        while (!open.empty() && (open.back().first->end <= zone->begin || open.back().first->depth >= zone->depth))
        {
            open.pop_back();
        }
        const std::int32_t parent = open.empty() ? -1 : open.back().second;
        const auto key = std::tuple{parent, std::string_view(zone->name), zone->thread};
        auto [place, inserted] = found.try_emplace(key, static_cast<std::int32_t>(nodes.size()));
        if (inserted)
        {
            Node node;
            node.summary = {.name = zone->name,
                            .depth = static_cast<std::uint16_t>(parent < 0 ? 0 : nodes[static_cast<std::size_t>(parent)].summary.depth + 1),
                            .thread = zone->thread,
                            .parent = parent};
            nodes.push_back(std::move(node));
            if (parent >= 0)
            {
                nodes[static_cast<std::size_t>(parent)].children.push_back(place->second);
            }
        }
        Node& node = nodes[static_cast<std::size_t>(place->second)];
        node.summary.inclusive += zone->duration();
        ++node.summary.calls;
        if (parent >= 0)
        {
            nodes[static_cast<std::size_t>(parent)].inChildren += zone->duration();
        }
        open.emplace_back(zone, place->second);
    }

    const auto costlier = [&nodes](std::int32_t a, std::int32_t b) {
        const ProfileSummary& left = nodes[static_cast<std::size_t>(a)].summary;
        const ProfileSummary& right = nodes[static_cast<std::size_t>(b)].summary;
        return std::tuple{left.thread, right.inclusive} < std::tuple{right.thread, left.inclusive};
    };
    std::vector<std::int32_t> roots;
    for (std::size_t index = 0; index < nodes.size(); ++index)
    {
        Node& node = nodes[index];
        node.summary.exclusive =
            node.summary.inclusive > node.inChildren ? node.summary.inclusive - node.inChildren : 0;
        std::ranges::sort(node.children, costlier);
        if (node.summary.parent < 0)
        {
            roots.push_back(static_cast<std::int32_t>(index));
        }
    }
    std::ranges::sort(roots, costlier);

    // Depth first, so that each line comes right after the one it sits in.
    std::vector<ProfileSummary> lines;
    lines.reserve(nodes.size());
    const auto visit = [&](auto&& self, std::int32_t index, std::int32_t parentLine) -> void {
        ProfileSummary line = nodes[static_cast<std::size_t>(index)].summary;
        line.parent = parentLine;
        const auto here = static_cast<std::int32_t>(lines.size());
        lines.push_back(line);
        for (const std::int32_t child : nodes[static_cast<std::size_t>(index)].children)
        {
            self(self, child, here);
        }
    };
    for (const std::int32_t root : roots)
    {
        visit(visit, root, -1);
    }
    return lines;
}

} // namespace profiler

ProfileScope::ProfileScope(const char* name) noexcept
{
    if (profiler::isEnabled())
    {
        profiler::beginZone(name);
        m_active = true;
    }
}

ProfileScope::~ProfileScope()
{
    if (m_active)
    {
        profiler::endZone();
    }
}

} // namespace devex::core
