#include <devex/asset/TerrainMaps.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace devex::asset {

namespace {

// The height of a sample, the edges continuing outside the grid.
[[nodiscard]] float sampleHeight(const TerrainMaps& terrain, std::int64_t x, std::int64_t z) noexcept
{
    const auto last = static_cast<std::int64_t>(terrain.resolution) - 1;
    x = std::clamp<std::int64_t>(x, 0, last);
    z = std::clamp<std::int64_t>(z, 0, last);
    return terrain.heights[static_cast<std::size_t>(z) * terrain.resolution + static_cast<std::size_t>(x)];
}

// Samples per meter of a terrain of that size.
[[nodiscard]] float samplesPerMeter(const TerrainMaps& terrain, float size) noexcept
{
    return size > 0.0f && terrain.resolution > 1 ? static_cast<float>(terrain.resolution - 1) / size : 0.0f;
}

// A number from 0 to 1 that only depends on the point of the grid.
[[nodiscard]] float hashOf(std::int32_t x, std::int32_t z) noexcept
{
    std::uint32_t hash = static_cast<std::uint32_t>(x) * 374761393u + static_cast<std::uint32_t>(z) * 668265263u;
    hash = (hash ^ (hash >> 13)) * 1274126177u;
    hash ^= hash >> 16;
    return static_cast<float>(hash & 0xffffffu) / static_cast<float>(0x1000000);
}

// A smooth noise of the place, from 0 to 1.
[[nodiscard]] float valueNoise(float x, float z) noexcept
{
    const float cellX = std::floor(x);
    const float cellZ = std::floor(z);
    const auto ix = static_cast<std::int32_t>(cellX);
    const auto iz = static_cast<std::int32_t>(cellZ);
    const float fx = x - cellX;
    const float fz = z - cellZ;
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sz = fz * fz * (3.0f - 2.0f * fz);
    const float bottom = std::lerp(hashOf(ix, iz), hashOf(ix + 1, iz), sx);
    const float top = std::lerp(hashOf(ix, iz + 1), hashOf(ix + 1, iz + 1), sx);
    return std::lerp(bottom, top, sz);
}

// How much of a brush reaches a point at that share of its radius from its center.
[[nodiscard]] float brushWeight(float distance, float softness) noexcept
{
    if (distance >= 1.0f)
    {
        return 0.0f;
    }
    const float inner = 1.0f - std::clamp(softness, 0.0f, 1.0f);
    if (distance <= inner)
    {
        return 1.0f;
    }
    const float t = (distance - inner) / (1.0f - inner);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

[[nodiscard]] std::uint8_t toByte(float share) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(share, 0.0f, 1.0f) * 255.0f));
}

} // namespace

core::Result<void> validate(const TerrainMaps& terrain)
{
    const std::uint32_t cells = terrain.resolution > 0 ? terrain.resolution - 1 : 0;
    if (cells < 32 || cells > 4096 || !std::has_single_bit(cells))
    {
        return core::makeError(core::ErrorCode::InvalidArgument,
                               "a terrain has a power of two plus one samples on a side, from 33 to 4097, not {}", terrain.resolution);
    }
    const std::size_t samples = terrain.sampleCount();
    if (terrain.heights.size() != samples || terrain.paint.size() != samples * maxTerrainLayers ||
        terrain.holes.size() != samples || terrain.grass.size() != samples)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the samples of the terrain do not match its resolution of {}",
                               terrain.resolution);
    }
    if (!std::ranges::all_of(terrain.heights, [](float height) { return std::isfinite(height); }))
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a height of the terrain is not a number");
    }
    return {};
}

TerrainMaps makeFlatTerrain(std::uint32_t resolution, float height)
{
    TerrainMaps terrain{.resolution = resolution};
    const std::size_t samples = terrain.sampleCount();
    terrain.heights.assign(samples, height);
    terrain.paint.assign(samples * maxTerrainLayers, 0);
    terrain.holes.assign(samples, 0);
    terrain.grass.assign(samples, 0);
    return terrain;
}

