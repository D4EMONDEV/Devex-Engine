#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <format>
#include <utility>
#include <vector>

namespace devex::tools {
namespace {

using scene::Entity;
using scene::Scene;
using serialization::TextValue;

[[nodiscard]] core::Result<Entity> findEntity(const Scene& scene, core::Uuid uuid)
{
    const Entity entity = scene.findEntity(uuid);
    if (!entity.isValid())
    {
        return core::makeError(core::ErrorCode::NotFound, "entity {} no longer exists", uuid);
    }
    return entity;
}

// Returns an invalid entity for a nil UUID, which stands for the scene roots.
[[nodiscard]] core::Result<Entity> findOptionalEntity(const Scene& scene, core::Uuid uuid)
{
    return uuid.isNil() ? core::Result<Entity>(Entity{}) : findEntity(scene, uuid);
}

[[nodiscard]] core::Result<const scene::ComponentType*> findComponentType(std::string_view name)
{
    const scene::ComponentType* const type = scene::componentRegistry().find(name);
    if (type == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "component type '{}' is not registered",
                               name);
    }
    return type;
}

// Returns the mutable component of the entity. The registry only exposes const lookups, but the
// scene itself is mutable here.
[[nodiscard]] core::Result<void*> findComponent(Scene& scene, Entity entity,
                                                const scene::ComponentType& type)
{
    const void* const component = type.find(scene, entity);
    if (component == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "'{}' has no {} component",
                               scene.name(entity), type.name());
    }
    return const_cast<void*>(component);
}

// Entities of prefab instances come from their prefab, apart from the root of each instance.
[[nodiscard]] core::Result<void> checkOutsidePrefab(const Scene& scene, Entity entity, std::string_view action)
{
    if (!scene::isInsidePrefabInstance(scene, entity))
    {
        return {};
    }
    const Entity instance = scene::owningPrefabInstance(scene, entity);
    return core::makeError(core::ErrorCode::InvalidState,
                           "cannot {} '{}', which comes from the prefab of '{}': change the prefab, or make the "
                           "instance local",
                           action, scene.name(entity), instance.isValid() ? scene.name(instance) : std::string());
}

class SetFieldCommand final : public Command
{
public:
    SetFieldCommand(core::Uuid entity, std::string component, std::string field,
                    TextValue before, TextValue after)
        : m_entity(entity)
        , m_component(std::move(component))
        , m_field(std::move(field))
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return std::format("Edit {}.{}", m_component, m_field);
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        return assign(scene, m_after);
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        return assign(scene, m_before);
    }

private:
    [[nodiscard]] core::Result<void> assign(Scene& scene, const TextValue& value) const
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        if (!entity)
        {
            return std::unexpected(entity.error());
        }
        const core::Result<const scene::ComponentType*> type = findComponentType(m_component);
        if (!type)
        {
            return std::unexpected(type.error());
        }
        const core::Result<void*> component = findComponent(scene, *entity, **type);
        if (!component)
        {
            return std::unexpected(component.error());
        }
        const reflection::FieldInfo* const field = (*type)->type->findField(m_field);
        if (field == nullptr)
        {
            return core::makeError(core::ErrorCode::NotFound, "{} has no field '{}'", m_component,
                                   m_field);
        }
        return scene::readFieldValue(*field, value, field->address(*component));
    }

    core::Uuid m_entity;
    std::string m_component;
    std::string m_field;
    TextValue m_before;
    TextValue m_after;
};

class RenameCommand final : public Command
{
public:
    RenameCommand(core::Uuid entity, std::string before, std::string after)
        : m_entity(entity)
        , m_before(std::move(before))
        , m_after(std::move(after))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return "Rename entity";
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        return rename(scene, m_after);
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        return rename(scene, m_before);
    }

private:
    [[nodiscard]] core::Result<void> rename(Scene& scene, const std::string& name) const
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        if (!entity)
        {
            return std::unexpected(entity.error());
        }
        scene.setName(*entity, name);
        return {};
    }

    core::Uuid m_entity;
    std::string m_before;
    std::string m_after;
};

