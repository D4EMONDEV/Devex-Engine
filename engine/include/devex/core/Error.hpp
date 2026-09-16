#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace devex::core {

enum class ErrorCode : std::uint16_t
{
    Unknown,
    InvalidArgument,
    InvalidState,
    NotFound,
    AlreadyExists,
    OutOfMemory,
    Io,
    Parse,
    Unsupported,
    Platform,
    Graphics,
};

[[nodiscard]] std::string_view toString(ErrorCode code) noexcept;

// A recoverable failure. Programming errors are reported with DEVEX_ASSERT instead.
struct Error
{
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
};

template <typename T = void>
using Result = std::expected<T, Error>;

template <typename... Args>
[[nodiscard]] std::unexpected<Error> makeError(ErrorCode code, std::format_string<Args...> format,
                                               Args&&... args)
{
    return std::unexpected<Error>(Error{code, std::format(format, std::forward<Args>(args)...)});
}

} // namespace devex::core

template <>
struct std::formatter<devex::core::ErrorCode> : std::formatter<std::string_view>
{
    auto format(devex::core::ErrorCode code, std::format_context& context) const
    {
        return std::formatter<std::string_view>::format(devex::core::toString(code), context);
    }
};

template <>
struct std::formatter<devex::core::Error> : std::formatter<std::string_view>
{
    auto format(const devex::core::Error& error, std::format_context& context) const
    {
        if (error.message.empty())
        {
            return std::format_to(context.out(), "{}", error.code);
        }
        return std::format_to(context.out(), "{}: {}", error.code, error.message);
    }
};
