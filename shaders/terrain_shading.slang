// Terrains, shared by Vulkan (terrain.slang) and WebGPU (webgpu_terrain.slang), generic over where their maps and the
// textures of their layers are read: a square grid drawn in blocks around the camera, each with more detail the closer
// it is, whose vertices take their height from the map of the terrain, after Strugar's continuous distance-dependent
// level of detail (CDLOD, 2009). The vertices of a block slide towards those of the coarser blocks around it as they
// near their distance, so that no crack opens between two levels of detail. The ground is shaded by up to eight
// material layers, painted or laid by rules on slopes and heights, and grass grows where it was painted.
module terrain_shading;

// A terrain, as both renderers pack it.
public struct TerrainShape
{
    // Its corner in the world, and the meters of its side.
    public float3 origin;
    public float size;
    public uint resolution;
    public uint layerCount;
    public float textureSize;
    public float blendSharpness;
    public uint triplanar;
    public int autoBaseLayer;
    public int autoSlopeLayer;
    public float autoSlope;
    public int autoHeightLayer;
    public float autoHeight;
    public float autoBlend;
    public float windStrength;
    public float windSpeed;
    public float grassDistance;
    public float3 grassColor;
    public float grassHeight;
    public float3 grassTipColor;
    public float grassWidth;
    // The distance within which the finest blocks are drawn; each coarser level reaches twice as far.
    public float leafRange;
};

// A block of the grid: its corner in the space of the terrain, its side and its level of detail; or a square where
// grass grows.
public struct TerrainNode
{
    public float2 corner;
    public float size;
    public uint level;
};

// The maps of a terrain: the heights read at a sample without filtering, which not every GPU does on floats, and the
// others between their samples.
public static const uint terrainSlopes = 0;
public static const uint terrainPaint0 = 1;
public static const uint terrainPaint1 = 2;
public static const uint terrainDetail = 3;

public interface ITerrainMaps
{
    float heightAt(int2 sample);
    float4 map(uint kind, float2 uv);
};

// The layers of a terrain: the factors of their materials, and their textures, with the slopes of their coordinates,
// taken where every pixel of a quad still goes the same way.
public static const uint layerBaseColor = 0;
public static const uint layerMetallicRoughness = 1;
public static const uint layerNormal = 2;

public interface ITerrainLayers
{
    float4 baseColorFactor(uint layer);
    // Metallic, roughness and the scale of the normal map.
    float3 factors(uint layer);
    float4 read(uint layer, uint kind, float2 uv, float2 slopeX, float2 slopeY);
};

public static const uint maxTerrainLayers = 8;

// Where a point of the terrain is in its maps, in texture coordinates at the centers of the samples.
public float2 mapUv(TerrainShape terrain, float2 local)
{
    const float cells = float(terrain.resolution - 1);
    return (local / terrain.size * cells + 0.5) / float(terrain.resolution);
}

// The height of the ground at a point of the terrain, between its samples.
public float terrainHeight<M : ITerrainMaps>(M maps, TerrainShape terrain, float2 local)
{
    const float last = float(terrain.resolution - 1);
    const float2 grid = clamp(local / terrain.size * last, 0.0, last);
    const int2 cell = int2(min(floor(grid), last - 1.0));
    const float2 blend = grid - float2(cell);
    const float bottom = lerp(maps.heightAt(cell), maps.heightAt(cell + int2(1, 0)), blend.x);
    const float top = lerp(maps.heightAt(cell + int2(0, 1)), maps.heightAt(cell + int2(1, 1)), blend.x);
    return lerp(bottom, top, blend.y);
}

// The normal of the ground, from the slopes between its samples.
public float3 terrainNormal<M : ITerrainMaps>(M maps, TerrainShape terrain, float2 local)
{
    const float spacing = terrain.size / float(terrain.resolution - 1);
    const float2 slope = maps.map(terrainSlopes, mapUv(terrain, local)).rg;
    return normalize(float3(-slope.x / spacing, 1.0, -slope.y / spacing));
}

public bool isHole<M : ITerrainMaps>(M maps, TerrainShape terrain, float2 local)
{
    return maps.map(terrainDetail, mapUv(terrain, local)).r > 0.5;
}

public struct TerrainVertex
{
    public float3 world;
    public float2 local;
};

