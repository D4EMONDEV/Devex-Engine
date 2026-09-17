// A game module for the runtime tests: counters that systems start and advance.
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>

#include <cstdint>

namespace {

struct Counter
{
    std::int32_t value = 0;
    std::int32_t step = 1;
};

// Reflection is found by argument-dependent lookup, in the namespace of the type.
DEVEX_DECLARE_REFLECTION(Counter);
DEVEX_REFLECT(Counter)
{
    type.field("value", &Counter::value);
    type.field("step", &Counter::step);
}

// A component whose type is known to this module only.
struct Started
{
};

void start(devex::runtime::SystemContext& context)
{
    // A pool of an engine type created by this module's code.
    for ([[maybe_unused]] auto [entity, counter] : context.scene.view<Counter>())
    {
        if (!context.scene.has<devex::scene::Transform>(entity))
        {
            context.scene.add<devex::scene::Transform>(entity);
        }
        context.scene.add<Started>(entity);
    }
}

void count(devex::runtime::SystemContext& context)
{
    for ([[maybe_unused]] auto [entity, counter] : context.scene.view<Counter>())
    {
        counter.value += counter.step;
    }
}

void doubleAfterCounting(devex::runtime::SystemContext& context)
{
    for ([[maybe_unused]] auto [entity, counter] : context.scene.view<Counter>())
    {
        if (counter.value >= 100)
        {
            context.quitRequested = true;
        }
    }
}

} // namespace

DEVEX_GAME_MODULE(game)
{
    game.component<Counter>();
    game.system("Check", devex::runtime::SystemPhase::FixedUpdate, &doubleAfterCounting, 10);
    game.system("Count", devex::runtime::SystemPhase::FixedUpdate, &count);
    game.system("Start counters", devex::runtime::SystemPhase::Start, &start);
}
