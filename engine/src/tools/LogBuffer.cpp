#include <devex/tools/LogBuffer.hpp>

#include <algorithm>

namespace devex::tools {

LogBuffer::LogBuffer(std::size_t capacity)
    : m_capacity(std::max<std::size_t>(capacity, 1))
{
    m_sink = core::addLogSink([this](const core::LogRecord& record) {
        const std::scoped_lock lock(m_mutex);
        m_entries.push_back({record.level, std::string(record.message), record.time});
        if (m_entries.size() > m_capacity)
        {
            m_entries.pop_front();
        }
    });
}

LogBuffer::~LogBuffer()
{
    core::removeLogSink(m_sink);
}

void LogBuffer::forEach(const std::function<void(const LogEntry& entry)>& function) const
{
    const std::scoped_lock lock(m_mutex);
    for (const LogEntry& entry : m_entries)
    {
        function(entry);
    }
}

std::size_t LogBuffer::size() const
{
    const std::scoped_lock lock(m_mutex);
    return m_entries.size();
}

void LogBuffer::clear()
{
    const std::scoped_lock lock(m_mutex);
    m_entries.clear();
}

} // namespace devex::tools
