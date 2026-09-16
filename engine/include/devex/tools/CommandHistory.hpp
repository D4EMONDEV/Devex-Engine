#pragma once

#include <devex/core/Error.hpp>
#include <devex/scene/Scene.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace devex::tools {

// A reversible change to a scene. Commands refer to entities by UUID, so they keep working when an
// entity is destroyed and recreated by other commands.
class Command
{
public:
    virtual ~Command() = default;

    // Short text for menus, such as "Rename entity".
    [[nodiscard]] virtual std::string description() const = 0;
    [[nodiscard]] virtual core::Result<void> apply(scene::Scene& scene) = 0;
    [[nodiscard]] virtual core::Result<void> revert(scene::Scene& scene) = 0;
};

// Undo and redo stacks of commands.
class CommandHistory
{
public:
    explicit CommandHistory(std::size_t capacity = 256);

    // Applies the command and records it. A failed command is not recorded.
    [[nodiscard]] core::Result<void> execute(scene::Scene& scene, std::unique_ptr<Command> command);
    // Records a command whose change was already applied, such as a value edited continuously
    // while dragging. Recording clears the redo stack.
    void recordApplied(std::unique_ptr<Command> command);

    // A command that fails to undo or redo is dropped from the history.
    [[nodiscard]] core::Result<void> undo(scene::Scene& scene);
    [[nodiscard]] core::Result<void> redo(scene::Scene& scene);

    [[nodiscard]] const Command* nextUndo() const noexcept;
    [[nodiscard]] const Command* nextRedo() const noexcept;
    [[nodiscard]] std::size_t undoCount() const noexcept;
    void clear() noexcept;

private:
    std::size_t m_capacity;
    std::deque<std::unique_ptr<Command>> m_done;
    std::vector<std::unique_ptr<Command>> m_undone;
};

} // namespace devex::tools
