#include <devex/runtime/Coroutine.hpp>

#include <devex/core/Log.hpp>

#include <algorithm>
#include <exception>

namespace devex::runtime {

CoroutineScheduler::CoroutineScheduler() = default;

CoroutineScheduler::~CoroutineScheduler()
{
    clear();
}

bool CoroutineScheduler::finishIfDone(Entry& entry)
{
    if (!entry.handle || (!entry.handle.done() && !entry.stopped))
    {
        return false;
    }
    if (entry.handle.done())
    {
        if (const std::exception_ptr exception = entry.handle.promise().exception)
        {
            try
            {
                std::rethrow_exception(exception);
            }
            catch (const std::exception& error)
            {
                DEVEX_LOG_ERROR("A coroutine ended with an error: {}", error.what());
            }
            catch (...)
            {
                DEVEX_LOG_ERROR("A coroutine ended with an error");
            }
        }
    }
    entry.handle.destroy();
    entry.handle = nullptr;
    return true;
}

CoroutineHandle CoroutineScheduler::start(SystemContext& frame, Function function, scene::Entity owner)
{
    if (!function)
    {
        return {};
    }
    auto entry = std::make_unique<Entry>();
    entry->id = m_nextId++;
    entry->function = std::move(function);
    entry->context = std::make_unique<CoroutineContext>();
    entry->context->m_frame = &frame;
    entry->context->m_owner = owner;
    entry->owner = owner.isValid() && frame.scene.isAlive(owner) ? frame.scene.uuid(owner) : core::Uuid{};
    entry->handle = entry->function(*entry->context).release();
    const CoroutineHandle handle{entry->id};
    if (!finishIfDone(*entry))
    {
        m_entries.push_back(std::move(entry));
    }
    return handle;
}

void CoroutineScheduler::stop(CoroutineHandle handle)
{
    for (const std::unique_ptr<Entry>& entry : m_entries)
    {
        if (entry->id == handle.id)
        {
            entry->stopped = true;
        }
    }
}

bool CoroutineScheduler::isRunning(CoroutineHandle handle) const noexcept
{
    return std::ranges::any_of(m_entries, [&](const std::unique_ptr<Entry>& entry) {
        return entry->id == handle.id && !entry->stopped && entry->handle;
    });
}

void CoroutineScheduler::update(SystemContext& frame)
{
    const auto seconds = static_cast<float>(frame.delta.count());
    // Coroutines started while others run wait for the next frame.
    const std::size_t count = m_entries.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        Entry& entry = *m_entries[index];
        if (!entry.handle || entry.stopped)
        {
            continue;
        }
        if (!entry.owner.isNil() && !frame.scene.findEntity(entry.owner).isValid())
        {
            entry.stopped = true;
            continue;
        }
        CoroutineContext& context = *entry.context;
        context.m_frame = &frame;
        CoroutineWait& wait = context.m_wait;
        bool ready = false;
        switch (wait.kind)
        {
        case CoroutineWait::Kind::NextFrame:
            ready = true;
            break;
        case CoroutineWait::Kind::Seconds:
            wait.seconds -= seconds;
            ready = wait.seconds <= 0.0f;
            break;
        case CoroutineWait::Kind::Until:
            ready = !wait.until || wait.until(frame);
            break;
        case CoroutineWait::Kind::Tween:
            ready = frame.tweens == nullptr || !frame.tweens->isPlaying(wait.tween);
            break;
        }
        if (ready)
        {
            entry.handle.resume();
        }
    }
    std::erase_if(m_entries, [this](std::unique_ptr<Entry>& entry) { return !entry->handle || finishIfDone(*entry); });
}

void CoroutineScheduler::clear()
{
    // Destroying a frame runs the destructors of its locals, which may start other coroutines.
    std::vector<std::unique_ptr<Entry>> entries = std::exchange(m_entries, {});
    for (const std::unique_ptr<Entry>& entry : entries)
    {
        if (entry->handle)
        {
            entry->handle.destroy();
        }
    }
    m_entries.clear();
}

std::size_t CoroutineScheduler::count() const noexcept
{
    return m_entries.size();
}

} // namespace devex::runtime
