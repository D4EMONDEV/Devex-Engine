#include <devex/runtime/NetworkReplication.hpp>

#include <devex/core/Log.hpp>
#include <devex/math/Math.hpp>
#include <devex/net/Bytes.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/EntityRef.hpp>
#include <devex/scene/NetworkComponents.hpp>
#include <devex/scene/Physics2DComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace devex::runtime {

namespace {

using net::ByteReader;
using net::ByteWriter;
using net::PeerId;
using reflection::ValueKind;
using scene::Entity;
using scene::NetworkSync;

// The first byte of what the replication sends.
enum class Message : std::uint8_t
{
    // The spawner, the prefab, the instance, then its transform.
    Spawn,
    // The spawner, then the instance.
    Despawn,
    // The entity, a number that grows, the time of the sender, what tells the list of fields, how many
    // fields follow, then each: its place in the list and its value.
    Values,
};

constexpr std::size_t maxSnapshots = 64;
// Further than this from where it should be, in seconds, what is shown jumps rather than catching up.
constexpr double catchUpLimit = 0.5;
constexpr double catchUpSpeed = 2.0;

// A position, a rotation and a scale at a time of the sender, as much as the entity synchronizes.
struct Snapshot
{
    double time = 0.0;
    std::optional<math::Vec3> position;
    std::optional<math::Quat> rotation;
    std::optional<math::Vec3> scale;
};

// What the replication knows of an entity with a NetworkSync.
struct Tracked
{
    // As its authority: the values last sent, whether the others surely have them, and when to look again.
    std::vector<std::byte> sent;
    bool settled = true;
    double wait = 0.0;
    std::uint32_t sequence = 0;
    // As one of the others: the latest values taken, from which authority.
    std::uint32_t lastSequence = 0;
    double lastTime = 0.0;
    std::optional<PeerId> lastAuthority;
    std::deque<Snapshot> snapshots;
    double playback = -1.0;
    bool warnedFields = false;
    // A body that the others do not simulate: it follows what they receive, pushing what it meets.
    bool bodyWasDynamic = false;
    bool body2DWasDynamic = false;
    // Likewise a character, which does not fall here: the gravity it has when it is its own again.
    std::optional<float> characterGravity;
    std::optional<float> character2DGravity;
};

// A dynamic body goes kinematic while another player has authority over it, and back.
template <typename Body>
void followBodyOf(scene::Scene& scene, Entity entity, bool moves, bool own, bool& wasDynamic)
{
    Body* const body = scene.tryGet<Body>(entity);
    if (body == nullptr)
    {
        return;
    }
    if (moves && !own && body->type == scene::BodyType::Dynamic)
    {
        body->type = scene::BodyType::Kinematic;
        wasDynamic = true;
    }
    else if (own && wasDynamic)
    {
        body->type = scene::BodyType::Dynamic;
        wasDynamic = false;
    }
}

// A character neither walks nor falls while another player has authority over it, and takes up again.
template <typename Character>
void followCharacterOf(scene::Scene& scene, Entity entity, bool moves, bool own, std::optional<float>& gravity)
{
    Character* const character = scene.tryGet<Character>(entity);
    if (character == nullptr)
    {
        return;
    }
    if (moves && !own)
    {
        if (!gravity)
        {
            gravity = character->gravityScale;
        }
        character->gravityScale = 0.0f;
        character->velocity = {};
    }
    else if (own && gravity)
    {
        character->gravityScale = *gravity;
        gravity.reset();
    }
}

struct Spawned
{
    core::Uuid spawner;
    asset::AssetId prefab;
    core::Uuid instance;
};

[[nodiscard]] std::uint32_t namesOf(const std::vector<std::string>& fields) noexcept
{
    // FNV-1a: that both sides have the same list, not a secret.
    std::uint32_t hash = 2166136261u;
    for (const std::string& field : fields)
    {
        for (const char character : field)
        {
            hash = (hash ^ static_cast<std::uint8_t>(character)) * 16777619u;
        }
        hash = (hash ^ 0xffu) * 16777619u;
    }
    return hash;
}

// "Transform.position" as its component type and its field, when the game has them and they can travel.
struct Binding
{
    const scene::ComponentType* type = nullptr;
    const reflection::FieldInfo* field = nullptr;
};

[[nodiscard]] Binding bindingOf(std::string_view name) noexcept
{
    const std::size_t dot = name.find('.');
    if (dot == std::string_view::npos)
    {
        return {};
    }
    const scene::ComponentType* const type = scene::componentRegistry().find(name.substr(0, dot));
    const reflection::FieldInfo* const field = type != nullptr ? type->type->findField(name.substr(dot + 1)) : nullptr;
    return field != nullptr && field->list == nullptr ? Binding{type, field} : Binding{};
}

void writeValue(ByteWriter& writer, const reflection::FieldInfo& field, const void* address)
{
    switch (field.kind)
    {
    case ValueKind::Bool:
        writer.number(*static_cast<const bool*>(address) ? 1 : 0, 1);
        break;
    case ValueKind::Int32:
    case ValueKind::UInt32:
        writer.number(*static_cast<const std::uint32_t*>(address), sizeof(std::uint32_t));
        break;
    case ValueKind::Float:
    case ValueKind::Vec2:
    case ValueKind::Vec3:
    case ValueKind::Vec4:
    case ValueKind::Quat: {
        const std::size_t count = field.kind == ValueKind::Float  ? 1
                                  : field.kind == ValueKind::Vec2 ? 2
                                  : field.kind == ValueKind::Vec3 ? 3
                                                                  : 4;
        for (std::size_t index = 0; index < count; ++index)
        {
            writer.decimal(static_cast<const float*>(address)[index]);
        }
        break;
    }
    case ValueKind::String:
        writer.text(*static_cast<const std::string*>(address));
        break;
    case ValueKind::Uuid:
        writer.uuid(*static_cast<const core::Uuid*>(address));
        break;
    case ValueKind::AssetId:
        writer.uuid(static_cast<const asset::AssetId*>(address)->uuid);
        break;
    case ValueKind::Entity:
        writer.uuid(static_cast<const scene::EntityRef*>(address)->uuid);
        break;
    case ValueKind::Enum:
        writer.number(field.enumSize == 1 ? *static_cast<const std::uint8_t*>(address) : *static_cast<const std::uint32_t*>(address), field.enumSize);
        break;
    }
}

// Into the field at the address; with none, it only goes past the value.
void readValue(ByteReader& reader, const reflection::FieldInfo& field, void* address)
{
    switch (field.kind)
    {
    case ValueKind::Bool: {
        const bool value = reader.number(1) != 0;
        if (address != nullptr && !reader.failed())
        {
            *static_cast<bool*>(address) = value;
        }
        break;
    }
    case ValueKind::Int32:
    case ValueKind::UInt32: {
        const auto value = static_cast<std::uint32_t>(reader.number(sizeof(std::uint32_t)));
        if (address != nullptr && !reader.failed())
        {
            *static_cast<std::uint32_t*>(address) = value;
        }
        break;
    }
    case ValueKind::Float:
    case ValueKind::Vec2:
    case ValueKind::Vec3:
    case ValueKind::Vec4:
    case ValueKind::Quat: {
        const std::size_t count = field.kind == ValueKind::Float  ? 1
                                  : field.kind == ValueKind::Vec2 ? 2
                                  : field.kind == ValueKind::Vec3 ? 3
                                                                  : 4;
        for (std::size_t index = 0; index < count; ++index)
        {
            const float value = reader.decimal();
            if (address != nullptr && !reader.failed())
            {
                static_cast<float*>(address)[index] = value;
            }
        }
        break;
    }
    case ValueKind::String: {
        const std::string_view value = reader.text();
        if (address != nullptr && !reader.failed())
        {
            static_cast<std::string*>(address)->assign(value);
        }
        break;
    }
    case ValueKind::Uuid:
    case ValueKind::AssetId:
    case ValueKind::Entity: {
        const core::Uuid value = reader.uuid();
        if (address != nullptr && !reader.failed())
        {
            if (field.kind == ValueKind::Uuid)
            {
                *static_cast<core::Uuid*>(address) = value;
            }
            else if (field.kind == ValueKind::AssetId)
            {
                static_cast<asset::AssetId*>(address)->uuid = value;
            }
            else
            {
                static_cast<scene::EntityRef*>(address)->uuid = value;
            }
        }
        break;
    }
    case ValueKind::Enum: {
        const std::uint64_t value = reader.number(field.enumSize);
        if (address != nullptr && !reader.failed())
        {
            if (field.enumSize == 1)
            {
                *static_cast<std::uint8_t*>(address) = static_cast<std::uint8_t>(value);
            }
            else
            {
                *static_cast<std::uint32_t*>(address) = static_cast<std::uint32_t>(value);
            }
        }
        break;
    }
    }
}

// The values of an entity, as many as it has the components for.
[[nodiscard]] std::vector<std::byte> valuesOf(const scene::Scene& scene, Entity entity, const NetworkSync& sync)
{
    std::vector<std::byte> values;
    ByteWriter writer(values);
    const std::size_t count = std::min<std::size_t>(sync.fields.size(), 255);
    writer.number(0, 1);
    std::size_t written = 0;
    for (std::size_t index = 0; index < count; ++index)
    {
        const Binding binding = bindingOf(sync.fields[index]);
        const void* const component = binding.type != nullptr ? binding.type->find(scene, entity) : nullptr;
        if (component != nullptr)
        {
            writer.number(index, 1);
            writeValue(writer, *binding.field, binding.field->address(component));
            ++written;
        }
    }
    values[0] = static_cast<std::byte>(written);
    return values;
}

[[nodiscard]] std::vector<std::byte> valuesMessage(const core::Uuid& entity, std::uint32_t sequence, double time, const NetworkSync& sync,
                                                   std::span<const std::byte> values)
{
    std::vector<std::byte> message{static_cast<std::byte>(Message::Values)};
    ByteWriter writer(message);
    writer.uuid(entity);
    writer.number(sequence, sizeof(std::uint32_t));
    writer.wide(time);
    writer.number(namesOf(sync.fields), sizeof(std::uint32_t));
    writer.raw(values);
    return message;
}

void writeTransform(ByteWriter& writer, const scene::Transform& transform)
{
    for (const float value : {transform.position.x, transform.position.y, transform.position.z, transform.rotation.x, transform.rotation.y,
                              transform.rotation.z, transform.rotation.w, transform.scale.x, transform.scale.y, transform.scale.z})
    {
        writer.decimal(value);
    }
}

[[nodiscard]] scene::Transform readTransform(ByteReader& reader) noexcept
{
    scene::Transform transform;
    transform.position.x = reader.decimal();
    transform.position.y = reader.decimal();
    transform.position.z = reader.decimal();
    transform.rotation.x = reader.decimal();
    transform.rotation.y = reader.decimal();
    transform.rotation.z = reader.decimal();
    transform.rotation.w = reader.decimal();
    transform.scale.x = reader.decimal();
    transform.scale.y = reader.decimal();
    transform.scale.z = reader.decimal();
    return transform;
}

} // namespace

