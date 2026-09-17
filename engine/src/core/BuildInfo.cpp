#include <devex/core/BuildInfo.hpp>

namespace devex::core {

std::string_view version() noexcept
{
    return DEVEX_VERSION_STRING;
}

std::string_view buildType() noexcept
{
    return DEVEX_BUILD_TYPE;
}

} // namespace devex::core