// A vertex of a block: its place on the grid, slid towards the coarser grid around the block as the camera gets
// farther from it, then lifted to the height of the ground.
public TerrainVertex terrainVertex<M : ITerrainMaps>(M maps, TerrainShape terrain, TerrainNode node, uint gridCells, uint vertexIndex,
                                                     float3 cameraPosition)
{
    const uint row = gridCells + 1;
    float2 grid = float2(float(vertexIndex % row), float(vertexIndex / row));
    const float cell = node.size / float(gridCells);

    float2 local = node.corner + grid * cell;
    const float3 still = terrain.origin + float3(local.x, terrainHeight(maps, terrain, local), local.y);
    const float distance = length(still - cameraPosition);
    const float rangeEnd = terrain.leafRange * exp2(float(node.level));
    const float morphStart = rangeEnd * (node.level == 0 ? 0.7 : 0.85);
    const float morph = saturate((distance - morphStart) / max(rangeEnd - morphStart, 0.001));
    // The odd vertices slide onto their even neighbours, those of the grid twice as coarse.
    grid -= frac(grid * 0.5) * 2.0 * morph;
    local = clamp(node.corner + grid * cell, 0.0, terrain.size);

    TerrainVertex vertex;
    vertex.local = local;
    vertex.world = terrain.origin + float3(local.x, terrainHeight(maps, terrain, local), local.y);
    return vertex;
}

// What a layer looks like at a point: its colour, its normal in the world, its roughness and metallic.
public struct LayerSample
{
    public float3 albedo;
    public float3 normal;
    public float roughness;
    public float metallic;
};

// A normal map read along one axis, as Golus's "UDN" blend of triplanar normals turns it into the world: its tilt
// added to the normal of the ground, in the plane of that axis.
float3 tiltOf<L : ITerrainLayers>(L layers, uint layer, float2 uv, float2 slopeX, float2 slopeY)
{
    const float2 encoded = layers.read(layer, layerNormal, uv, slopeX, slopeY).rg * 2.0 - 1.0;
    return float3(encoded * layers.factors(layer).z, 0.0);
}

LayerSample sampleLayer<L : ITerrainLayers>(L layers, TerrainShape terrain, uint layer, float3 world, float3 worldDx, float3 worldDy,
                                           float3 ground, float3 axes)
{
    const float scale = 1.0 / terrain.textureSize;
    LayerSample result;
    float4 color = float4(0.0);
    float4 surface = float4(0.0);
    // From above, then along X and Z where the ground is steep enough for them to show.
    const float2 top = world.xz * scale;
    const float2 topDx = worldDx.xz * scale;
    const float2 topDy = worldDy.xz * scale;
    color += layers.read(layer, layerBaseColor, top, topDx, topDy) * axes.y;
    surface += layers.read(layer, layerMetallicRoughness, top, topDx, topDy) * axes.y;
    const float3 tiltTop = tiltOf(layers, layer, top, topDx, topDy);
    float3 bent = float3(tiltTop.x, 0.0, tiltTop.y) * axes.y;
    if (axes.x > 0.0)
    {
        const float2 side = world.zy * scale;
        const float2 sideDx = worldDx.zy * scale;
        const float2 sideDy = worldDy.zy * scale;
        color += layers.read(layer, layerBaseColor, side, sideDx, sideDy) * axes.x;
        surface += layers.read(layer, layerMetallicRoughness, side, sideDx, sideDy) * axes.x;
        const float3 tilt = tiltOf(layers, layer, side, sideDx, sideDy);
        bent += float3(0.0, tilt.y, tilt.x) * axes.x;
    }
    if (axes.z > 0.0)
    {
        const float2 side = world.xy * scale;
        const float2 sideDx = worldDx.xy * scale;
        const float2 sideDy = worldDy.xy * scale;
        color += layers.read(layer, layerBaseColor, side, sideDx, sideDy) * axes.z;
        surface += layers.read(layer, layerMetallicRoughness, side, sideDx, sideDy) * axes.z;
        const float3 tilt = tiltOf(layers, layer, side, sideDx, sideDy);
        bent += float3(tilt.x, tilt.y, 0.0) * axes.z;
    }
    const float3 factors = layers.factors(layer);
    result.albedo = layers.baseColorFactor(layer).rgb * color.rgb;
    result.normal = normalize(ground + bent);
    result.roughness = clamp(factors.y * surface.g, 0.045, 1.0);
    result.metallic = saturate(factors.x * surface.b);
    return result;
}