class CreateEntityCommand final : public Command
{
public:
    CreateEntityCommand(core::Uuid entity, std::string name, core::Uuid parent)
        : m_entity(entity)
        , m_name(std::move(name))
        , m_parent(parent)
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return "Create entity";
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        const core::Result<Entity> parent = findOptionalEntity(scene, m_parent);
        if (!parent)
        {
            return std::unexpected(parent.error());
        }
        const core::Result<Entity> entity = scene.createEntity(m_entity, m_name);
        if (!entity)
        {
            return std::unexpected(entity.error());
        }
        scene.add<scene::Transform>(*entity);
        if (core::Result<void> parented = scene.setParent(*entity, *parent); !parented)
        {
            scene.destroyEntity(*entity);
            return parented;
        }
        return {};
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        if (!entity)
        {
            return std::unexpected(entity.error());
        }
        scene.destroyEntity(*entity);
        return {};
    }

private:
    core::Uuid m_entity;
    std::string m_name;
    core::Uuid m_parent;
};

class CreateEntityTreeCommand final : public Command
{
public:
    CreateEntityTreeCommand(std::string tree, core::Uuid root, core::Uuid parent,
                            std::string description)
        : m_tree(std::move(tree))
        , m_root(root)
        , m_parent(parent)
        , m_description(std::move(description))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return m_description;
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        const core::Result<Entity> parent = findOptionalEntity(scene, m_parent);
        if (!parent)
        {
            return std::unexpected(parent.error());
        }
        core::Result<Entity> created = scene::loadEntityTree(scene, m_tree, *parent);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        return {};
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        const core::Result<Entity> root = findEntity(scene, m_root);
        if (!root)
        {
            return std::unexpected(root.error());
        }
        scene.destroyEntity(*root);
        return {};
    }

private:
    std::string m_tree;
    core::Uuid m_root;
    core::Uuid m_parent;
    std::string m_description;
};

class DestroyEntityCommand final : public Command
{
public:
    explicit DestroyEntityCommand(core::Uuid entity)
        : m_entity(entity)
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return "Delete entity";
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        if (!entity)
        {
            return std::unexpected(entity.error());
        }
        if (core::Result<void> allowed = checkOutsidePrefab(scene, *entity, "delete"); !allowed)
        {
            return allowed;
        }
        // Remember the subtree and its place, so that undo puts it back identically.
        m_snapshot = scene::saveEntityTree(scene, *entity);
        const Entity parent = scene.parent(*entity);
        const Entity next = scene.nextSibling(*entity);
        m_parent = parent.isValid() ? scene.uuid(parent) : core::Uuid{};
        m_nextSibling = next.isValid() ? scene.uuid(next) : core::Uuid{};
        scene.destroyEntity(*entity);
        return {};
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        const core::Result<Entity> parent = findOptionalEntity(scene, m_parent);
        if (!parent)
        {
            return std::unexpected(parent.error());
        }
        // The next sibling may be gone; the subtree then goes last.
        const Entity before = m_nextSibling.isNil() ? Entity{} : scene.findEntity(m_nextSibling);
        const bool beforeIsSibling = before.isValid() && scene.parent(before) == *parent;
        core::Result<Entity> restored = scene::loadEntityTree(scene, m_snapshot, *parent,
                                                              beforeIsSibling ? before : Entity{});
        if (!restored)
        {
            return std::unexpected(restored.error());
        }
        return {};
    }

private:
    core::Uuid m_entity;
    std::string m_snapshot;
    core::Uuid m_parent;
    core::Uuid m_nextSibling;
};

class ReplaceEntityTreeCommand final : public Command
{
public:
    ReplaceEntityTreeCommand(core::Uuid root, std::string tree, std::string description)
        : m_root(root)
        , m_after(std::move(tree))
        , m_description(std::move(description))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return m_description;
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        const core::Result<Entity> root = findEntity(scene, m_root);
        if (!root)
        {
            return std::unexpected(root.error());
        }
        m_before = scene::saveEntityTree(scene, *root);
        return replace(scene, *root, m_before, m_after);
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        const core::Result<Entity> root = findEntity(scene, m_root);
        if (!root)
        {
            return std::unexpected(root.error());
        }
        return replace(scene, *root, m_after, m_before);
    }

private:
    [[nodiscard]] static core::Result<void> replace(Scene& scene, Entity root, const std::string& current,
                                                    const std::string& replacement)
    {
        const Entity parent = scene.parent(root);
        const Entity next = scene.nextSibling(root);
        scene.destroyEntity(root);
        core::Result<Entity> replaced = scene::loadEntityTree(scene, replacement, parent, next);
        if (!replaced)
        {
            // The entities come back as they were.
            static_cast<void>(scene::loadEntityTree(scene, current, parent, next));
            return std::unexpected(replaced.error());
        }
        return {};
    }

    core::Uuid m_root;
    std::string m_before;
    std::string m_after;
    std::string m_description;
};

