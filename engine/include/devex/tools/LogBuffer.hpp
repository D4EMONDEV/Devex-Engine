#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/Log.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace devex::tools {

struct DEVEX_API LogEntry
{
    core::LogLevel level = core::LogLevel::Info;
    std::string message;
    std::chrono::steady_clock::time_point time;
    // Counts the entries from the first one, never going back: what a reader already has.
    std::uint64_t sequence = 0;
};

// Keeps the most recent log messages for display, from the moment it is created.
class DEVEX_API LogBuffer
{
public:
    explicit LogBuffer(std::size_t capacity = 2000);
    ~LogBuffer();

    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;

    // Calls the function for every entry, oldest first. The function must not log.
    void forEach(const std::function<void(const LogEntry& entry)>& function) const;
    // The same for the entries that came after a sequence number, for a reader that keeps a copy.
    void forEachAfter(std::uint64_t sequence, const std::function<void(const LogEntry& entry)>& function) const;
    // The sequence number of the oldest entry kept; what came before was dropped or cleared.
    [[nodiscard]] std::uint64_t oldest() const;
    [[nodiscard]] std::size_t size() const;
    void clear();

private:
    std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    std::uint64_t m_next = 1;
    core::LogSinkId m_sink{};
};

} // namespace devex::tools