// The share of each layer at a point: what was painted, and the rules on what was not.
void layerWeights<M : ITerrainMaps>(M maps, TerrainShape terrain, float2 local, float3 normal, float height, out float weights[maxTerrainLayers])
{
    const float2 uv = mapUv(terrain, local);
    const float4 first = maps.map(terrainPaint0, uv);
    const float4 second = maps.map(terrainPaint1, uv);
    for (uint layer = 0; layer < 4; ++layer)
    {
        weights[layer] = first[layer];
        weights[layer + 4] = second[layer];
    }
    float painted = 0.0;
    for (uint layer = 0; layer < maxTerrainLayers; ++layer)
    {
        painted += weights[layer];
    }
    const float rules = saturate(1.0 - painted);
    if (rules <= 0.0)
    {
        return;
    }
    float automatic[maxTerrainLayers] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    const uint count = terrain.layerCount;
    if (terrain.autoBaseLayer >= 0 && uint(terrain.autoBaseLayer) < count)
    {
        automatic[terrain.autoBaseLayer] = 1.0;
    }
    if (terrain.autoSlopeLayer >= 0 && uint(terrain.autoSlopeLayer) < count)
    {
        const float angle = acos(saturate(normal.y));
        const float width = max(terrain.autoSlope * terrain.autoBlend, 0.001);
        const float steep = smoothstep(terrain.autoSlope - width, terrain.autoSlope + width, angle);
        for (uint layer = 0; layer < maxTerrainLayers; ++layer)
        {
            automatic[layer] *= 1.0 - steep;
        }
        automatic[terrain.autoSlopeLayer] += steep;
    }
    if (terrain.autoHeightLayer >= 0 && uint(terrain.autoHeightLayer) < count)
    {
        const float width = max(abs(terrain.autoHeight) * terrain.autoBlend, 0.5);
        const float high = smoothstep(terrain.autoHeight - width, terrain.autoHeight + width, height);
        for (uint layer = 0; layer < maxTerrainLayers; ++layer)
        {
            automatic[layer] *= 1.0 - high;
        }
        automatic[terrain.autoHeightLayer] += high;
    }
    for (uint layer = 0; layer < maxTerrainLayers; ++layer)
    {
        weights[layer] += automatic[layer] * rules;
    }
}

// The ground at a point, from its layers: each that shows sampled once; where they meet, the brighter texels of each
// show through first, as the tops of stones show through sand. A neutral ground without layers.
public LayerSample terrainGround<M : ITerrainMaps, L : ITerrainLayers>(M maps, L layers, TerrainShape terrain, float3 world,
                                                                       float3 worldDx, float3 worldDy, float2 local)
{
    const float3 ground = terrainNormal(maps, terrain, local);
    // The axes the textures are seen along: from above alone on gentle slopes.
    float3 axes = float3(0.0, 1.0, 0.0);
    if (terrain.triplanar != 0)
    {
        axes = pow(abs(ground), 4.0);
        axes /= axes.x + axes.y + axes.z;
        // Too little of a side is left out, which spares its samples.
        axes.x = axes.x < 0.02 ? 0.0 : axes.x;
        axes.z = axes.z < 0.02 ? 0.0 : axes.z;
        axes /= axes.x + axes.y + axes.z;
    }

    float weights[maxTerrainLayers];
    layerWeights(maps, terrain, local, ground, world.y - terrain.origin.y, weights);
    const uint count = min(terrain.layerCount, maxTerrainLayers);
    LayerSample samples[maxTerrainLayers];
    float heights[maxTerrainLayers];
    float highest = 0.0;
    for (uint layer = 0; layer < count; ++layer)
    {
        heights[layer] = 0.0;
        if (weights[layer] > 0.002)
        {
            samples[layer] = sampleLayer(layers, terrain, layer, world, worldDx, worldDy, ground, axes);
            const float bright = dot(samples[layer].albedo, float3(0.2126, 0.7152, 0.0722));
            heights[layer] = weights[layer] + bright * terrain.blendSharpness;
            highest = max(highest, heights[layer]);
        }
    }
    LayerSample result;
    result.albedo = float3(0.0);
    result.normal = float3(0.0);
    result.roughness = 0.0;
    result.metallic = 0.0;
    float total = 0.0;
    const float depth = lerp(1.0, 0.08, terrain.blendSharpness);
    for (uint layer = 0; layer < count; ++layer)
    {
        if (weights[layer] <= 0.002)
        {
            continue;
        }
        const float share = max(heights[layer] - (highest - depth), 0.0) * weights[layer];
        result.albedo += samples[layer].albedo * share;
        result.normal += samples[layer].normal * share;
        result.roughness += samples[layer].roughness * share;
        result.metallic += samples[layer].metallic * share;
        total += share;
    }
    if (total <= 0.0)
    {
        result.albedo = float3(0.36, 0.34, 0.3);
        result.normal = ground;
        result.roughness = 0.9;
        result.metallic = 0.0;
        total = 1.0;
    }
    result.albedo /= total;
    result.normal = normalize(result.normal);
    result.roughness /= total;
    result.metallic /= total;
    return result;
}