struct NetworkReplication::Impl
{
    explicit Impl(net::Multiplayer& session)
        : multiplayer(session)
    {
    }

    net::Multiplayer& multiplayer;
    // In the order they appeared, as one can be under another.
    std::vector<Spawned> spawned;
    std::unordered_set<core::Uuid> known;
    std::unordered_map<core::Uuid, Tracked> tracked;
    double clock = 0.0;
    bool wasClient = false;

    // The session ended for a client: joining again gets what there is then.
    void leftSession(scene::Scene& scene)
    {
        for (const Spawned& entry : spawned)
        {
            if (const Entity instance = scene.findEntity(entry.instance); scene.isAlive(instance))
            {
                scene.destroyEntity(instance);
            }
        }
        spawned.clear();
        known.clear();
        // The bodies it does not simulate are its own again.
        for (auto& [uuid, tracking] : tracked)
        {
            tracking.lastSequence = 0;
            tracking.lastAuthority.reset();
            tracking.snapshots.clear();
            tracking.playback = -1.0;
            tracking.sent.clear();
            tracking.settled = true;
        }
    }

    void forget(const core::Uuid& instance)
    {
        known.erase(instance);
        tracked.erase(instance);
        std::erase_if(spawned, [&](const Spawned& entry) { return entry.instance == instance; });
    }

