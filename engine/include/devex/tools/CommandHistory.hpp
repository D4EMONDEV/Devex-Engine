#pragma once

#include <devex/core/Error.hpp>
#include <devex/scene/Scene.hpp>

#include <cstddef>
#include <cstdint>
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
    // Forgets every command. The state that follows gets a new identifier.
    void clear() noexcept;

    // Identifies the state reached by the commands done so far: undoing then redoing a command
    // comes back to the same identifier, and a new command never reuses one. Comparing it with the
    // identifier recorded when a scene was saved tells whether the scene has unsaved changes.
    [[nodiscard]] std::uint64_t stateId() const noexcept;

private:
    struct Entry
    {
        std::unique_ptr<Command> command;
        // The state identifier once the command is done.
        std::uint64_t state = 0;
    };

    std::size_t m_capacity;
    std::deque<Entry> m_done;
    std::vector<Entry> m_undone;
    // The state before the oldest remembered command.
    std::uint64_t m_baseState = 0;
    std::uint64_t m_lastState = 0;
};

} // namespace devex::tools
