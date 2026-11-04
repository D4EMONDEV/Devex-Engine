#include <devex/runtime/CsgWorld.hpp>

#include <devex/core/Log.hpp>
#include <devex/csg/CsgBuilder.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/CsgComponents.hpp>
#include <devex/scene/PathComponents.hpp>

#include <cstring>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace devex::runtime {

namespace {

using scene::Entity;

[[nodiscard]] csg::Operation operationOf(scene::CsgOperation operation) noexcept
{
    return operation == scene::CsgOperation::Subtract ? csg::Operation::Subtract
           : operation == scene::CsgOperation::Intersect ? csg::Operation::Intersect
                                                         : csg::Operation::Union;
}

// The shape an entity has, without its transform or its children, and whether it collides.
[[nodiscard]] std::optional<csg::Node> shapeOf(const scene::Scene& scene, Entity entity, bool* collision = nullptr)
{
    csg::Node node;
    const auto finish = [&](const auto& shape) {
        node.operation = operationOf(shape.operation);
        if (collision != nullptr)
        {
            *collision = shape.collision;
        }
    };
    if (const auto* const box = scene.tryGet<scene::CsgBox>(entity))
    {
        node.shape = csg::Shape::Box;
        node.size = box->size;
        node.material = box->material;
        finish(*box);
    }
    else if (const auto* const sphere = scene.tryGet<scene::CsgSphere>(entity))
    {
        node.shape = csg::Shape::Sphere;
        node.radius = sphere->radius;
        node.sides = sphere->sides;
        node.material = sphere->material;
        finish(*sphere);
    }
    else if (const auto* const cylinder = scene.tryGet<scene::CsgCylinder>(entity))
    {
        node.shape = csg::Shape::Cylinder;
        node.radius = cylinder->radius;
        node.height = cylinder->height;
        node.sides = cylinder->sides;
        node.material = cylinder->material;
        finish(*cylinder);
    }
    else if (const auto* const path = scene.tryGet<scene::CsgPath>(entity))
    {
        node.shape = csg::Shape::Extrusion;
        node.width = path->width;
        node.depth = path->depth;
        node.material = path->material;
        finish(*path);
    }
    else if (const auto* const combiner = scene.tryGet<scene::CsgCombiner>(entity))
    {
        node.shape = csg::Shape::Group;
        finish(*combiner);
    }
    else
    {
        return std::nullopt;
    }
    return node;
}

[[nodiscard]] math::Mat4 worldOf(const scene::Scene& scene, Entity entity)
{
    const scene::WorldTransform* const world = scene.tryGet<scene::WorldTransform>(entity);
    if (world != nullptr)
    {
        return world->matrix;
    }
    const scene::Transform* const local = scene.tryGet<scene::Transform>(entity);
    return local != nullptr ? local->matrix() : math::Mat4{1.0f};
}

// The line of an extrusion: its path, in the space of the root.
void readLine(const scene::Scene& scene, Entity entity, const math::Mat4& fromWorld, csg::Node& node)
{
    const auto* const shape = scene.tryGet<scene::CsgPath>(entity);
    if (shape == nullptr)
    {
        return;
    }
    const Entity owner = !shape->path.uuid.isNil() ? scene.findEntity(shape->path.uuid) : scene.parent(entity);
    const scene::Path3D* const path = scene.isAlive(owner) ? scene.tryGet<scene::Path3D>(owner) : nullptr;
    if (path == nullptr)
    {
        return;
    }
    const scene::PathCurve curve(*path);
    const math::Mat4 toRoot = fromWorld * worldOf(scene, owner);
    node.closed = path->closed;
    node.line.reserve(curve.points().size());
    for (const math::Vec3 point : curve.points())
    {
        node.line.push_back(math::Vec3(toRoot * math::Vec4(point, 1.0f)));
    }
}

// The tree of a solid, in the space of its root. Entities without a shape cut the tree there.
[[nodiscard]] csg::Node treeOf(const scene::Scene& scene, Entity entity, csg::Node node, const math::Mat4& transform, const math::Mat4& fromWorld)
{
    node.transform = transform;
    readLine(scene, entity, fromWorld, node);
    for (Entity child = scene.firstChild(entity); scene.isAlive(child); child = scene.nextSibling(child))
    {
        if (std::optional<csg::Node> shape = shapeOf(scene, child))
        {
            const scene::Transform* const local = scene.tryGet<scene::Transform>(child);
            node.children.push_back(treeOf(scene, child, *std::move(shape), local != nullptr ? transform * local->matrix() : transform, fromWorld));
        }
    }
    return node;
}

class Signature
{
public:
    template <typename T>
    void add(const T& value)
    {
        addBytes(std::as_bytes(std::span(&value, 1)));
    }