    [[nodiscard]] static std::vector<std::byte> spawnMessage(const scene::Scene& scene, const Spawned& entry, Entity instance)
    {
        std::vector<std::byte> message{static_cast<std::byte>(Message::Spawn)};
        ByteWriter writer(message);
        writer.uuid(entry.spawner);
        writer.uuid(entry.prefab.uuid);
        writer.uuid(entry.instance);
        const scene::Transform* const transform = scene.tryGet<scene::Transform>(instance);
        writeTransform(writer, transform != nullptr ? *transform : scene::Transform{});
        return message;
    }

    // What arrives from a client goes on to the others, once the host has accepted it.
    void passOn(const net::NetworkEvent& event)
    {
        if (!multiplayer.isHost())
        {
            return;
        }
        for (const PeerId peer : multiplayer.peers())
        {
            if (peer != event.peer)
            {
                multiplayer.sendReplication(peer, event.bytes, event.delivery);
            }
        }
    }

    // On the host, whether this player may say this of an entity. The host vouches for what it passes on.
    [[nodiscard]] bool mayDecide(const net::NetworkEvent& event, const core::Uuid& entity) const
    {
        if (!multiplayer.isHost() || event.peer == multiplayer.authority(entity))
        {
            return true;
        }
        DEVEX_LOG_WARNING("Refused what player {} sent for an entity of player {}", event.peer, multiplayer.authority(entity));
        return false;
    }

