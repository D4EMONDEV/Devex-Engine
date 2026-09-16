#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>

#include <atomic>
#include <cstdlib>

namespace devex::core {
namespace {

AssertAction defaultAssertHandler(const AssertInfo& info)
{
    if (info.message.empty())
    {
        logMessage(LogLevel::Fatal, std::format("Assertion failed: {}", info.expression),
                   info.location);
    }
    else
    {
        logMessage(LogLevel::Fatal,
                   std::format("Assertion failed: {} ({})", info.expression, info.message),
                   info.location);
    }
    return AssertAction::Break;
}

std::atomic<AssertHandler> assertHandler{&defaultAssertHandler};

} // namespace

AssertHandler setAssertHandler(AssertHandler handler) noexcept
{
    return assertHandler.exchange(handler != nullptr ? handler : &defaultAssertHandler);
}

namespace detail {

AssertAction reportAssertFailure(std::string_view expression, std::string_view message,
                                 std::source_location location)
{
    return assertHandler.load()({expression, message, location});
}

void reportUnreachable(std::source_location location)
{
    if (assertHandler.load()({"unreachable code", {}, location}) == AssertAction::Break)
    {
        DEVEX_DEBUG_BREAK();
    }
    std::abort();
}

} // namespace detail

} // namespace devex::core
