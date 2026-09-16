#include <devex/core/Assert.hpp>
#include <devex/core/Error.hpp>

namespace devex::core {

std::string_view toString(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::Unknown:
        return "Unknown";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::InvalidState:
        return "InvalidState";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::AlreadyExists:
        return "AlreadyExists";
    case ErrorCode::OutOfMemory:
        return "OutOfMemory";
    case ErrorCode::Io:
        return "Io";
    case ErrorCode::Parse:
        return "Parse";
    case ErrorCode::Unsupported:
        return "Unsupported";
    case ErrorCode::Platform:
        return "Platform";
    case ErrorCode::Graphics:
        return "Graphics";
    }
    DEVEX_UNREACHABLE();
}

} // namespace devex::core