    void receiveSpawn(scene::Scene& scene, const net::NetworkEvent& event, ByteReader& reader)
    {
        const core::Uuid spawner = reader.uuid();
        const asset::AssetId prefab{reader.uuid()};
        const core::Uuid instance = reader.uuid();
        const scene::Transform transform = readTransform(reader);
        if (reader.failed() || !mayDecide(event, spawner) || known.contains(instance))
        {
            return;
        }
        const Entity parent = scene.findEntity(spawner);
        if (!scene.isAlive(parent))
        {
            DEVEX_LOG_WARNING("Player {} placed an object under a NetworkSpawner this game does not have", event.peer);
            return;
        }
        const core::Result<Entity> created = scene::instantiatePrefab(scene, prefab, parent, instance);
        if (!created)
        {
            DEVEX_LOG_ERROR("Cannot place what player {} placed: {}", event.peer, created.error());
            return;
        }
        if (scene::Transform* const placed = scene.tryGet<scene::Transform>(*created))
        {
            *placed = transform;
        }
        spawned.push_back({.spawner = spawner, .prefab = prefab, .instance = instance});
        known.insert(instance);
        passOn(event);
    }

    void receiveDespawn(scene::Scene& scene, const net::NetworkEvent& event, ByteReader& reader)
    {
        const core::Uuid spawner = reader.uuid();
        const core::Uuid instance = reader.uuid();
        if (reader.failed() || !mayDecide(event, spawner))
        {
            return;
        }
        if (const Entity entity = scene.findEntity(instance); scene.isAlive(entity))
        {
            scene.destroyEntity(entity);
        }
        forget(instance);
        passOn(event);
    }

