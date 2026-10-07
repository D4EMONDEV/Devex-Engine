#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/AssetId.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/TilesetData.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Time.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>
#include <devex/scene/TilemapComponents.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace devex::scene {
class Scene;
}

// Rigid bodies, colliders and characters in the XY plane, simulated with Box2D from the 2D physics
// components of a scene.
namespace devex::physics2d {

// A mask of collision layers with every layer.
inline constexpr std::uint16_t allLayers = 0xFFFF;

struct DEVEX_API RayHit
{
    // The entity that owns the body hit: the entity of its RigidBody2D, or of its collider.
    scene::Entity entity;
    math::Vec2 point{0.0f};
    math::Vec2 normal{0.0f, 1.0f};
    float distance = 0.0f;
};

enum class ContactPhase : std::uint8_t
{
    Begin,
    End,
};

// Two bodies that started or stopped touching. A trigger contact involves at least one trigger:
// nothing blocked, something entered or left. The same layout as the contacts of 3D physics.
struct DEVEX_API Contact
{
    ContactPhase phase = ContactPhase::Begin;
    scene::Entity first;
    scene::Entity second;
    bool trigger = false;

    [[nodiscard]] bool involves(scene::Entity entity) const noexcept
    {
        return first == entity || second == entity;
    }

    [[nodiscard]] scene::Entity other(scene::Entity entity) const noexcept
    {
        return first == entity ? second : first;
    }
};

// A joint that broke: its entity, and those of the bodies it tied; body B is invalid for the world.
// The same layout as the breaks of 3D physics.
struct DEVEX_API JointBreak
{
    scene::Entity joint;
    scene::Entity bodyA;
    scene::Entity bodyB;
};

// Cells a tilemap collides with: from the bottom-left cell, so many cells across and up. A one-way
// rectangle is a ledge along the top of its cells.
struct DEVEX_API TileRectangle
{
    math::IVec2 cell{0};
    math::IVec2 size{1};
    bool oneWay = false;

    bool operator==(const TileRectangle&) const = default;
};

// The rectangles the collider of a tilemap is made of: full tiles merged into rectangles along rows,
// then rows of the same width on top of each other; top tiles merged along rows. The collision of a
// tile comes from its tileset.
[[nodiscard]] DEVEX_API std::vector<TileRectangle> tileRectangles(
              const scene::TileGrid& grid, const std::function<asset::TileCollision(std::uint32_t tile)>& collisionOf);

// The tileset a tilemap collider reads; null when it cannot be loaded.
using TilesetSource = std::function<std::shared_ptr<const asset::TilesetData>(asset::AssetId tileset)>;

struct DEVEX_API Physics2DWorldConfig
{
    // The gravity of the world is the X and Y of the gravity of the project; the layers are the
    // project's.
    asset::PhysicsSettings settings;
    TilesetSource tilesets;
    // Sub-steps of each fixed step, which make stacks and joints stiffer.
    int subSteps = 4;
};

// The 2D simulation of one scene. As the 3D one, each step brings the bodies in line with the 2D
// physics components of the scene, so that game code creates, changes, moves and removes bodies by
// editing components and transforms, then advances the simulation and writes the result back.
class DEVEX_API Physics2DWorld
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<Physics2DWorld>> create(Physics2DWorldConfig config);
    ~Physics2DWorld();

    Physics2DWorld(const Physics2DWorld&) = delete;
    Physics2DWorld& operator=(const Physics2DWorld&) = delete;

    // Advances the simulation of the scene by a fixed step. Bodies are created for new components,
    // rebuilt when their components change and removed with them; a body whose entity game code moved
    // is teleported, a kinematic body follows its entity. Characters move with the velocity of their
    // CharacterController2D. Afterwards, dynamic bodies and characters write their Transform,
    // velocities and ground state. World transforms must be up to date. Using the world with another
    // scene starts over from that scene.
    void step(scene::Scene& scene, core::Duration delta);

    // Places dynamic bodies, characters and their descendants between their poses of the last two
    // steps, by alpha from the previous one, in world transforms only.
    void interpolate(scene::Scene& scene, float alpha);

    // The contacts that began or ended during the steps since clearContacts, and the joints that broke
    // during them, which clearContacts forgets too.
    [[nodiscard]] std::span<const Contact> contacts() const noexcept;
    [[nodiscard]] std::span<const JointBreak> brokenJoints() const noexcept;
    void clearContacts() noexcept;

    // The closest body along a ray within the distance, among the layers of the mask, through
    // triggers, ignoring the bodies of an entity. The direction does not need to be normalized.
    [[nodiscard]] std::optional<RayHit> raycast(math::Vec2 origin, math::Vec2 direction, float maxDistance,
                                                std::uint16_t layers = allLayers, scene::Entity ignore = {}) const;
    // The entities whose bodies, triggers included, overlap a circle, each once.
    [[nodiscard]] std::vector<scene::Entity> overlapCircle(math::Vec2 center, float radius,
                                                           std::uint16_t layers = allLayers,
                                                           scene::Entity ignore = {}) const;

    // Act on the dynamic body of an entity; ignored for other entities. Forces and torques apply
    // during the next step, impulses change the velocity at once.
    void addForce(scene::Entity entity, math::Vec2 force);
    void addTorque(scene::Entity entity, float torque);
    void addImpulse(scene::Entity entity, math::Vec2 impulse);

    // Bodies in the simulation, the ones of characters included, and joints holding them.
    [[nodiscard]] std::size_t bodyCount() const noexcept;
    [[nodiscard]] std::size_t jointCount() const noexcept;
    [[nodiscard]] math::Vec2 gravity() const noexcept;

private:
    struct Implementation;

    explicit Physics2DWorld(std::unique_ptr<Implementation> implementation) noexcept;

    std::unique_ptr<Implementation> m_implementation;
};

} // namespace devex::physics2d
