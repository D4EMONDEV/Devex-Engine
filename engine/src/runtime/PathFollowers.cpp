#include <devex/runtime/PathFollowers.hpp>

#include <devex/scene/Components.hpp>
#include <devex/scene/NetworkComponents.hpp>

#include <algorithm>
#include <cmath>
#include <span>

namespace devex::runtime {

namespace {

[[nodiscard]] math::Mat4 worldOf(const scene::Scene& scene, scene::Entity entity)
{
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    if (world != nullptr)
    {
        return world->matrix;
    }
    const scene::Transform* const local = scene.tryGet<scene::Transform>(entity);
    return local != nullptr ? local->matrix() : math::Mat4{1.0f};
}

void mix(std::uint64_t& hash, std::span<const math::Vec3> values)
{
    for (const std::byte byte : std::as_bytes(values))
    {
        hash = (hash ^ std::to_integer<std::uint64_t>(byte)) * 1099511628211ull;
    }
    hash = (hash ^ 0xffull) * 1099511628211ull;
}

[[nodiscard]] std::uint64_t signatureOf(const scene::Path3D& path)
{
    std::uint64_t hash = 14695981039346656037ull;
    mix(hash, path.points);
    mix(hash, path.handlesIn);
    mix(hash, path.handlesOut);
    return (hash ^ (path.closed ? 1ull : 0ull)) * 1099511628211ull;
}

// Where it stands after moving on: at the end, around, or back the other way.
void advance(scene::PathFollower& follower, const scene::PathCurve& curve, float seconds)
{
    if (!follower.playing)
    {
        return;
    }
    const float total = curve.length();
    follower.distance += follower.speed * seconds * (follower.reversed ? -1.0f : 1.0f);
    if (total <= 0.0f)
    {
        follower.distance = 0.0f;
        return;
    }
    switch (follower.end)
    {
    case scene::PathEnd::Stop:
        if (follower.distance >= total || follower.distance <= 0.0f)
        {
            follower.distance = std::clamp(follower.distance, 0.0f, total);
            follower.playing = false;
        }
        break;
    case scene::PathEnd::Loop:
        follower.distance = std::fmod(follower.distance, total);
        if (follower.distance < 0.0f)
        {
            follower.distance += total;
        }
        break;
    case scene::PathEnd::PingPong:
        if (follower.distance > total)
        {
            follower.distance = std::max(2.0f * total - follower.distance, 0.0f);
            follower.reversed = !follower.reversed;
        }
        else if (follower.distance < 0.0f)
        {
            follower.distance = std::min(-follower.distance, total);
            follower.reversed = !follower.reversed;
        }
        break;
    }
}

// The rotation whose -Z goes along a direction, as upright as it can be.
[[nodiscard]] math::Quat looking(math::Vec3 direction, math::Vec3 up, math::Quat otherwise)
{
    const math::Vec3 side = math::cross(direction, up);
    const float size = math::length(side);
    if (size < 1.0e-4f)
    {
        return otherwise;
    }
    const math::Vec3 right = side / size;
    math::Mat4 basis{1.0f};
    basis[0] = math::Vec4(right, 0.0f);
    basis[1] = math::Vec4(math::cross(right, direction), 0.0f);
    basis[2] = math::Vec4(-direction, 0.0f);
    return math::decomposeTrs(basis).rotation;
}

[[nodiscard]] const scene::Path3D* pathOf(const scene::Scene& scene, scene::Entity entity)
{
    return scene.isAlive(entity) ? scene.tryGet<scene::Path3D>(entity) : nullptr;
}

} // namespace

const scene::PathCurve& PathFollowers::curveOf(const core::Uuid& entity, const scene::Path3D& path)
{
    const std::uint64_t signature = signatureOf(path);
    const auto found = m_curves.find(entity);
    if (found != m_curves.end() && found->second.signature == signature)
    {
        return found->second.curve;
    }
    return m_curves.insert_or_assign(entity, Baked{.signature = signature, .curve = scene::PathCurve(path)}).first->second.curve;
}

void PathFollowers::update(scene::Scene& scene, core::Duration delta, const net::Multiplayer* multiplayer)
{
    const auto seconds = static_cast<float>(delta.count());
    for ([[maybe_unused]] auto [entity, follower] : scene.view<scene::PathFollower>())
    {
        if (multiplayer != nullptr && scene.has<scene::NetworkSync>(entity) && !multiplayer->isAuthority(scene.uuid(entity)))
        {
            continue;
        }
        const scene::Entity parent = scene.parent(entity);
        const scene::Entity owner = !follower.path.uuid.isNil() ? scene.findEntity(follower.path.uuid) : parent;
        const scene::Path3D* const path = pathOf(scene, owner);
        scene::Transform* const transform = scene.tryGet<scene::Transform>(entity);
        if (path == nullptr || transform == nullptr)
        {
            continue;
        }
        const scene::PathCurve& curve = curveOf(scene.uuid(owner), *path);
        if (curve.empty())
        {
            continue;
        }
        advance(follower, curve, seconds);

        // From the space of the path to the one the transform of the entity is in.
        const math::Mat4 toLocal = owner == parent ? math::Mat4{1.0f}
                                                   : (scene.isAlive(parent) ? math::inverse(worldOf(scene, parent)) : math::Mat4{1.0f}) * worldOf(scene, owner);
        transform->position = math::Vec3(toLocal * math::Vec4(curve.positionAt(follower.distance), 1.0f));
        if (follower.orient)
        {
            const math::Vec3 forward = math::normalize(math::Vec3(toLocal * math::Vec4(curve.directionAt(follower.distance), 0.0f)));
            const math::Mat4 fromWorld = scene.isAlive(parent) ? math::inverse(worldOf(scene, parent)) : math::Mat4{1.0f};
            const math::Vec3 up = math::normalize(math::Vec3(fromWorld * math::Vec4(0.0f, 1.0f, 0.0f, 0.0f)));
            transform->rotation = looking(follower.reversed ? -forward : forward, up, transform->rotation);
        }
    }
}

void PathFollowers::clear() noexcept
{
    m_curves.clear();
}

float pathLength(const scene::Scene& scene, scene::Entity entity)
{
    const scene::Path3D* const path = pathOf(scene, entity);
    if (path == nullptr)
    {
        return 0.0f;
    }
    // In meters of the world, the way the entity is scaled.
    const scene::PathCurve curve(*path);
    const math::Mat4 world = worldOf(scene, entity);
    float total = 0.0f;
    const std::span<const math::Vec3> points = curve.points();
    for (std::size_t index = 1; index < points.size(); ++index)
    {
        total += math::length(math::Vec3(world * math::Vec4(points[index], 1.0f)) - math::Vec3(world * math::Vec4(points[index - 1], 1.0f)));
    }
    return total;
}

math::Vec3 pathPositionAt(const scene::Scene& scene, scene::Entity entity, float distance)
{
    const scene::Path3D* const path = pathOf(scene, entity);
    return path != nullptr ? math::Vec3(worldOf(scene, entity) * math::Vec4(scene::PathCurve(*path).positionAt(distance), 1.0f)) : math::Vec3{0.0f};
}

math::Vec3 pathDirectionAt(const scene::Scene& scene, scene::Entity entity, float distance)
{
    const scene::Path3D* const path = pathOf(scene, entity);
    if (path == nullptr)
    {
        return math::Vec3{0.0f, 0.0f, -1.0f};
    }
    return math::normalize(math::Vec3(worldOf(scene, entity) * math::Vec4(scene::PathCurve(*path).directionAt(distance), 0.0f)));
}

float pathClosestDistance(const scene::Scene& scene, scene::Entity entity, math::Vec3 position)
{
    const scene::Path3D* const path = pathOf(scene, entity);
    return path != nullptr ? scene::PathCurve(*path).closestDistance(math::Vec3(math::inverse(worldOf(scene, entity)) * math::Vec4(position, 1.0f))) : 0.0f;
}

} // namespace devex::runtime