class ReparentCommand final : public Command
{
public:
    ReparentCommand(core::Uuid entity, core::Uuid newParent)
        : m_entity(entity)
        , m_newParent(newParent)
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return "Move entity";
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        const core::Result<Entity> newParent = findOptionalEntity(scene, m_newParent);
        if (!entity || !newParent)
        {
            return std::unexpected(!entity ? entity.error() : newParent.error());
        }
        if (core::Result<void> allowed = checkOutsidePrefab(scene, *entity, "move"); !allowed)
        {
            return allowed;
        }
        const Entity oldParent = scene.parent(*entity);
        const Entity oldNext = scene.nextSibling(*entity);
        if (core::Result<void> moved = scene.setParent(*entity, *newParent); !moved)
        {
            return moved;
        }
        m_oldParent = oldParent.isValid() ? scene.uuid(oldParent) : core::Uuid{};
        m_oldNextSibling = oldNext.isValid() ? scene.uuid(oldNext) : core::Uuid{};
        return {};
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        const core::Result<Entity> oldParent = findOptionalEntity(scene, m_oldParent);
        if (!entity || !oldParent)
        {
            return std::unexpected(!entity ? entity.error() : oldParent.error());
        }
        const Entity before = m_oldNextSibling.isNil() ? Entity{} : scene.findEntity(m_oldNextSibling);
        const bool beforeIsSibling = before.isValid() && scene.parent(before) == *oldParent;
        return scene.setParent(*entity, *oldParent, beforeIsSibling ? before : Entity{});
    }

private:
    core::Uuid m_entity;
    core::Uuid m_newParent;
    core::Uuid m_oldParent;
    core::Uuid m_oldNextSibling;
};

class AddComponentCommand final : public Command
{
public:
    AddComponentCommand(core::Uuid entity, std::string component)
        : m_entity(entity)
        , m_component(std::move(component))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return std::format("Add {}", m_component);
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        const core::Result<const scene::ComponentType*> type = findComponentType(m_component);
        if (!entity || !type)
        {
            return std::unexpected(!entity ? entity.error() : type.error());
        }
        if ((*type)->find(scene, *entity) != nullptr)
        {
            return core::makeError(core::ErrorCode::AlreadyExists, "'{}' already has {}",
                                   scene.name(*entity), m_component);
        }
        (*type)->emplace(scene, *entity);
        return {};
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        const core::Result<const scene::ComponentType*> type = findComponentType(m_component);
        if (!entity || !type)
        {
            return std::unexpected(!entity ? entity.error() : type.error());
        }
        (*type)->remove(scene, *entity);
        return {};
    }

private:
    core::Uuid m_entity;
    std::string m_component;
};

class RemoveComponentCommand final : public Command
{
public:
    RemoveComponentCommand(core::Uuid entity, std::string component)
        : m_entity(entity)
        , m_component(std::move(component))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return std::format("Remove {}", m_component);
    }

    [[nodiscard]] core::Result<void> apply(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        const core::Result<const scene::ComponentType*> type = findComponentType(m_component);
        if (!entity || !type)
        {
            return std::unexpected(!entity ? entity.error() : type.error());
        }
        const core::Result<void*> component = findComponent(scene, *entity, **type);
        if (!component)
        {
            return std::unexpected(component.error());
        }
        if (const scene::PrefabEntity* const fromPrefab = scene.tryGet<scene::PrefabEntity>(*entity))
        {
            const scene::PrefabInstance* const instance =
                scene.isAlive(fromPrefab->instance) ? scene.tryGet<scene::PrefabInstance>(fromPrefab->instance) : nullptr;
            const std::shared_ptr<const Scene> base =
                instance != nullptr ? scene::prefabBase(instance->prefab, scene.uuid(fromPrefab->instance)) : nullptr;
            const Entity baseEntity = base != nullptr ? base->findEntity(m_entity) : Entity{};
            if (baseEntity.isValid() && (*type)->find(*base, baseEntity) != nullptr)
            {
                return core::makeError(core::ErrorCode::InvalidState,
                                       "cannot remove {} from '{}', which has it from its prefab", m_component,
                                       scene.name(*entity));
            }
        }

        m_values.clear();
        for (const reflection::FieldInfo& field : (*type)->type->fields)
        {
            m_values.emplace_back(field.name, scene::writeFieldValue(field,
                                                                     field.address(*component)));
        }
        (*type)->remove(scene, *entity);
        return {};
    }

    [[nodiscard]] core::Result<void> revert(Scene& scene) override
    {
        const core::Result<Entity> entity = findEntity(scene, m_entity);
        const core::Result<const scene::ComponentType*> type = findComponentType(m_component);
        if (!entity || !type)
        {
            return std::unexpected(!entity ? entity.error() : type.error());
        }
        void* const component = (*type)->emplace(scene, *entity);
        for (const auto& [fieldName, value] : m_values)
        {
            if (const reflection::FieldInfo* const field = (*type)->type->findField(fieldName))
            {
                if (core::Result<void> read =
                        scene::readFieldValue(*field, value, field->address(component));
                    !read)
                {
                    return read;
                }
            }
        }
        return {};
    }