// Grass: blades scattered over the tiles around the camera, at places that only depend on the tile, so that they stay
// put as the camera moves; as many of them grow as the grass painted there says.

float hashOf(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return float(value & 0xffffffu) / 16777216.0;
}

public struct GrassBlade
{
    // Whether the blade grows at all; a blade that does not leaves its vertices at one point.
    public bool grows;
    public float3 world;
    public float3 normal;
    // From 0 at the foot of the blade to 1 at its tip.
    public float along;
};

// A vertex of a blade: seven of them, two at each of three heights, then the tip.
public GrassBlade bladeVertex<M : ITerrainMaps>(M maps, TerrainShape terrain, TerrainNode tile, uint blade, float bladeWidthScale,
                                               uint vertexIndex, float3 cameraPosition, float time)
{
    // The seed of a blade is its tile in the world and its number in it.
    const int2 place = int2(floor(tile.corner / tile.size));
    const uint seed = ((uint(place.x) * 73856093u) ^ (uint(place.y) * 19349663u)) + blade * 2654435761u;
    const float2 local = tile.corner + float2(hashOf(seed), hashOf(seed + 1u)) * tile.size;

    GrassBlade output;
    output.grows = false;
    output.world = float3(0.0);
    output.normal = float3(0.0, 1.0, 0.0);
    output.along = 0.0;
    if (any(local < 0.0) || any(local > terrain.size))
    {
        return output;
    }
    // Fewer blades where less grass was painted, and towards the end of their distance.
    const float4 detail = maps.map(terrainDetail, mapUv(terrain, local));
    const float3 foot = terrain.origin + float3(local.x, terrainHeight(maps, terrain, local), local.y);
    const float distance = length(foot - cameraPosition);
    const float fade = 1.0 - smoothstep(terrain.grassDistance * 0.7, terrain.grassDistance, distance);
    if (detail.r > 0.5 || hashOf(seed + 2u) >= detail.g * fade)
    {
        return output;
    }

    const uint level = min(vertexIndex / 2, 3u);
    const float side = vertexIndex >= 6 ? 0.0 : (vertexIndex % 2 == 0 ? -1.0 : 1.0);
    const float along = float(level) / 3.0;
    const float angle = hashOf(seed + 3u) * 6.2831853;
    const float3 across = float3(cos(angle), 0.0, sin(angle));
    const float3 facing = float3(-across.z, 0.0, across.x);
    const float height = terrain.grassHeight * (0.65 + 0.7 * hashOf(seed + 4u));
    const float width = terrain.grassWidth * bladeWidthScale * (1.0 - along * 0.85);
    // Bent forward a little, and swaying in the wind, the tip most.
    const float phase = dot(foot.xz, float2(0.13, 0.17)) + hashOf(seed + 5u) * 1.5;
    const float sway = sin(time * terrain.windSpeed + phase) * 0.6 + sin(time * terrain.windSpeed * 2.3 + phase * 1.7) * 0.25;
    const float3 wind = normalize(float3(1.0, 0.0, 0.35)) * terrain.windStrength * sway;
    const float bend = along * along;
    output.grows = true;
    output.world = foot + across * side * width + float3(0.0, height * along, 0.0) + (facing * 0.25 * height + wind * height) * bend;
    output.normal = normalize(lerp(terrainNormal(maps, terrain, local), facing, 0.4));
    output.along = along;
    return output;
}