    void receiveValues(scene::Scene& scene, const net::NetworkEvent& event, ByteReader& reader)
    {
        const core::Uuid uuid = reader.uuid();
        const auto sequence = static_cast<std::uint32_t>(reader.number(sizeof(std::uint32_t)));
        const double time = reader.wide();
        const auto names = static_cast<std::uint32_t>(reader.number(sizeof(std::uint32_t)));
        if (reader.failed() || !mayDecide(event, uuid) || multiplayer.isAuthority(uuid))
        {
            return;
        }
        passOn(event);
        const Entity entity = scene.findEntity(uuid);
        const NetworkSync* const sync = scene.isAlive(entity) ? scene.tryGet<NetworkSync>(entity) : nullptr;
        if (sync == nullptr)
        {
            return;
        }
        Tracked& tracking = tracked[uuid];
        if (names != namesOf(sync->fields))
        {
            if (!tracking.warnedFields)
            {
                DEVEX_LOG_WARNING("The NetworkSync of '{}' does not list the same fields on every player's game: it does not follow", scene.name(entity));
                tracking.warnedFields = true;
            }
            return;
        }
        const PeerId authority = multiplayer.authority(uuid);
        if (tracking.lastAuthority != authority)
        {
            tracking.lastAuthority = authority;
            tracking.lastSequence = 0;
            tracking.snapshots.clear();
            tracking.playback = -1.0;
        }
        if (sequence <= tracking.lastSequence)
        {
            return;
        }
        tracking.lastSequence = sequence;
        tracking.lastTime = time;
        applyValues(scene, entity, *sync, tracking, time, reader);
    }

    static void applyValues(scene::Scene& scene, Entity entity, const NetworkSync& sync, Tracked& tracking, double time, ByteReader& reader)
    {
        Snapshot snapshot{.time = time};
        bool moved = false;
        const auto count = static_cast<std::size_t>(reader.number(1));
        for (std::size_t entry = 0; entry < count && !reader.failed(); ++entry)
        {
            const auto index = static_cast<std::size_t>(reader.number(1));
            if (index >= sync.fields.size())
            {
                return;
            }
            const std::string_view name = sync.fields[index];
            const Binding binding = bindingOf(name);
            if (binding.field == nullptr)
            {
                // It cannot be read past, not knowing its kind: what follows is lost, not what came before.
                return;
            }
            if (sync.smoothing > 0.0f && binding.type->name() == "Transform")
            {
                scene::Transform incoming;
                readValue(reader, *binding.field, binding.field->address(&incoming));
                if (reader.failed())
                {
                    return;
                }
                if (binding.field->name == "position")
                {
                    snapshot.position = incoming.position;
                }
                else if (binding.field->name == "rotation")
                {
                    snapshot.rotation = incoming.rotation;
                }
                else
                {
                    snapshot.scale = incoming.scale;
                }
                moved = true;
                continue;
            }
            void* const component = binding.type->findMutable(scene, entity);
            readValue(reader, *binding.field, component != nullptr ? binding.field->address(component) : nullptr);
        }
        if (moved && !reader.failed())
        {
            tracking.snapshots.push_back(snapshot);
            if (tracking.snapshots.size() > maxSnapshots)
            {
                tracking.snapshots.pop_front();
            }
        }
    }

    // Tells a new player what there is: its first state rather than what it has missed.
    void welcome(const scene::Scene& scene, PeerId peer)
    {
        for (const Spawned& entry : spawned)
        {
            if (const Entity instance = scene.findEntity(entry.instance); scene.isAlive(instance))
            {
                multiplayer.sendReplication(peer, spawnMessage(scene, entry, instance), net::Delivery::Reliable);
            }
        }
        for ([[maybe_unused]] const auto [entity, sync] : scene.view<NetworkSync>())
        {
            const core::Uuid uuid = scene.uuid(entity);
            Tracked& tracking = tracked[uuid];
            const bool own = multiplayer.isAuthority(uuid);
            if (own && tracking.sequence == 0)
            {
                tracking.sequence = 1;
            }
            const std::vector<std::byte> values = valuesOf(scene, entity, sync);
            multiplayer.sendReplication(peer, valuesMessage(uuid, own ? tracking.sequence : std::max(tracking.lastSequence, 1u), own ? clock : tracking.lastTime, sync, values),
                                        net::Delivery::Reliable);
        }
    }

