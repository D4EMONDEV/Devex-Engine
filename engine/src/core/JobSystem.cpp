#include <devex/core/Assert.hpp>
#include <devex/core/JobSystem.hpp>

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

namespace devex::core {
namespace {

thread_local bool onWorkerThread = false;

// Shared by the calling thread and the helper jobs of one parallelFor. Helpers may start after
// the loop has finished: they then find no index left and never touch the body.
struct ParallelLoop
{
    std::size_t count = 0;
    const std::function<void(std::size_t)>* body = nullptr;
    std::atomic<std::size_t> nextIndex{0};
    std::atomic<std::uint32_t> activeHelpers{0};

    void run()
    {
        for (std::size_t index = nextIndex.fetch_add(1); index < count;
             index = nextIndex.fetch_add(1))
        {
            (*body)(index);
        }
    }
};

} // namespace

JobSystem::JobSystem(std::uint32_t workerCount)
{
    if (workerCount == 0)
    {
        workerCount = std::max(std::thread::hardware_concurrency(), 2u) - 1;
    }
    m_workers.reserve(workerCount);
    for (std::uint32_t worker = 0; worker < workerCount; ++worker)
    {
        m_workers.emplace_back([this](std::stop_token stop) { runWorker(stop); });
    }
}

JobSystem::~JobSystem()
{
    for (std::jthread& worker : m_workers)
    {
        worker.request_stop();
    }
    // Joins the workers; each one finishes its current job first.
    m_workers.clear();
}

std::uint32_t JobSystem::workerCount() const noexcept
{
    return static_cast<std::uint32_t>(m_workers.size());
}

void JobSystem::schedule(std::function<void()> job)
{
    DEVEX_ASSERT(job != nullptr);
    {
        const std::scoped_lock lock(m_mutex);
        m_queue.push_back(std::move(job));
    }
    m_jobAvailable.notify_one();
}

void JobSystem::parallelFor(std::size_t count, const std::function<void(std::size_t index)>& body)
{
    if (count == 0)
    {
        return;
    }
    if (count == 1 || m_workers.empty())
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            body(index);
        }
        return;
    }

    auto loop = std::make_shared<ParallelLoop>();
    loop->count = count;
    loop->body = &body;

    const std::size_t helpers = std::min<std::size_t>(m_workers.size(), count - 1);
    for (std::size_t helper = 0; helper < helpers; ++helper)
    {
        schedule([loop] {
            loop->activeHelpers.fetch_add(1);
            loop->run();
            if (loop->activeHelpers.fetch_sub(1) == 1)
            {
                loop->activeHelpers.notify_all();
            }
        });
    }

    loop->run();
    // Every index is taken; wait for the helpers still running one.
    for (std::uint32_t active = loop->activeHelpers.load(); active != 0;
         active = loop->activeHelpers.load())
    {
        loop->activeHelpers.wait(active);
    }
}

void JobSystem::waitIdle()
{
    DEVEX_ASSERT_MSG(!onWorkerThread, "waitIdle would wait for the job calling it");
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this] { return m_queue.empty() && m_runningJobs == 0; });
}

bool JobSystem::isWorkerThread() noexcept
{
    return onWorkerThread;
}

void JobSystem::runWorker(std::stop_token stop)
{
    onWorkerThread = true;
    std::unique_lock lock(m_mutex);
    while (true)
    {
        m_jobAvailable.wait(lock, stop, [this] { return !m_queue.empty(); });
        if (stop.stop_requested())
        {
            return;
        }

        std::function<void()> job = std::move(m_queue.front());
        m_queue.pop_front();
        ++m_runningJobs;
        lock.unlock();

        job();
        // Captured state is released outside the lock.
        job = nullptr;

        lock.lock();
        --m_runningJobs;
        if (m_queue.empty() && m_runningJobs == 0)
        {
            m_idle.notify_all();
        }
    }
}

} // namespace devex::core
