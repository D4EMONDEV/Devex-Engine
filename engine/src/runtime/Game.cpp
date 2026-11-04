#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/runtime/Game.hpp>

#include <algorithm>
#include <exception>
#include <memory>
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

void GameRegistry::addRpc(RpcInfo rpc)
{
    if (const auto found = std::ranges::find(m_rpcs, rpc.name, &RpcInfo::name); found != m_rpcs.end())
    {
        DEVEX_LOG_WARNING("Two remote methods are named {}: the last one is used", rpc.name);
        *found = std::move(rpc);
        return;
    }
    m_rpcs.push_back(std::move(rpc));
}

std::span<const RpcInfo> GameRegistry::rpcs() const noexcept
{
    return m_rpcs;
}

const RpcInfo* GameRegistry::findRpc(std::string_view name) const noexcept
{
    const auto found = std::ranges::find(m_rpcs, name, &RpcInfo::name);
    return found != m_rpcs.end() ? std::to_address(found) : nullptr;
}

void GameRegistry::runRpc(SystemContext& context, const net::RpcCall& call, net::PeerId sender) const
{
    const RpcInfo* const rpc = findRpc(call.name);
    if (rpc == nullptr || context.multiplayer == nullptr)
    {
        return;
    }
    const scene::Entity entity = context.scene.findEntity(call.target);
    if (!context.scene.isAlive(entity))
    {
        DEVEX_LOG_WARNING("The remote call {} from player {} is for an entity this game does not have", call.name, sender);
        return;
    }
    // Back to what it was after: a method can call another that runs here.
    const net::PeerId previous = context.multiplayer->remoteSender();
    context.multiplayer->setRemoteSender(sender);
    try
    {
        if (!rpc->function(context, entity, call.arguments))
        {
            DEVEX_LOG_ERROR("The remote call {} from player {} does not carry what the method takes", call.name, sender);
        }
    }
    catch (const std::exception& error)
    {
        DEVEX_LOG_ERROR("The remote method {} threw an exception: {}", call.name, error.what());
    }
    catch (...)
    {
        DEVEX_LOG_ERROR("The remote method {} threw an exception", call.name);
    }
    context.multiplayer->setRemoteSender(previous);
}

void GameRegistry::dispatchRpcs(SystemContext& context) const
{
    if (context.multiplayer == nullptr || m_rpcs.empty())
    {
        return;
    }
    for (const net::NetworkEvent& event : context.multiplayer->events())
    {
        if (event.kind != net::NetworkEvent::Kind::Rpc)
        {
            continue;
        }
        const std::optional<net::RpcHeader> header = net::decodeRpcHeader(event.bytes);
        if (!header || findRpc(header->name) == nullptr)
        {
            continue;
        }
        const core::Result<net::RpcCall> call = net::decodeRpc(event.bytes);
        if (!call)
        {
            DEVEX_LOG_WARNING("Refused a remote call from player {}: {}", event.peer, call.error());
            continue;
        }
        runRpc(context, *call, event.peer);
    }
}

void sendRpc(SystemContext& context, net::PeerId to, scene::Entity target, std::string_view name, std::span<const net::RpcValue> arguments)
{
    if (context.multiplayer == nullptr)
    {
        DEVEX_LOG_ERROR("Cannot call {} over the network outside a game", name);
        return;
    }
    if (!context.scene.isAlive(target))
    {
        DEVEX_LOG_ERROR("Cannot call {} for an entity that is not in the scene", name);
        return;
    }
    const net::RpcCall call{.target = context.scene.uuid(target), .name = std::string(name), .arguments = {arguments.begin(), arguments.end()}};
    const core::Result<std::vector<std::byte>> bytes = net::encodeRpc(call);
    if (!bytes)
    {
        DEVEX_LOG_ERROR("{}", bytes.error());
        return;
    }
    if (!context.multiplayer->sendRpc(to, *bytes))
    {
        return;
    }
    if (context.game != nullptr && context.game->findRpc(name) != nullptr)
    {
        context.game->runRpc(context, call, context.multiplayer->caller());
    }
    else
    {
        // A method of C#, whose components are only in its hands during its own phases.
        context.multiplayer->loopbackRpc(*bytes);
    }
}

void GameRegistry::run(SystemPhase phase, SystemContext& context) const
{
    for (const SystemInfo& system : m_systems)
    {
        if (system.phase == phase)
        {
            // Every system has its zone, under the name it was registered with.
            const core::ProfileScope scope(core::profiler::isEnabled() ? core::profiler::intern(system.name) : "");
            // An exception is reported and the other systems run, as with the systems of C#.
            try
            {
                system.function(context);
            }
            catch (const std::exception& error)
            {
                DEVEX_LOG_ERROR("The system {} threw an exception: {}", system.name, error.what());
            }
            catch (...)
            {
                DEVEX_LOG_ERROR("The system {} threw an exception", system.name);
            }
        }
    }
}

} // namespace devex::runtime
