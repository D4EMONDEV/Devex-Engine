#include <devex/core/BuildInfo.hpp>

#include <array>
#include <charconv>
#include <compare>
#include <optional>
#include <system_error>

namespace devex::core {

namespace {

// The numbers of a version, none when it is not one.
[[nodiscard]] std::optional<std::array<int, 4>> partsOf(std::string_view version) noexcept
{
    std::array<int, 4> parts{};
    if (version.empty())
    {
        return std::nullopt;
    }
    std::size_t part = 0;
    const char* at = version.data();
    const char* const end = version.data() + version.size();
    while (at != end)
    {
        const auto [next, error] = std::from_chars(at, end, parts[part]);
        if (error != std::errc{} || (next != end && (*next != '.' || next + 1 == end)) || (next != end && part + 1 == parts.size()))
        {
            return std::nullopt;
        }
        at = next == end ? end : next + 1;
        ++part;
    }
    return parts;
}

} // namespace

int compareVersions(std::string_view left, std::string_view right) noexcept
{
    const auto first = partsOf(left);
    const auto second = partsOf(right);
    if (!first || !second)
    {
        return (first ? 1 : 0) - (second ? 1 : 0);
    }
    const auto order = *first <=> *second;
    return order < 0 ? -1 : (order > 0 ? 1 : 0);
}

std::string_view version() noexcept
{
    return DEVEX_VERSION_STRING;
}

std::string_view buildType() noexcept
{
    return DEVEX_BUILD_TYPE;
}

} // namespace devex::core
