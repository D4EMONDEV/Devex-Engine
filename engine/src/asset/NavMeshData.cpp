#include <devex/asset/NavMeshData.hpp>

#include <cmath>

namespace devex::asset {

core::Result<void> validate(const NavMeshData& navMesh)
{
    const NavMeshBuildSettings& settings = navMesh.settings;
    const auto positive = [](float value) { return std::isfinite(value) && value > 0.0f; };
    if (!positive(settings.agentRadius) || !positive(settings.agentHeight) || !(settings.agentMaxClimb >= 0.0f) ||
        !positive(settings.agentMaxSlope) || !positive(settings.cellSize) || !positive(settings.cellHeight) ||
        settings.tileSize < 8 || settings.tileSize > 200)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the navigation mesh has invalid build settings");
    }
    if (navMesh.tilesX < 0 || navMesh.tilesZ < 0 || static_cast<std::int64_t>(navMesh.tilesX) * navMesh.tilesZ > (1 << 20))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the navigation mesh has an invalid grid of tiles");
    }
    for (const std::vector<std::byte>& layer : navMesh.layers)
    {
        if (layer.empty())
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "the navigation mesh has an empty layer");
        }
    }
    return {};
}

} // namespace devex::asset
