#pragma once

#include <devex/core/Log.hpp>

#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace devex::tools {

struct LogEntry
{
    core::LogLevel level = core::LogLevel::Info;
    std::string message;
    std::chrono::steady_clock::time_point time;
};

// Keeps the most recent log messages for display, from the moment it is created.
class LogBuffer
{
public:
    explicit LogBuffer(std::size_t capacity = 2000);
    ~LogBuffer();

    LogBuffer(const LogBuffer&) = delete;
    LogBuffer& operator=(const LogBuffer&) = delete;

    // Calls the function for every entry, oldest first. The function must not log.
    void forEach(const std::function<void(const LogEntry& entry)>& function) const;
    [[nodiscard]] std::size_t size() const;
    void clear();

private:
    std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::deque<LogEntry> m_entries;
    core::LogSinkId m_sink{};
};

} // namespace devex::tools
