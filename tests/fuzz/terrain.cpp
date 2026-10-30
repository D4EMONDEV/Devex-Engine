#include "Readers.hpp"

#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size)
{
    devex::tests::checkTerrain(std::as_bytes(std::span(data, size)));
    return 0;
}