    // Moves what another player has authority over to where it was a moment ago, between what arrived.
    void smooth(scene::Scene& scene, Entity entity, const NetworkSync& sync, Tracked& tracking, double delta) const
    {
        std::deque<Snapshot>& snapshots = tracking.snapshots;
        scene::Transform* const transform = scene.tryGet<scene::Transform>(entity);
        if (snapshots.empty() || transform == nullptr)
        {
            return;
        }
        const double target = snapshots.back().time - static_cast<double>(sync.smoothing);
        if (tracking.playback < 0.0 || std::abs(tracking.playback + delta - target) > catchUpLimit)
        {
            tracking.playback = target;
        }
        else
        {
            tracking.playback += delta;
            tracking.playback += (target - tracking.playback) * std::min(1.0, delta * catchUpSpeed);
        }
        while (snapshots.size() >= 2 && snapshots[1].time <= tracking.playback)
        {
            snapshots.pop_front();
        }
        const Snapshot& from = snapshots.front();
        const Snapshot& to = snapshots.size() >= 2 ? snapshots[1] : from;
        const auto amount = to.time > from.time ? static_cast<float>(std::clamp((tracking.playback - from.time) / (to.time - from.time), 0.0, 1.0)) : 0.0f;
        if (from.position && to.position)
        {
            transform->position = math::mix(*from.position, *to.position, amount);
        }
        if (from.rotation && to.rotation)
        {
            transform->rotation = math::slerp(*from.rotation, *to.rotation, amount);
        }
        if (from.scale && to.scale)
        {
            transform->scale = math::mix(*from.scale, *to.scale, amount);
        }
    }

    // A body whose Transform comes from another player's game is simulated there, not here.
    void followBody(scene::Scene& scene, Entity entity, const NetworkSync& sync, const core::Uuid& uuid, Tracked& tracking) const
    {
        const bool moves = std::ranges::any_of(sync.fields, [](const std::string& field) { return field.starts_with("Transform."); });
        const bool own = multiplayer.isAuthority(uuid);
        followBodyOf<scene::RigidBody>(scene, entity, moves, own, tracking.bodyWasDynamic);
        followBodyOf<scene::RigidBody2D>(scene, entity, moves, own, tracking.body2DWasDynamic);
        followCharacterOf<scene::CharacterController>(scene, entity, moves, own, tracking.characterGravity);
        followCharacterOf<scene::CharacterController2D>(scene, entity, moves, own, tracking.character2DGravity);
    }

    // What the authority of a spawner placed under it, and what it no longer has. Kept when there is no
    // session yet, for those who join later.
    void sendSpawns(const scene::Scene& scene)
    {
        for ([[maybe_unused]] const auto [spawner, component] : scene.view<scene::NetworkSpawner>())
        {
            const core::Uuid spawnerId = scene.uuid(spawner);
            if (!multiplayer.isAuthority(spawnerId))
            {
                continue;
            }
            for (Entity child = scene.firstChild(spawner); scene.isAlive(child); child = scene.nextSibling(child))
            {
                const core::Uuid instance = scene.uuid(child);
                const auto* const prefab = scene.tryGet<scene::PrefabInstance>(child);
                if (prefab == nullptr || !known.insert(instance).second)
                {
                    continue;
                }
                const Spawned entry{.spawner = spawnerId, .prefab = prefab->prefab, .instance = instance};
                spawned.push_back(entry);
                multiplayer.sendReplication(net::everyone, spawnMessage(scene, entry, child), net::Delivery::Reliable);
            }
        }
        for (std::size_t index = spawned.size(); index-- > 0;)
        {
            const Spawned entry = spawned[index];
            if (multiplayer.isAuthority(entry.spawner) && !scene.isAlive(scene.findEntity(entry.instance)))
            {
                std::vector<std::byte> message{static_cast<std::byte>(Message::Despawn)};
                ByteWriter writer(message);
                writer.uuid(entry.spawner);
                writer.uuid(entry.instance);
                multiplayer.sendReplication(net::everyone, message, net::Delivery::Reliable);
                forget(entry.instance);
            }
        }
    }

