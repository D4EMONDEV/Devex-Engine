#include <devex/asset/IrradianceVolumeData.hpp>

#include <algorithm>
#include <cmath>

namespace devex::asset {

core::Result<void> validate(const IrradianceVolumeData& volume)
{
    if (volume.countX < 2 || volume.countY < 2 || volume.countZ < 2)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "an irradiance volume has fewer than two probes along an axis");
    }
    if (volume.probeCount() > maxIrradianceProbes)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "an irradiance volume has {} probes, more than {}", volume.probeCount(),
                               maxIrradianceProbes);
    }
    if (volume.coefficients.size() != volume.probeCount() * irradianceCoefficients)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "an irradiance volume of {} probes has {} coefficients",
                               volume.probeCount(), volume.coefficients.size());
    }
    if (!std::ranges::all_of(volume.coefficients, [](float value) { return std::isfinite(value); }))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "an irradiance volume holds a value that is not a number");
    }
    return {};
}

} // namespace devex::asset
