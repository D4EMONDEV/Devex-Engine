#pragma once

#include <devex/core/Export.hpp>

#include <devex/animation/TweenWorld.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/runtime/Game.hpp>
#include <devex/scene/Entity.hpp>

#include <coroutine>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

// Coroutines of C++ game code: functions that wait for time, frames, conditions or tweens in the
// middle of what they do, as a cutscene or a blinking light does:
//
//     devex::runtime::Coroutine blink(devex::runtime::CoroutineContext& co, devex::scene::Entity lamp)
//     {
//         for (int flash = 0; flash < 3; ++flash)
//         {
//             devex::runtime::SystemContext& frame = co_await co.wait(0.5f);
//             frame.scene.get<devex::scene::PointLight>(lamp).intensity = flash % 2 == 0 ? 0.0f : 800.0f;
//         }
//     }
//
//     context.coroutines->start(context, [lamp](auto& co) { return blink(co, lamp); }, lamp);
//
// The SystemContext of a frame only lives during that frame: a coroutine takes the one each co_await
// returns. Coroutines run during Update; they end with the entity that owns them, when the scene is
// replaced, when the game stops, and before the game module reloads.
namespace devex::runtime {

class CoroutineScheduler;

class DEVEX_API [[nodiscard]] Coroutine
{
public:
    struct DEVEX_API promise_type
    {
        std::exception_ptr exception;

        Coroutine get_return_object() noexcept
        {
            return Coroutine{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // Runs at once, until its first co_await.
        std::suspend_never initial_suspend() noexcept
        {
            return {};
        }
        // Kept until the scheduler sees it is over.
        std::suspend_always final_suspend() noexcept
        {
            return {};
        }
        void return_void() noexcept
        {
        }
        void unhandled_exception() noexcept
        {
            exception = std::current_exception();
        }
    };

    Coroutine(Coroutine&& other) noexcept
        : m_handle(std::exchange(other.m_handle, nullptr))
    {
    }
    Coroutine& operator=(Coroutine&& other) noexcept
    {
        if (this != &other)
        {
            reset();
            m_handle = std::exchange(other.m_handle, nullptr);
        }
        return *this;
    }
    Coroutine(const Coroutine&) = delete;
    Coroutine& operator=(const Coroutine&) = delete;
    ~Coroutine()
    {
        reset();
    }

    [[nodiscard]] std::coroutine_handle<promise_type> release() noexcept
    {
        return std::exchange(m_handle, nullptr);
    }

private:
    explicit Coroutine(std::coroutine_handle<promise_type> handle) noexcept
        : m_handle(handle)
    {
    }

    void reset() noexcept
    {
        if (m_handle)
        {
            m_handle.destroy();
            m_handle = nullptr;
        }
    }

    std::coroutine_handle<promise_type> m_handle;
};

// What a coroutine waits for.
struct DEVEX_API CoroutineWait
{
    enum class Kind : std::uint8_t
    {
        NextFrame,
        Seconds,
        Until,
        Tween,
    };
    Kind kind = Kind::NextFrame;
    float seconds = 0.0f;
    std::function<bool(SystemContext& frame)> until;
    animation::TweenHandle tween;
};

// What a coroutine knows of the game while it runs: the frame it resumes in, and how to wait.
class DEVEX_API CoroutineContext
{
public:
    class DEVEX_API Awaitable
    {
    public:
        Awaitable(CoroutineContext& context, CoroutineWait wait) noexcept
            : m_context(context)
            , m_wait(std::move(wait))
        {
        }

        [[nodiscard]] bool await_ready() const noexcept
        {
            return false;
        }
        void await_suspend(std::coroutine_handle<>) noexcept
        {
            m_context.m_wait = std::move(m_wait);
        }
        // The frame the coroutine resumes in.
        SystemContext& await_resume() const noexcept
        {
            return *m_context.m_frame;
        }

    private:
        CoroutineContext& m_context;
        CoroutineWait m_wait;
    };

    // Waits for the next frame.
    [[nodiscard]] Awaitable nextFrame()
    {
        return {*this, {.kind = CoroutineWait::Kind::NextFrame}};
    }
    // Waits for seconds of the game.
    [[nodiscard]] Awaitable wait(float seconds)
    {
        return {*this, {.kind = CoroutineWait::Kind::Seconds, .seconds = seconds}};
    }
    // Waits until the condition holds, asked once per frame.
    [[nodiscard]] Awaitable until(std::function<bool(SystemContext& frame)> condition)
    {
        return {*this, {.kind = CoroutineWait::Kind::Until, .until = std::move(condition)}};
    }
    // Waits for a tween or a sequence to end.
    [[nodiscard]] Awaitable tween(animation::TweenHandle handle)
    {
        return {*this, {.kind = CoroutineWait::Kind::Tween, .tween = handle}};
    }

    // The frame the coroutine runs in now.
    [[nodiscard]] SystemContext& frame() const noexcept
    {
        return *m_frame;
    }
    // The entity whose end ends the coroutine; invalid for none.
    [[nodiscard]] scene::Entity owner() const noexcept
    {
        return m_owner;
    }

private:
    friend class CoroutineScheduler;

    SystemContext* m_frame = nullptr;
    scene::Entity m_owner;
    CoroutineWait m_wait;
};

struct DEVEX_API CoroutineHandle
{
    std::uint64_t id = 0;

    [[nodiscard]] bool isValid() const noexcept
    {
        return id != 0;
    }
};

// Runs the coroutines of a game: each frame, those whose wait is over go on until their next one.
class DEVEX_API CoroutineScheduler
{
public:
    using Function = std::function<Coroutine(CoroutineContext& context)>;

    CoroutineScheduler();
    ~CoroutineScheduler();

    CoroutineScheduler(const CoroutineScheduler&) = delete;
    CoroutineScheduler& operator=(const CoroutineScheduler&) = delete;

    // Runs the coroutine at once until its first wait. It ends with the owner when one is given.
    // The function is kept while the coroutine runs, so that a lambda that is itself a coroutine
    // keeps its captures.
    CoroutineHandle start(SystemContext& frame, Function function, scene::Entity owner = {});
    // Ends a coroutine where it waits.
    void stop(CoroutineHandle handle);
    [[nodiscard]] bool isRunning(CoroutineHandle handle) const noexcept;

    // Once per frame, during Update.
    void update(SystemContext& frame);
    // Ends every coroutine, as a new scene, the end of the game and a reload of the module do.
    void clear();
    [[nodiscard]] std::size_t count() const noexcept;

private:
    struct DEVEX_API Entry
    {
        std::uint64_t id = 0;
        Function function;
        std::coroutine_handle<Coroutine::promise_type> handle;
        std::unique_ptr<CoroutineContext> context;
        core::Uuid owner;
        bool stopped = false;
    };

    // True once the coroutine is over, and its frame destroyed.
    bool finishIfDone(Entry& entry);

    std::vector<std::unique_ptr<Entry>> m_entries;
    std::uint64_t m_nextId = 1;
};

} // namespace devex::runtime