float terrainHeight(const TerrainMaps& terrain, float size, float x, float z) noexcept
{
    const float scale = samplesPerMeter(terrain, size);
    if (scale <= 0.0f || terrain.heights.size() != terrain.sampleCount())
    {
        return 0.0f;
    }
    const float last = static_cast<float>(terrain.resolution - 1);
    const float gridX = std::clamp(x * scale, 0.0f, last);
    const float gridZ = std::clamp(z * scale, 0.0f, last);
    const float cellX = std::floor(gridX);
    const float cellZ = std::floor(gridZ);
    const auto ix = static_cast<std::int64_t>(cellX);
    const auto iz = static_cast<std::int64_t>(cellZ);
    const float fx = gridX - cellX;
    const float fz = gridZ - cellZ;
    const float bottom = std::lerp(sampleHeight(terrain, ix, iz), sampleHeight(terrain, ix + 1, iz), fx);
    const float top = std::lerp(sampleHeight(terrain, ix, iz + 1), sampleHeight(terrain, ix + 1, iz + 1), fx);
    return std::lerp(bottom, top, fz);
}

math::Vec3 terrainNormal(const TerrainMaps& terrain, float size, float x, float z) noexcept
{
    const float scale = samplesPerMeter(terrain, size);
    if (scale <= 0.0f)
    {
        return {0.0f, 1.0f, 0.0f};
    }
    const float spacing = 1.0f / scale;
    const float left = terrainHeight(terrain, size, x - spacing, z);
    const float right = terrainHeight(terrain, size, x + spacing, z);
    const float back = terrainHeight(terrain, size, x, z - spacing);
    const float front = terrainHeight(terrain, size, x, z + spacing);
    return math::normalize(math::Vec3{left - right, 2.0f * spacing, back - front});
}

bool isTerrainHole(const TerrainMaps& terrain, float size, float x, float z) noexcept
{
    const float scale = samplesPerMeter(terrain, size);
    if (scale <= 0.0f || terrain.holes.size() != terrain.sampleCount())
    {
        return false;
    }
    const auto last = static_cast<std::int64_t>(terrain.resolution) - 1;
    const auto ix = std::clamp<std::int64_t>(std::llround(x * scale), 0, last);
    const auto iz = std::clamp<std::int64_t>(std::llround(z * scale), 0, last);
    return terrain.holes[static_cast<std::size_t>(iz) * terrain.resolution + static_cast<std::size_t>(ix)] > 127;
}

std::optional<float> raycastTerrain(const TerrainMaps& terrain, float size, math::Vec3 origin, math::Vec3 direction,
                                    float maxDistance) noexcept
{
    const float scale = samplesPerMeter(terrain, size);
    const float length = math::length(direction);
    if (scale <= 0.0f || length <= 0.0f || maxDistance <= 0.0f)
    {
        return std::nullopt;
    }
    direction /= length;
    // Steps of half a cell, then halves between the last two to find the ground closely.
    const float step = 0.5f / scale;
    const auto inside = [size](math::Vec3 point) {
        return point.x >= 0.0f && point.z >= 0.0f && point.x <= size && point.z <= size;
    };
    const auto below = [&](float distance) {
        const math::Vec3 point = origin + direction * distance;
        return inside(point) && point.y <= terrainHeight(terrain, size, point.x, point.z);
    };
    float previous = 0.0f;
    bool wasBelow = below(0.0f);
    for (float distance = step; distance <= maxDistance + step; distance += step)
    {
        const float reached = std::min(distance, maxDistance);
        const bool isBelow = below(reached);
        if (isBelow && !wasBelow)
        {
            float low = previous;
            float high = reached;
            for (int iteration = 0; iteration < 12; ++iteration)
            {
                const float middle = (low + high) * 0.5f;
                (below(middle) ? high : low) = middle;
            }
            const math::Vec3 hit = origin + direction * high;
            if (!isTerrainHole(terrain, size, hit.x, hit.z))
            {
                return high;
            }
        }
        wasBelow = isBelow;
        previous = reached;
        if (reached >= maxDistance)
        {
            break;
        }
    }
    return std::nullopt;
}

