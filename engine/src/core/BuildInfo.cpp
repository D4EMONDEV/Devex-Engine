#include <devex/core/BuildInfo.hpp>

namespace devex::core {

std::string_view version() noexcept
{
    return DEVEX_VERSION_STRING;
}

} // namespace devex::core
