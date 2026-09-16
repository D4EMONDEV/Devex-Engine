#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace devex::core {

// A pool of worker threads for work that must not block the main loop, such as asset imports.
// Jobs start in the order they were scheduled.
class JobSystem
{
public:
    // A worker count of 0 uses every hardware thread but one, and at least one worker.
    explicit JobSystem(std::uint32_t workerCount = 0);
    // Waits for the running jobs to return. Jobs that have not started are discarded.
    ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;

    [[nodiscard]] std::uint32_t workerCount() const noexcept;

    void schedule(std::function<void()> job);

    // Calls body(index) for every index in [0, count) on the workers and the calling thread, and
    // returns once every call has returned. It may be called from a job: the calling thread keeps
    // working instead of waiting for busy workers.
    void parallelFor(std::size_t count, const std::function<void(std::size_t index)>& body);

    // Blocks until no job is queued or running. Must not be called from a job.
    void waitIdle();

    // True on the worker threads of any JobSystem.
    [[nodiscard]] static bool isWorkerThread() noexcept;

private:
    void runWorker(std::stop_token stop);

    std::mutex m_mutex;
    std::condition_variable_any m_jobAvailable;
    std::condition_variable m_idle;
    std::deque<std::function<void()>> m_queue;
    std::size_t m_runningJobs = 0;
    // Declared last so that the workers stop before the state they use is destroyed.
    std::vector<std::jthread> m_workers;
};

} // namespace devex::core