std::vector<math::Vec2> terrainHeightRanges(const TerrainMaps& terrain, std::uint32_t cells)
{
    std::vector<math::Vec2> ranges;
    if (cells == 0 || terrain.resolution < 2 || terrain.heights.size() != terrain.sampleCount())
    {
        return ranges;
    }
    const std::uint32_t blocks = std::max((terrain.resolution - 1) / cells, 1u);
    ranges.reserve(std::size_t{blocks} * blocks);
    for (std::uint32_t blockZ = 0; blockZ < blocks; ++blockZ)
    {
        for (std::uint32_t blockX = 0; blockX < blocks; ++blockX)
        {
            math::Vec2 range{std::numeric_limits<float>::max(), std::numeric_limits<float>::lowest()};
            for (std::uint32_t z = blockZ * cells; z <= std::min((blockZ + 1) * cells, terrain.resolution - 1); ++z)
            {
                for (std::uint32_t x = blockX * cells; x <= std::min((blockX + 1) * cells, terrain.resolution - 1); ++x)
                {
                    const float height = terrain.heights[std::size_t{z} * terrain.resolution + x];
                    range.x = std::min(range.x, height);
                    range.y = std::max(range.y, height);
                }
            }
            ranges.push_back(range);
        }
    }
    return ranges;
}

TerrainRegion TerrainRegion::merged(const TerrainRegion& other) const noexcept
{
    if (empty())
    {
        return other;
    }
    if (other.empty())
    {
        return *this;
    }
    const std::uint32_t left = std::min(x, other.x);
    const std::uint32_t top = std::min(z, other.z);
    const std::uint32_t right = std::max(x + width, other.x + other.width);
    const std::uint32_t bottom = std::max(z + height, other.z + other.height);
    return {.x = left, .z = top, .width = right - left, .height = bottom - top};
}

