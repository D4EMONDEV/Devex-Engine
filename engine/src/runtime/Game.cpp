#include <devex/core/Profiler.hpp>
#include <devex/runtime/Game.hpp>

#include <algorithm>
#include <utility>

namespace devex::runtime {

void GameRegistry::system(std::string name, SystemPhase phase, SystemFunction function, int order)
{
    if (function == nullptr)
    {
        return;
    }
    const SystemInfo system{.name = std::move(name), .phase = phase, .order = order, .function = function};
    // Inserted after the systems that come before it or tie with it, which keeps registration order.
    const auto position = std::ranges::upper_bound(m_systems, system, [](const SystemInfo& left, const SystemInfo& right) {
        return std::pair(left.phase, left.order) < std::pair(right.phase, right.order);
    });
    m_systems.insert(position, system);
}

std::span<const std::string> GameRegistry::components() const noexcept
{
    return m_components;
}

std::span<const SystemInfo> GameRegistry::systems() const noexcept
{
    return m_systems;
}

void GameRegistry::run(SystemPhase phase, SystemContext& context) const
{
    for (const SystemInfo& system : m_systems)
    {
        if (system.phase == phase)
        {
            // Every system has its zone, under the name it was registered with.
            const core::ProfileScope scope(core::profiler::isEnabled() ? core::profiler::intern(system.name) : "");
            system.function(context);
        }
    }
}

} // namespace devex::runtime