private:
    core::Uuid m_entity;
    std::string m_component;
    std::vector<std::pair<std::string, TextValue>> m_values;
};

// Several commands as one step of the history.
class CompositeCommand final : public Command
{
public:
    CompositeCommand(std::vector<std::unique_ptr<Command>> commands, std::string description)
        : m_commands(std::move(commands))
        , m_description(std::move(description))
    {
    }

    [[nodiscard]] std::string description() const override
    {
        return m_description;
    }

    [[nodiscard]] core::Result<void> apply(scene::Scene& scene) override
    {
        for (const std::unique_ptr<Command>& command : m_commands)
        {
            if (core::Result<void> applied = command->apply(scene); !applied)
            {
                return applied;
            }
        }
        return {};
    }

    [[nodiscard]] core::Result<void> revert(scene::Scene& scene) override
    {
        // Backwards, so that each command undoes what the ones after it left.
        for (auto command = m_commands.rbegin(); command != m_commands.rend(); ++command)
        {
            if (core::Result<void> reverted = (*command)->revert(scene); !reverted)
            {
                return reverted;
            }
        }
        return {};
    }

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    std::string m_description;
};

} // namespace

std::unique_ptr<Command> makeCompositeCommand(std::vector<std::unique_ptr<Command>> commands,
                                              std::string description)
{
    if (commands.empty())
    {
        return nullptr;
    }
    if (commands.size() == 1)
    {
        return std::move(commands.front());
    }
    return std::make_unique<CompositeCommand>(std::move(commands), std::move(description));
}

std::unique_ptr<Command> makeSetFieldCommand(core::Uuid entity, std::string component,
                                             std::string field, TextValue before, TextValue after)
{
    return std::make_unique<SetFieldCommand>(entity, std::move(component), std::move(field),
                                             std::move(before), std::move(after));
}

std::unique_ptr<Command> makeRenameCommand(core::Uuid entity, std::string before, std::string after)
{
    return std::make_unique<RenameCommand>(entity, std::move(before), std::move(after));
}

std::unique_ptr<Command> makeCreateEntityCommand(core::Uuid entity, std::string name,
                                                 core::Uuid parent)
{
    return std::make_unique<CreateEntityCommand>(entity, std::move(name), parent);
}

std::unique_ptr<Command> makeCreateEntityTreeCommand(std::string tree, core::Uuid root,
                                                     core::Uuid parent, std::string description)
{
    return std::make_unique<CreateEntityTreeCommand>(std::move(tree), root, parent,
                                                     std::move(description));
}

std::unique_ptr<Command> makeReplaceEntityTreeCommand(core::Uuid root, std::string tree, std::string description)
{
    return std::make_unique<ReplaceEntityTreeCommand>(root, std::move(tree), std::move(description));
}

std::unique_ptr<Command> makeDestroyEntityCommand(core::Uuid entity)
{
    return std::make_unique<DestroyEntityCommand>(entity);
}

std::unique_ptr<Command> makeReparentCommand(core::Uuid entity, core::Uuid newParent)
{
    return std::make_unique<ReparentCommand>(entity, newParent);
}

std::unique_ptr<Command> makeAddComponentCommand(core::Uuid entity, std::string component)
{
    return std::make_unique<AddComponentCommand>(entity, std::move(component));
}

std::unique_ptr<Command> makeRemoveComponentCommand(core::Uuid entity, std::string component)
{
    return std::make_unique<RemoveComponentCommand>(entity, std::move(component));
}

} // namespace devex::tools