TerrainRegion applyTerrainBrush(TerrainMaps& terrain, float size, const TerrainBrush& brush)
{
    const float scale = samplesPerMeter(terrain, size);
    const std::size_t samples = terrain.sampleCount();
    if (scale <= 0.0f || brush.radius <= 0.0f || terrain.heights.size() != samples ||
        terrain.paint.size() != samples * maxTerrainLayers || terrain.holes.size() != samples || terrain.grass.size() != samples)
    {
        return {};
    }
    const auto last = static_cast<std::int64_t>(terrain.resolution) - 1;
    const float centerX = brush.center.x * scale;
    const float centerZ = brush.center.y * scale;
    const float reach = brush.radius * scale;
    const auto first = [last](float value) { return std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(value)), 0, last); };
    const auto x0 = first(centerX - reach);
    const auto x1 = first(centerX + reach + 1.0f);
    const auto z0 = first(centerZ - reach);
    const auto z1 = first(centerZ + reach + 1.0f);
    if (centerX + reach < 0.0f || centerZ + reach < 0.0f || centerX - reach > static_cast<float>(last) ||
        centerZ - reach > static_cast<float>(last))
    {
        return {};
    }
    const std::uint32_t resolution = terrain.resolution;
    // Smoothing reads the heights as they were before this step, under the brush and one sample
    // around it.
    const std::int64_t windowX = std::max<std::int64_t>(x0 - 1, 0);
    const std::int64_t windowZ = std::max<std::int64_t>(z0 - 1, 0);
    const std::int64_t windowWidth = std::min<std::int64_t>(x1 + 1, last) - windowX + 1;
    std::vector<float> before;
    if (brush.tool == TerrainTool::Smooth)
    {
        for (std::int64_t z = windowZ; z <= std::min<std::int64_t>(z1 + 1, last); ++z)
        {
            const auto row = terrain.heights.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(z) * resolution) + windowX;
            before.insert(before.end(), row, row + windowWidth);
        }
    }
    const float noiseScale = std::max(brush.radius / 3.0f, 2.0f / scale);
    TerrainRegion changed;
    for (std::int64_t z = z0; z <= z1; ++z)
    {
        for (std::int64_t x = x0; x <= x1; ++x)
        {
            const float dx = (static_cast<float>(x) - centerX) / reach;
            const float dz = (static_cast<float>(z) - centerZ) / reach;
            const float weight = brushWeight(std::sqrt(dx * dx + dz * dz), brush.softness);
            if (weight <= 0.0f)
            {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(z) * resolution + static_cast<std::size_t>(x);
            const float share = std::clamp(brush.amount * weight, 0.0f, 1.0f);
            float& height = terrain.heights[index];
            switch (brush.tool)
            {
            case TerrainTool::Raise:
                height += brush.amount * weight;
                break;
            case TerrainTool::Lower:
                height -= brush.amount * weight;
                break;
            case TerrainTool::Smooth: {
                float sum = 0.0f;
                for (std::int64_t oz = -1; oz <= 1; ++oz)
                {
                    for (std::int64_t ox = -1; ox <= 1; ++ox)
                    {
                        const auto sx = std::clamp<std::int64_t>(x + ox, 0, last) - windowX;
                        const auto sz = std::clamp<std::int64_t>(z + oz, 0, last) - windowZ;
                        sum += before[static_cast<std::size_t>(sz * windowWidth + sx)];
                    }
                }
                height = std::lerp(height, sum / 9.0f, share);
                break;
            }
            case TerrainTool::Flatten:
                height = std::lerp(height, brush.height, share);
                break;
            case TerrainTool::Noise: {
                // Meters across, so that the bumps stay put as the brush moves over them.
                const float px = static_cast<float>(x) / scale / noiseScale;
                const float pz = static_cast<float>(z) / scale / noiseScale;
                const float noise = valueNoise(px, pz) * 0.7f + valueNoise(px * 2.3f + 17.0f, pz * 2.3f + 5.0f) * 0.3f;
                height += (noise * 2.0f - 1.0f) * brush.amount * weight;
                break;
            }
            case TerrainTool::Paint: {
                if (brush.layer >= maxTerrainLayers)
                {
                    break;
                }
                std::uint8_t* const shares = terrain.paint.data() + index * maxTerrainLayers;
                const float own = static_cast<float>(shares[brush.layer]) / 255.0f;
                const float painted = brush.erase ? own * (1.0f - share) : own + (1.0f - own) * share;
                // The others give way, so that the shares never add up to more than all of it.
                float others = 0.0f;
                for (std::uint32_t layer = 0; layer < maxTerrainLayers; ++layer)
                {
                    others += layer != brush.layer ? static_cast<float>(shares[layer]) / 255.0f : 0.0f;
                }
                if (!brush.erase && others + painted > 1.0f && others > 0.0f)
                {
                    const float kept = std::max(1.0f - painted, 0.0f) / others;
                    for (std::uint32_t layer = 0; layer < maxTerrainLayers; ++layer)
                    {
                        if (layer != brush.layer)
                        {
                            shares[layer] = toByte(static_cast<float>(shares[layer]) / 255.0f * kept);
                        }
                    }
                }
                shares[brush.layer] = toByte(painted);
                // What rounding added comes off the largest of the others.
                unsigned total = 0;
                for (std::uint32_t layer = 0; layer < maxTerrainLayers; ++layer)
                {
                    total += shares[layer];
                }
                if (total > 255)
                {
                    std::uint32_t largest = brush.layer == 0 ? 1 : 0;
                    for (std::uint32_t layer = 0; layer < maxTerrainLayers; ++layer)
                    {
                        largest = layer != brush.layer && shares[layer] > shares[largest] ? layer : largest;
                    }
                    shares[largest] = static_cast<std::uint8_t>(shares[largest] - std::min<unsigned>(total - 255, shares[largest]));
                }
                break;
            }
            case TerrainTool::Hole:
                if (weight >= 0.5f)
                {
                    terrain.holes[index] = brush.erase ? 0 : 255;
                }
                break;
            case TerrainTool::Grass: {
                const float density = static_cast<float>(terrain.grass[index]) / 255.0f;
                terrain.grass[index] = toByte(brush.erase ? density * (1.0f - share) : density + (1.0f - density) * share);
                break;
            }
            }
            changed = changed.merged({.x = static_cast<std::uint32_t>(x), .z = static_cast<std::uint32_t>(z), .width = 1, .height = 1});
        }
    }
    return changed;
}

