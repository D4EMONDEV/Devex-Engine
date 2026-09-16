#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace devex::render {

enum class GpuType : std::uint8_t
{
    Other,
    Integrated,
    Discrete,
    Virtual,
    Cpu,
};

[[nodiscard]] std::string_view toString(GpuType type) noexcept;

// Description of a graphics adapter, for display and diagnostics.
struct GpuInfo
{
    std::string name;
    GpuType type = GpuType::Other;
    std::string apiVersion;
    std::string driverName;
    std::string driverVersion;
};

} // namespace devex::render
