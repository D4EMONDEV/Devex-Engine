#include <devex/core/Assert.hpp>
#include <devex/tools/CommandHistory.hpp>

#include <algorithm>
#include <utility>

namespace devex::tools {

CommandHistory::CommandHistory(std::size_t capacity)
    : m_capacity(std::max<std::size_t>(capacity, 1))
{
}

core::Result<void> CommandHistory::execute(scene::Scene& scene, std::unique_ptr<Command> command)
{
    DEVEX_ASSERT(command != nullptr);
    if (core::Result<void> applied = command->apply(scene); !applied)
    {
        return applied;
    }
    recordApplied(std::move(command));
    return {};
}

void CommandHistory::recordApplied(std::unique_ptr<Command> command)
{
    DEVEX_ASSERT(command != nullptr);
    m_undone.clear();
    m_done.push_back({std::move(command), ++m_lastState});
    while (m_done.size() > m_capacity)
    {
        m_baseState = m_done.front().state;
        m_done.pop_front();
    }
}

core::Result<void> CommandHistory::undo(scene::Scene& scene)
{
    if (m_done.empty())
    {
        return core::makeError(core::ErrorCode::InvalidState, "nothing to undo");
    }
    Entry entry = std::move(m_done.back());
    m_done.pop_back();
    if (core::Result<void> reverted = entry.command->revert(scene); !reverted)
    {
        return core::makeError(reverted.error().code, "cannot undo \"{}\": {}",
                               entry.command->description(), reverted.error().message);
    }
    m_undone.push_back(std::move(entry));
    return {};
}

core::Result<void> CommandHistory::redo(scene::Scene& scene)
{
    if (m_undone.empty())
    {
        return core::makeError(core::ErrorCode::InvalidState, "nothing to redo");
    }
    Entry entry = std::move(m_undone.back());
    m_undone.pop_back();
    if (core::Result<void> applied = entry.command->apply(scene); !applied)
    {
        return core::makeError(applied.error().code, "cannot redo \"{}\": {}",
                               entry.command->description(), applied.error().message);
    }
    m_done.push_back(std::move(entry));
    return {};
}

const Command* CommandHistory::nextUndo() const noexcept
{
    return m_done.empty() ? nullptr : m_done.back().command.get();
}

const Command* CommandHistory::nextRedo() const noexcept
{
    return m_undone.empty() ? nullptr : m_undone.back().command.get();
}

std::size_t CommandHistory::undoCount() const noexcept
{
    return m_done.size();
}

void CommandHistory::clear() noexcept
{
    m_done.clear();
    m_undone.clear();
    m_baseState = ++m_lastState;
}

std::uint64_t CommandHistory::stateId() const noexcept
{
    return m_done.empty() ? m_baseState : m_done.back().state;
}

} // namespace devex::tools