TerrainPatch copyTerrainRegion(const TerrainMaps& terrain, const TerrainRegion& region)
{
    TerrainPatch patch{.region = region};
    if (region.empty() || region.x + region.width > terrain.resolution || region.z + region.height > terrain.resolution)
    {
        patch.region = {};
        return patch;
    }
    const std::size_t samples = std::size_t{region.width} * region.height;
    patch.heights.reserve(samples);
    patch.paint.reserve(samples * maxTerrainLayers);
    patch.holes.reserve(samples);
    patch.grass.reserve(samples);
    for (std::uint32_t z = region.z; z < region.z + region.height; ++z)
    {
        const std::size_t row = std::size_t{z} * terrain.resolution + region.x;
        patch.heights.insert(patch.heights.end(), terrain.heights.begin() + static_cast<std::ptrdiff_t>(row),
                             terrain.heights.begin() + static_cast<std::ptrdiff_t>(row + region.width));
        patch.paint.insert(patch.paint.end(), terrain.paint.begin() + static_cast<std::ptrdiff_t>(row * maxTerrainLayers),
                           terrain.paint.begin() + static_cast<std::ptrdiff_t>((row + region.width) * maxTerrainLayers));
        patch.holes.insert(patch.holes.end(), terrain.holes.begin() + static_cast<std::ptrdiff_t>(row),
                           terrain.holes.begin() + static_cast<std::ptrdiff_t>(row + region.width));
        patch.grass.insert(patch.grass.end(), terrain.grass.begin() + static_cast<std::ptrdiff_t>(row),
                           terrain.grass.begin() + static_cast<std::ptrdiff_t>(row + region.width));
    }
    return patch;
}

void pasteTerrainPatch(TerrainMaps& terrain, const TerrainPatch& patch)
{
    const TerrainRegion& region = patch.region;
    const std::size_t samples = std::size_t{region.width} * region.height;
    if (region.empty() || region.x + region.width > terrain.resolution || region.z + region.height > terrain.resolution ||
        patch.heights.size() != samples || patch.paint.size() != samples * maxTerrainLayers || patch.holes.size() != samples ||
        patch.grass.size() != samples)
    {
        return;
    }
    for (std::uint32_t row = 0; row < region.height; ++row)
    {
        const std::size_t to = std::size_t{region.z + row} * terrain.resolution + region.x;
        const std::size_t from = std::size_t{row} * region.width;
        std::copy_n(patch.heights.begin() + static_cast<std::ptrdiff_t>(from), region.width,
                    terrain.heights.begin() + static_cast<std::ptrdiff_t>(to));
        std::copy_n(patch.paint.begin() + static_cast<std::ptrdiff_t>(from * maxTerrainLayers), region.width * maxTerrainLayers,
                    terrain.paint.begin() + static_cast<std::ptrdiff_t>(to * maxTerrainLayers));
        std::copy_n(patch.holes.begin() + static_cast<std::ptrdiff_t>(from), region.width,
                    terrain.holes.begin() + static_cast<std::ptrdiff_t>(to));
        std::copy_n(patch.grass.begin() + static_cast<std::ptrdiff_t>(from), region.width,
                    terrain.grass.begin() + static_cast<std::ptrdiff_t>(to));
    }
}

} // namespace devex::asset