    void add(const csg::Node& node)
    {
        add(node.shape);
        add(node.operation);
        add(node.transform);
        add(node.size);
        add(node.radius);
        add(node.height);
        add(node.sides);
        add(node.material.uuid);
        add(node.closed);
        add(node.width);
        add(node.depth);
        addBytes(std::as_bytes(std::span(node.line)));
        for (const csg::Node& child : node.children)
        {
            add(child);
        }
        add(node.children.size());
    }

    [[nodiscard]] std::uint64_t value() const noexcept
    {
        return m_hash;
    }

private:
    void addBytes(std::span<const std::byte> bytes)
    {
        for (const std::byte byte : bytes)
        {
            m_hash = (m_hash ^ std::to_integer<std::uint64_t>(byte)) * 1099511628211ull;
        }
    }

    std::uint64_t m_hash = 14695981039346656037ull;
};

template <typename T>
void collectRoots(const scene::Scene& scene, std::vector<Entity>& roots)
{
    for ([[maybe_unused]] const auto [entity, shape] : scene.view<T>())
    {
        const Entity parent = scene.parent(entity);
        if (!scene.isAlive(parent) || !shapeOf(scene, parent))
        {
            roots.push_back(entity);
        }
    }
}

} // namespace

void CsgWorld::update(scene::Scene& scene, AssetManager& assets)
{
    std::vector<Entity> roots;
    collectRoots<scene::CsgBox>(scene, roots);
    collectRoots<scene::CsgSphere>(scene, roots);
    collectRoots<scene::CsgCylinder>(scene, roots);
    collectRoots<scene::CsgPath>(scene, roots);
    collectRoots<scene::CsgCombiner>(scene, roots);

    for (auto& [uuid, built] : m_built)
    {
        built.seen = false;
    }
    for (const Entity root : roots)
    {
        bool collision = true;
        std::optional<csg::Node> shape = shapeOf(scene, root, &collision);
        if (!shape)
        {
            continue;
        }
        const core::Uuid uuid = scene.uuid(root);
        const csg::Node tree = treeOf(scene, root, *std::move(shape), math::Mat4{1.0f}, math::inverse(worldOf(scene, root)));
        Signature signature;
        signature.add(tree);

        Built& built = m_built[uuid];
        built.seen = true;
        if (built.signature != signature.value())
        {
            if (built.mesh.isValid())
            {
                assets.removeGeneratedMesh(built.mesh);
                built.mesh = {};
            }
            built.signature = signature.value();
            core::Result<asset::MeshData> mesh = csg::buildMesh(tree);
            if (!mesh)
            {
                DEVEX_LOG_WARNING("The solid '{}' cannot be made: {}", scene.name(root), mesh.error());
            }
            else if (!mesh->vertices.empty())
            {
                // Another identifier for each result, for what keeps things by mesh, such as the physics.
                const auto uuidBytes = std::as_bytes(std::span(uuid.bytes()));
                Signature identifier;
                identifier.add(signature.value());
                for (const std::byte byte : uuidBytes)
                {
                    identifier.add(byte);
                }
                built.mesh = asset::AssetId{core::Uuid::fromParts(identifier.value(), signature.value())};
                assets.addGeneratedMesh(built.mesh, *std::move(mesh));
            }
        }
        scene::GeneratedMesh* result = scene.tryGet<scene::GeneratedMesh>(root);
        if (result == nullptr)
        {
            result = &scene.add<scene::GeneratedMesh>(root);
        }
        result->mesh = built.mesh;
        result->collision = collision;
    }

    // What is no longer a root: another scene, a shape taken away, or one moved under another.
    std::erase_if(m_built, [&](const auto& entry) {
        if (!entry.second.seen)
        {
            if (entry.second.mesh.isValid())
            {
                assets.removeGeneratedMesh(entry.second.mesh);
            }
            const Entity gone = scene.findEntity(entry.first);
            if (scene.isAlive(gone) && scene.has<scene::GeneratedMesh>(gone))
            {
                scene.remove<scene::GeneratedMesh>(gone);
            }
        }
        return !entry.second.seen;
    });
}

void CsgWorld::clear(AssetManager& assets)
{
    for (const auto& [uuid, built] : m_built)
    {
        if (built.mesh.isValid())
        {
            assets.removeGeneratedMesh(built.mesh);
        }
    }
    m_built.clear();
}

} // namespace devex::runtime
