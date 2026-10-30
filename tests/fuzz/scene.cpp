#include "Readers.hpp"

#include <devex/core/Log.hpp>

#include <cstdint>

extern "C" int LLVMFuzzerInitialize(int*, char***)
{
    devex::core::setLogLevel(devex::core::LogLevel::Error);
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    devex::tests::checkScene(std::as_bytes(std::span(data, size)));
    return 0;
}
