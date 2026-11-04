#pragma once

#include <devex/core/Export.hpp>
#include <devex/core/Time.hpp>
#include <devex/net/Multiplayer.hpp>
#include <devex/scene/Scene.hpp>

#include <memory>

namespace devex::runtime {

// What keeps the scenes of a session alike, through the NetworkSpawner and NetworkSync components: the
// objects that appear and go, and the fields that follow, from whoever has authority over them. The host
// checks what clients send and passes it on, and tells those who join what there is. Nothing is predicted:
// a player moves what it has authority over at once, and sees the others slightly late, smoothed. For a
// client, what appeared during a session goes with it.
class DEVEX_API NetworkReplication
{
public:
    explicit NetworkReplication(net::Multiplayer& multiplayer);
    ~NetworkReplication();

    NetworkReplication(const NetworkReplication&) = delete;
    NetworkReplication& operator=(const NetworkReplication&) = delete;

    // After the session was polled and before the frame runs: what arrived enters the scene.
    void receive(scene::Scene& scene, core::Duration delta);
    // After the frame ran and before the session sends: what changed here goes out.
    void send(scene::Scene& scene, core::Duration delta);
    // Another scene replaced the one it knew about: what it knew is gone with it.
    void sceneChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

// Gives an entity and what is under it to a player, which only the host can do.
DEVEX_API void setAuthority(const scene::Scene& scene, net::Multiplayer& multiplayer, scene::Entity entity, net::PeerId peer);

} // namespace devex::runtime
