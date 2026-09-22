#pragma once

#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/tools/CommandHistory.hpp>

#include <memory>
#include <string>
#include <vector>

// Editing commands for scenes. Entities are named by UUID and components and fields by their
// registered names, so commands only rely on reflection.
namespace devex::tools {

// Several commands undone and redone as one, such as the two corners a drag moves together. The
// commands are applied in order and reverted in the opposite one; an empty list is no command.
[[nodiscard]] std::unique_ptr<Command> makeCompositeCommand(
    std::vector<std::unique_ptr<Command>> commands, std::string description);

[[nodiscard]] std::unique_ptr<Command> makeSetFieldCommand(core::Uuid entity, std::string component,
                                                           std::string field,
                                                           serialization::TextValue before,
                                                           serialization::TextValue after);

[[nodiscard]] std::unique_ptr<Command> makeRenameCommand(core::Uuid entity, std::string before,
                                                         std::string after);

// Creates an entity with a Transform, last under parent, or as a root when parent is nil. The
// caller chooses the UUID, so it can select the entity, and redo recreates the same one.
[[nodiscard]] std::unique_ptr<Command> makeCreateEntityCommand(core::Uuid entity, std::string name,
                                                               core::Uuid parent);

// Creates the entities written by scene::saveEntityTree, whose root has the UUID `root`, last under
// parent or as a root when parent is nil. Used to place models: redo recreates the same UUIDs.
[[nodiscard]] std::unique_ptr<Command> makeCreateEntityTreeCommand(std::string tree,
                                                                   core::Uuid root,
                                                                   core::Uuid parent,
                                                                   std::string description);

// Replaces an entity and its descendants with the entities written by scene::saveEntityTree, whose
// root has the same UUID, at the same place. Used to turn entities into a prefab instance and back.
[[nodiscard]] std::unique_ptr<Command> makeReplaceEntityTreeCommand(core::Uuid root, std::string tree,
                                                                    std::string description);

// Destroys an entity and its descendants; undo restores them with their UUIDs and position. The
// entities of a prefab instance other than its root cannot be destroyed.
[[nodiscard]] std::unique_ptr<Command> makeDestroyEntityCommand(core::Uuid entity);

// Moves an entity last under a new parent, or to the roots when newParent is nil. The entities of a
// prefab instance other than its root cannot be moved.
[[nodiscard]] std::unique_ptr<Command> makeReparentCommand(core::Uuid entity, core::Uuid newParent);

[[nodiscard]] std::unique_ptr<Command> makeAddComponentCommand(core::Uuid entity,
                                                               std::string component);

// Undo restores the values the component had when it was removed. Components that an entity of a
// prefab instance has from its prefab cannot be removed.
[[nodiscard]] std::unique_ptr<Command> makeRemoveComponentCommand(core::Uuid entity,
                                                                  std::string component);

} // namespace devex::tools
