#pragma once

#include <cstdint>
#include <format>
#include <source_location>
#include <string_view>
#include <utility>

namespace devex::core {

enum class AssertAction : std::uint8_t
{
    Break,
    Continue,
};

struct AssertInfo
{
    std::string_view expression;
    std::string_view message;
    std::source_location location;
};

using AssertHandler = AssertAction (*)(const AssertInfo& info);

#ifdef DEVEX_ENABLE_ASSERTS
inline constexpr bool assertsEnabled = true;
#else
inline constexpr bool assertsEnabled = false;
#endif

// Installs a process-wide handler and returns the previous one. nullptr restores the default
// handler, which logs the failure and requests a debugger break.
AssertHandler setAssertHandler(AssertHandler handler) noexcept;

namespace detail {

[[nodiscard]] AssertAction reportAssertFailure(std::string_view expression,
                                               std::string_view message,
                                               std::source_location location);

[[noreturn]] void reportUnreachable(std::source_location location);

} // namespace detail

} // namespace devex::core

#if defined(_MSC_VER)
#define DEVEX_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
#define DEVEX_DEBUG_BREAK() __builtin_debugtrap()
#else
#define DEVEX_DEBUG_BREAK() __builtin_trap()
#endif

#ifdef DEVEX_ENABLE_ASSERTS

#define DEVEX_ASSERT(condition)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if (!(condition)) [[unlikely]]                                                             \
        {                                                                                          \
            if (::devex::core::detail::reportAssertFailure(#condition, {},                         \
                                                           std::source_location::current()) ==    \
                ::devex::core::AssertAction::Break)                                                \
            {                                                                                      \
                DEVEX_DEBUG_BREAK();                                                               \
            }                                                                                      \
        }                                                                                          \
    } while (false)

#define DEVEX_ASSERT_MSG(condition, ...)                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(condition)) [[unlikely]]                                                             \
        {                                                                                          \
            if (::devex::core::detail::reportAssertFailure(#condition, std::format(__VA_ARGS__),   \
                                                           std::source_location::current()) ==    \
                ::devex::core::AssertAction::Break)                                                \
            {                                                                                      \
                DEVEX_DEBUG_BREAK();                                                               \
            }                                                                                      \
        }                                                                                          \
    } while (false)

#define DEVEX_UNREACHABLE() ::devex::core::detail::reportUnreachable(std::source_location::current())

#else

// Disabled assertions still type-check their arguments but never evaluate them.
#define DEVEX_ASSERT(condition)                                                                    \
    do                                                                                             \
    {                                                                                              \
        if (false)                                                                                 \
        {                                                                                          \
            static_cast<void>(condition);                                                          \
        }                                                                                          \
    } while (false)

#define DEVEX_ASSERT_MSG(condition, ...)                                                           \
    do                                                                                             \
    {                                                                                              \
        if (false)                                                                                 \
        {                                                                                          \
            static_cast<void>(condition);                                                          \
            static_cast<void>(std::format(__VA_ARGS__));                                           \
        }                                                                                          \
    } while (false)

#define DEVEX_UNREACHABLE() std::unreachable()

#endif
