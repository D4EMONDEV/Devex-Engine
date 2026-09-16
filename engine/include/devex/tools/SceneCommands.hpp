#pragma once

#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>
#include <devex/tools/CommandHistory.hpp>

#include <memory>
#include <string>

// Editing commands for scenes. Entities are named by UUID and components and fields by their
// registered names, so commands only rely on reflection.
namespace devex::tools {

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

// Destroys an entity and its descendants; undo restores them with their UUIDs and position.
[[nodiscard]] std::unique_ptr<Command> makeDestroyEntityCommand(core::Uuid entity);

// Moves an entity last under a new parent, or to the roots when newParent is nil.
[[nodiscard]] std::unique_ptr<Command> makeReparentCommand(core::Uuid entity, core::Uuid newParent);

[[nodiscard]] std::unique_ptr<Command> makeAddComponentCommand(core::Uuid entity,
                                                               std::string component);

// Undo restores the values the component had when it was removed.
[[nodiscard]] std::unique_ptr<Command> makeRemoveComponentCommand(core::Uuid entity,
                                                                  std::string component);

} // namespace devex::tools
