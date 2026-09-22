#include <devex/core/BuildInfo.hpp>
#include <devex/core/LogFile.hpp>
#include <devex/core/Path.hpp>

#include <array>
#include <chrono>
#include <ctime>
#include <print>
#include <system_error>

#ifdef _WIN32
#include <share.h>
#endif

namespace devex::core {
namespace {

[[nodiscard]] std::string_view fileName(std::string_view path) noexcept
{
    const auto separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? path : path.substr(separator + 1);
}

// The date and time of the machine, which says when a log was written once it is sent around.
[[nodiscard]] std::string localTime()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    std::array<char, 32> text{};
    const std::size_t length = std::strftime(text.data(), text.size(), "%Y-%m-%d %H:%M:%S", &local);
    return std::string(text.data(), length);
}

[[nodiscard]] std::FILE* openForWriting(const std::filesystem::path& path) noexcept
{
#ifdef _WIN32
    // The wide name, since a user folder is not always in the code page of the system; and shared
    // for reading, so that the log can be read while the program runs.
    return _wfsopen(path.c_str(), L"wb", _SH_DENYWR);
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

} // namespace

Result<std::unique_ptr<LogFile>> LogFile::open(const std::filesystem::path& path)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return makeError(ErrorCode::Io, "cannot create '{}': {}", toUtf8(path.parent_path()), error.message());
    }
    // The log of the run before stays beside the new one: it is the one that says why a game
    // stopped when it is started again to look.
    if (std::filesystem::exists(path, error))
    {
        std::filesystem::path previous = path;
        previous.replace_extension(".previous" + path.extension().string());
        std::filesystem::remove(previous, error);
        std::filesystem::rename(path, previous, error);
    }
    std::FILE* const file = openForWriting(path);
    if (file == nullptr)
    {
        return makeError(ErrorCode::Io, "cannot write '{}'", toUtf8(path));
    }
    std::unique_ptr<LogFile> log(new LogFile(path, file));
    std::println(file, "Devex Engine {} ({}), {}", version(), buildType(), localTime());
    std::fflush(file);
    log->m_sink = addLogSink([raw = log.get()](const LogRecord& record) { raw->write(record); });
    return log;
}

LogFile::LogFile(std::filesystem::path path, std::FILE* file)
    : m_path(std::move(path))
    , m_file(file)
{
}

LogFile::~LogFile()
{
    removeLogSink(m_sink);
    if (m_file != nullptr)
    {
        std::fclose(m_file);
    }
}

const std::filesystem::path& LogFile::path() const noexcept
{
    return m_path;
}

void LogFile::write(const LogRecord& record)
{
    const std::chrono::duration<double> elapsed = record.time - logStartTime();
    // The same lines as the console, so that both read alike.
    if (record.level >= LogLevel::Error && record.location.line() != 0)
    {
        std::println(m_file, "[{:9.3f}] {:<7} {} ({}:{})", elapsed.count(), toString(record.level),
                     record.message, fileName(record.location.file_name()), record.location.line());
    }
    else
    {
        std::println(m_file, "[{:9.3f}] {:<7} {}", elapsed.count(), toString(record.level),
                     record.message);
    }
    // Every line reaches the disk as it is written: a process killed from outside, which no
    // handler sees, still leaves its log up to the end. A log is a few lines a second at most.
    std::fflush(m_file);
}

void LogFile::writeRaw(std::string_view text) noexcept
{
    std::fwrite(text.data(), 1, text.size(), m_file);
    std::fflush(m_file);
}

void LogFile::flush() noexcept
{
    std::fflush(m_file);
}

} // namespace devex::core