    // As soon as values change they go, and may be lost: the last ones go surely, once they stop changing.
    void sendValues(const scene::Scene& scene, double delta)
    {
        for ([[maybe_unused]] const auto [entity, sync] : scene.view<NetworkSync>())
        {
            const core::Uuid uuid = scene.uuid(entity);
            if (!multiplayer.isAuthority(uuid))
            {
                continue;
            }
            Tracked& tracking = tracked[uuid];
            tracking.wait -= delta;
            if (tracking.wait > 0.0)
            {
                continue;
            }
            tracking.wait = sync.rate > 0.0f ? std::max(tracking.wait + 1.0 / static_cast<double>(sync.rate), 0.0) : 0.0;
            std::vector<std::byte> values = valuesOf(scene, entity, sync);
            const bool changed = values != tracking.sent;
            if (!changed && tracking.settled)
            {
                continue;
            }
            ++tracking.sequence;
            multiplayer.sendReplication(net::everyone, valuesMessage(uuid, tracking.sequence, clock, sync, values),
                                        changed ? net::Delivery::Unreliable : net::Delivery::Reliable);
            tracking.settled = !changed;
            tracking.sent = std::move(values);
        }
    }
};

NetworkReplication::NetworkReplication(net::Multiplayer& multiplayer)
    : m_impl(std::make_unique<Impl>(multiplayer))
{
}

NetworkReplication::~NetworkReplication() = default;

void NetworkReplication::receive(scene::Scene& scene, core::Duration delta)
{
    Impl& impl = *m_impl;
    const double seconds = delta.count();
    impl.clock += seconds;
    const bool online = impl.multiplayer.state() == net::MultiplayerState::Online;
    if (impl.wasClient && !online)
    {
        impl.leftSession(scene);
    }
    impl.wasClient = online && !impl.multiplayer.isHost();
    for (const net::NetworkEvent& event : impl.multiplayer.events())
    {
        if (event.kind == net::NetworkEvent::Kind::PeerJoined && impl.multiplayer.isHost())
        {
            impl.welcome(scene, event.peer);
        }
        else if (event.kind == net::NetworkEvent::Kind::Replication && !event.bytes.empty())
        {
            ByteReader reader(event.bytes);
            const auto kind = static_cast<Message>(reader.number(1));
            if (kind == Message::Spawn)
            {
                impl.receiveSpawn(scene, event, reader);
            }
            else if (kind == Message::Despawn)
            {
                impl.receiveDespawn(scene, event, reader);
            }
            else if (kind == Message::Values)
            {
                impl.receiveValues(scene, event, reader);
            }
        }
    }
    for ([[maybe_unused]] const auto [entity, sync] : scene.view<NetworkSync>())
    {
        const core::Uuid uuid = scene.uuid(entity);
        Tracked& tracking = impl.tracked[uuid];
        impl.followBody(scene, entity, sync, uuid, tracking);
        if (!impl.multiplayer.isAuthority(uuid))
        {
            impl.smooth(scene, entity, sync, tracking, seconds);
        }
    }
}

void NetworkReplication::send(scene::Scene& scene, core::Duration delta)
{
    Impl& impl = *m_impl;
    impl.sendSpawns(scene);
    if (impl.multiplayer.state() == net::MultiplayerState::Online && !impl.multiplayer.peers().empty())
    {
        impl.sendValues(scene, delta.count());
    }
}

void NetworkReplication::sceneChanged()
{
    m_impl->spawned.clear();
    m_impl->known.clear();
    m_impl->tracked.clear();
}

void setAuthority(const scene::Scene& scene, net::Multiplayer& multiplayer, Entity entity, PeerId peer)
{
    if (!scene.isAlive(entity))
    {
        return;
    }
    multiplayer.setAuthority(scene.uuid(entity), peer);
    for (Entity child = scene.firstChild(entity); scene.isAlive(child); child = scene.nextSibling(child))
    {
        setAuthority(scene, multiplayer, child, peer);
    }
}

} // namespace devex::runtime
