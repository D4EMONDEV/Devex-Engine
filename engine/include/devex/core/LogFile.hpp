#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Log.hpp>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <string_view>

namespace devex::core {

// Keeps the log of a run in a file, beside the log of the run before it, which is renamed
// <name>.previous.log. Every line reaches the disk as it is written, and the file can be read while
// the program runs.
class LogFile
{
public:
    // Creates the folder when it is missing, and starts receiving the log.
    [[nodiscard]] static Result<std::unique_ptr<LogFile>> open(const std::filesystem::path& path);

    ~LogFile();
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept;

    // Writes a text as it is, without the logger: a crash handler writes its report through this,
    // since the crash may have happened while the logger was busy.
    void writeRaw(std::string_view text) noexcept;
    void flush() noexcept;

private:
    LogFile(std::filesystem::path path, std::FILE* file);
    void write(const LogRecord& record);

    std::filesystem::path m_path;
    std::FILE* m_file = nullptr;
    LogSinkId m_sink{};
};

} // namespace devex::core
