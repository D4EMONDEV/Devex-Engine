// Terrains: a square grid drawn in blocks around the camera, each with more detail the closer it is,
// whose vertices take their height from the map of the terrain, after Strugar's continuous
// distance-dependent level of detail (CDLOD, 2009). The vertices of a block slide towards those of
// the coarser blocks around it as they near their distance, so that no crack opens between two
// levels of detail. The ground is shaded by up to eight material layers, painted or laid by rules on
// slopes and heights, and grass grows where it was painted.

import common;
import pbr;

struct Terrain
{
    // Its corner in the world, and the meters of its side.
    float3 origin;
    float size;
    // Slots of its maps among the textures: heights, slopes (meters of height per cell along X and
    // Z), the shares of layers 0 to 3 and 4 to 7, and the holes and the grass.
    uint heights;
    uint slopes;
    uint paint0;
    uint paint1;
    uint detail;
    uint resolution;
    uint layerCount;
    uint objectId;
    // The materials of its layers.
    uint4 layers[2];
    float textureSize;
    float blendSharpness;
    uint triplanar;
    int autoBaseLayer;
    int autoSlopeLayer;
    float autoSlope;
    int autoHeightLayer;
    float autoHeight;
    float autoBlend;
    float windStrength;
    float windSpeed;
    float grassDistance;
    float3 grassColor;
    float grassHeight;
    float3 grassTipColor;
    float grassWidth;
    float grassDensity;
    // The distance within which the finest blocks are drawn; each coarser level reaches twice as far.
    float leafRange;
    float padding0;
    float padding1;
};

// A block of the grid: its corner in the space of the terrain, its side and its level of detail.
struct TerrainNode
{
    float2 corner;
    float size;
    uint level;
};

// A square of the ground where grass grows, and how many blades each one gets.
struct GrassTile
{
    float2 corner;
    float size;
    uint blades;
};

struct TerrainData
{
    SceneData* scene;
    Terrain* terrain;
    TerrainNode* nodes;
    GrassTile* tiles;
    uint cascade;
    // Cells on a side of a block.
    uint gridCells;
    // Grass: blades per tile, and how much wider they are to cover for the ones left out far away.
    uint bladesPerTile;
    float bladeWidthScale;
};

[[vk::push_constant]]
ConstantBuffer<TerrainData> data;

static const uint maxLayers = 8;

// Where a point of the terrain is in its maps, in texture coordinates at the centers of the samples.
float2 mapUv(Terrain* terrain, float2 local)
{
    const float cells = float(terrain->resolution - 1);
    return (local / terrain->size * cells + 0.5) / float(terrain->resolution);
}

// The height of the ground at a point of the terrain, between its samples: the map of floats is read
// without filtering, which not every GPU does on them.
float heightAt(Terrain* terrain, float2 local)
{
    const float last = float(terrain->resolution - 1);
    const float2 grid = clamp(local / terrain->size * last, 0.0, last);
    const int2 cell = int2(min(floor(grid), last - 1.0));
    const float2 blend = grid - float2(cell);
    let map = textures[NonUniformResourceIndex(terrain->heights)];
    const float bottom = lerp(map.Load(int3(cell, 0)).r, map.Load(int3(cell + int2(1, 0), 0)).r, blend.x);
    const float top = lerp(map.Load(int3(cell + int2(0, 1), 0)).r, map.Load(int3(cell + int2(1, 1), 0)).r, blend.x);
    return lerp(bottom, top, blend.y);
}

float4 mapAt(uint slot, float2 uv)
{
    return textures[NonUniformResourceIndex(slot)].SampleLevel(clampSampler, uv, 0.0);
}

// The normal of the ground, from the slopes between its samples.
float3 normalAt(Terrain* terrain, float2 local)
{
    const float spacing = terrain->size / float(terrain->resolution - 1);
    const float2 slope = mapAt(terrain->slopes, mapUv(terrain, local)).rg;
    return normalize(float3(-slope.x / spacing, 1.0, -slope.y / spacing));
}

bool isHole(Terrain* terrain, float2 local)
{
    return mapAt(terrain->detail, mapUv(terrain, local)).r > 0.5;
}

struct TerrainVertex
{
    float3 world;
    float2 local;
};

// A vertex of a block: its place on the grid, slid towards the coarser grid around the block as the
// camera gets farther from it, then lifted to the height of the ground.
TerrainVertex terrainVertex(uint vertexIndex, uint instanceIndex)
{
    let terrain = data.terrain;
    let node = data.nodes[instanceIndex];
    const uint row = data.gridCells + 1;
    float2 grid = float2(float(vertexIndex % row), float(vertexIndex / row));
    const float cell = node.size / float(data.gridCells);

    float2 local = node.corner + grid * cell;
    const float3 still = terrain->origin + float3(local.x, heightAt(terrain, local), local.y);
    const float distance = length(still - data.scene->cameraPosition);
    const float rangeEnd = terrain->leafRange * exp2(float(node.level));
    const float morphStart = rangeEnd * (node.level == 0 ? 0.7 : 0.85);
    const float morph = saturate((distance - morphStart) / max(rangeEnd - morphStart, 0.001));
    // The odd vertices slide onto their even neighbours, those of the grid twice as coarse.
    grid -= frac(grid * 0.5) * 2.0 * morph;
    local = clamp(node.corner + grid * cell, 0.0, terrain->size);

    TerrainVertex vertex;
    vertex.local = local;
    vertex.world = terrain->origin + float3(local.x, heightAt(terrain, local), local.y);
    return vertex;
}

// The prepass: the depth, the motion of the pixels and the normals. The ground does not move: its
// pixels move as the camera does.

struct PrepassOutput
{
    float4 position : SV_Position;
    float4 current : TEXCOORD0;
    float4 previous : TEXCOORD1;
    float2 local : TEXCOORD2;
};

[shader("vertex")]
PrepassOutput prepassVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const TerrainVertex vertex = terrainVertex(vertexIndex, instanceIndex);
    PrepassOutput output;
    output.position = mul(data.scene->viewProjection, float4(vertex.world, 1.0));
    output.current = mul(data.scene->unjitteredViewProjection, float4(vertex.world, 1.0));
    output.previous = mul(data.scene->previousViewProjection, float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

struct PrepassTargets
{
    float2 velocity : SV_Target0;
    float2 normal : SV_Target1;
};

[shader("fragment")]
PrepassTargets prepassFragment(PrepassOutput input)
{
    if (isHole(data.terrain, input.local))
    {
        discard;
    }
    PrepassTargets output;
    output.velocity = (input.current.xy / input.current.w - input.previous.xy / input.previous.w) * 0.5;
    output.normal = encodeNormal(normalAt(data.terrain, input.local));
    return output;
}

// The shadows: one cascade of the sun, or the view of a local light in the atlas.

struct ShadowOutput
{
    float4 position : SV_Position;
    float2 local : TEXCOORD0;
};

[shader("vertex")]
ShadowOutput shadowVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const TerrainVertex vertex = terrainVertex(vertexIndex, instanceIndex);
    ShadowOutput output;
    output.position = mul(data.scene->cascadeViewProjections[data.cascade], float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

[shader("vertex")]
ShadowOutput localShadowVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const TerrainVertex vertex = terrainVertex(vertexIndex, instanceIndex);
    ShadowOutput output;
    output.position = mul(data.scene->shadowViews[data.cascade].viewProjection, float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
void shadowFragment(ShadowOutput input)
{
    if (isHole(data.terrain, input.local))
    {
        discard;
    }
}

// Picking, for the editor.

[shader("vertex")]
ShadowOutput pickVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const TerrainVertex vertex = terrainVertex(vertexIndex, instanceIndex);
    ShadowOutput output;
    output.position = mul(data.scene->pickViewProjection, float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
uint pickFragment(ShadowOutput input) : SV_Target
{
    if (isHole(data.terrain, input.local))
    {
        discard;
    }
    return data.terrain->objectId;
}

// The ground, shaded.

struct SceneOutput
{
    float4 position : SV_Position;
    float3 world : POSITION;
    float2 local : TEXCOORD0;
};

[shader("vertex")]
SceneOutput sceneVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const TerrainVertex vertex = terrainVertex(vertexIndex, instanceIndex);
    SceneOutput output;
    output.position = mul(data.scene->viewProjection, float4(vertex.world, 1.0));
    output.world = vertex.world;
    output.local = vertex.local;
    return output;
}

uint layerMaterial(Terrain* terrain, uint layer)
{
    return terrain->layers[layer / 4][layer % 4];
}

// What a layer looks like at a point: its colour, its normal in the world, its roughness and metallic.
struct LayerSample
{
    float3 albedo;
    float3 normal;
    float roughness;
    float metallic;
};

// A normal map read along one axis, as Golus's "UDN" blend of triplanar normals turns it into the
// world: its tilt added to the normal of the ground, in the plane of that axis.
float3 tiltOf(Material material, float2 uv)
{
    const float2 encoded = sampleTexture(material.normalTexture, uv).rg * 2.0 - 1.0;
    return float3(encoded * material.normalScale, 0.0);
}

LayerSample sampleLayer(SceneData* scene, Terrain* terrain, uint layer, float3 world, float3 ground, float3 axes)
{
    let material = scene->materials[layerMaterial(terrain, layer)];
    const float scale = 1.0 / terrain->textureSize;
    LayerSample result;
    float4 color = float4(0.0);
    float3 normal = ground;
    float4 surface = float4(0.0);
    // From above, then along X and Z where the ground is steep enough for them to show.
    const float2 top = world.xz * scale;
    color += sampleTexture(material.baseColorTexture, top) * axes.y;
    surface += sampleTexture(material.metallicRoughnessTexture, top) * axes.y;
    const float3 tiltTop = tiltOf(material, top);
    float3 bent = float3(tiltTop.x, 0.0, tiltTop.y) * axes.y;
    if (axes.x > 0.0)
    {
        const float2 side = world.zy * scale;
        color += sampleTexture(material.baseColorTexture, side) * axes.x;
        surface += sampleTexture(material.metallicRoughnessTexture, side) * axes.x;
        const float3 tilt = tiltOf(material, side);
        bent += float3(0.0, tilt.y, tilt.x) * axes.x;
    }
    if (axes.z > 0.0)
    {
        const float2 side = world.xy * scale;
        color += sampleTexture(material.baseColorTexture, side) * axes.z;
        surface += sampleTexture(material.metallicRoughnessTexture, side) * axes.z;
        const float3 tilt = tiltOf(material, side);
        bent += float3(tilt.x, tilt.y, 0.0) * axes.z;
    }
    result.albedo = material.baseColorFactor.rgb * color.rgb;
    result.normal = normalize(ground + bent);
    result.roughness = clamp(material.roughnessFactor * surface.g, 0.045, 1.0);
    result.metallic = saturate(material.metallicFactor * surface.b);
    return result;
}

// The share of each layer at a point: what was painted, and the rules on what was not.
void layerWeights(Terrain* terrain, float2 local, float3 normal, float height, out float weights[maxLayers])
{
    const float2 uv = mapUv(terrain, local);
    const float4 first = mapAt(terrain->paint0, uv);
    const float4 second = mapAt(terrain->paint1, uv);
    for (uint layer = 0; layer < 4; ++layer)
    {
        weights[layer] = first[layer];
        weights[layer + 4] = second[layer];
    }
    float painted = 0.0;
    for (uint layer = 0; layer < maxLayers; ++layer)
    {
        painted += weights[layer];
    }
    const float rules = saturate(1.0 - painted);
    if (rules <= 0.0)
    {
        return;
    }
    float automatic[maxLayers] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    const uint count = terrain->layerCount;
    if (terrain->autoBaseLayer >= 0 && uint(terrain->autoBaseLayer) < count)
    {
        automatic[terrain->autoBaseLayer] = 1.0;
    }
    if (terrain->autoSlopeLayer >= 0 && uint(terrain->autoSlopeLayer) < count)
    {
        const float angle = acos(saturate(normal.y));
        const float width = max(terrain->autoSlope * terrain->autoBlend, 0.001);
        const float steep = smoothstep(terrain->autoSlope - width, terrain->autoSlope + width, angle);
        for (uint layer = 0; layer < maxLayers; ++layer)
        {
            automatic[layer] *= 1.0 - steep;
        }
        automatic[terrain->autoSlopeLayer] += steep;
    }
    if (terrain->autoHeightLayer >= 0 && uint(terrain->autoHeightLayer) < count)
    {
        const float width = max(abs(terrain->autoHeight) * terrain->autoBlend, 0.5);
        const float high = smoothstep(terrain->autoHeight - width, terrain->autoHeight + width, height);
        for (uint layer = 0; layer < maxLayers; ++layer)
        {
            automatic[layer] *= 1.0 - high;
        }
        automatic[terrain->autoHeightLayer] += high;
    }
    for (uint layer = 0; layer < maxLayers; ++layer)
    {
        weights[layer] += automatic[layer] * rules;
    }
}

[shader("fragment")]
float4 sceneFragment(SceneOutput input) : SV_Target
{
    let scene = data.scene;
    let terrain = data.terrain;
    if (isHole(terrain, input.local))
    {
        discard;
    }
    const float3 ground = normalAt(terrain, input.local);

    // The axes the textures are seen along: from above alone on gentle slopes.
    float3 axes = float3(0.0, 1.0, 0.0);
    if (terrain->triplanar != 0)
    {
        axes = pow(abs(ground), 4.0);
        axes /= axes.x + axes.y + axes.z;
        // Too little of a side is left out, which spares its samples.
        axes.x = axes.x < 0.02 ? 0.0 : axes.x;
        axes.z = axes.z < 0.02 ? 0.0 : axes.z;
        axes /= axes.x + axes.y + axes.z;
    }

    float weights[maxLayers];
    layerWeights(terrain, input.local, ground, input.world.y - terrain->origin.y, weights);
    const uint count = min(terrain->layerCount, maxLayers);

    // The layers that show, each sampled once; where they meet, the brighter texels of each show
    // through first, as the tops of stones show through sand.
    LayerSample samples[maxLayers];
    float heights[maxLayers];
    float highest = 0.0;
    for (uint layer = 0; layer < count; ++layer)
    {
        heights[layer] = 0.0;
        if (weights[layer] > 0.002)
        {
            samples[layer] = sampleLayer(scene, terrain, layer, input.world, ground, axes);
            const float bright = dot(samples[layer].albedo, float3(0.2126, 0.7152, 0.0722));
            heights[layer] = weights[layer] + bright * terrain->blendSharpness;
            highest = max(highest, heights[layer]);
        }
    }
    float3 albedo = float3(0.0);
    float3 normal = float3(0.0);
    float roughness = 0.0;
    float metallic = 0.0;
    float total = 0.0;
    const float depth = lerp(1.0, 0.08, terrain->blendSharpness);
    for (uint layer = 0; layer < count; ++layer)
    {
        if (weights[layer] <= 0.002)
        {
            continue;
        }
        const float share = max(heights[layer] - (highest - depth), 0.0) * weights[layer];
        albedo += samples[layer].albedo * share;
        normal += samples[layer].normal * share;
        roughness += samples[layer].roughness * share;
        metallic += samples[layer].metallic * share;
        total += share;
    }
    if (total <= 0.0)
    {
        // Without layers: a neutral ground.
        albedo = float3(0.36, 0.34, 0.3);
        normal = ground;
        roughness = 0.9;
        total = 1.0;
    }
    albedo /= total;
    normal = normalize(normal);
    roughness /= total;
    metallic /= total;

    const float2 screenUv = input.position.xy / float2(scene->viewportWidth, scene->viewportHeight);
    Surface surface;
    surface.position = input.world;
    surface.normal = normal;
    surface.view = normalize(scene->cameraPosition - input.world);
    surface.diffuseColor = albedo * (1.0 - metallic);
    surface.f0 = lerp(float3(0.04), albedo, metallic);
    surface.roughness = roughness;
    surface.alpha = roughness * roughness;
    surface.occlusion = ambientOcclusionTexture.Sample(clampSampler, screenUv).r;
    surface.viewDistance = -mul(scene->view, float4(input.world, 1.0)).z;
    surface.fragmentCoordinate = input.position.xy;
    const float3 luminance = shadeSurface(scene, surface) * scene->exposure;
    return float4(applyAtmosphere(scene, luminance, surface.position), 1.0);
}

// Grass: blades scattered over the tiles around the camera, at places that only depend on the tile,
// so that they stay put as the camera moves; as many of them grow as the grass painted there says.

float hashOf(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return float(value & 0xffffffu) / 16777216.0;
}

struct GrassOutput
{
    float4 position : SV_Position;
    float3 world : POSITION;
    float3 normal : NORMAL;
    // From 0 at the foot of the blade to 1 at its tip.
    float along : TEXCOORD0;
};

[shader("vertex")]
GrassOutput grassVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    let scene = data.scene;
    let terrain = data.terrain;
    const uint tileIndex = instanceIndex / data.bladesPerTile;
    const uint blade = instanceIndex % data.bladesPerTile;
    let tile = data.tiles[tileIndex];
    // The seed of a blade is its tile in the world and its number in it.
    const int2 place = int2(floor(tile.corner / tile.size));
    const uint seed = ((uint(place.x) * 73856093u) ^ (uint(place.y) * 19349663u)) + blade * 2654435761u;
    const float2 local = tile.corner + float2(hashOf(seed), hashOf(seed + 1u)) * tile.size;

    GrassOutput output;
    output.position = float4(0.0, 0.0, 0.0, 1.0);
    output.world = float3(0.0);
    output.normal = float3(0.0, 1.0, 0.0);
    output.along = 0.0;
    if (any(local < 0.0) || any(local > terrain->size))
    {
        return output;
    }
    // Fewer blades where less grass was painted, and towards the end of their distance.
    const float2 uv = mapUv(terrain, local);
    const float4 detail = mapAt(terrain->detail, uv);
    const float3 foot = terrain->origin + float3(local.x, heightAt(terrain, local), local.y);
    const float distance = length(foot - scene->cameraPosition);
    const float fade = 1.0 - smoothstep(terrain->grassDistance * 0.7, terrain->grassDistance, distance);
    if (detail.r > 0.5 || hashOf(seed + 2u) >= detail.g * fade)
    {
        return output;
    }

    // Seven vertices: two at each of three heights, then the tip.
    const uint level = min(vertexIndex / 2, 3u);
    const float side = vertexIndex >= 6 ? 0.0 : (vertexIndex % 2 == 0 ? -1.0 : 1.0);
    const float along = float(level) / 3.0;
    const float angle = hashOf(seed + 3u) * 6.2831853;
    const float3 across = float3(cos(angle), 0.0, sin(angle));
    const float3 facing = float3(-across.z, 0.0, across.x);
    const float height = terrain->grassHeight * (0.65 + 0.7 * hashOf(seed + 4u));
    const float width = terrain->grassWidth * data.bladeWidthScale * (1.0 - along * 0.85);
    // Bent forward a little, and swaying in the wind, the tip most.
    const float phase = dot(foot.xz, float2(0.13, 0.17)) + hashOf(seed + 5u) * 1.5;
    const float sway = sin(scene->time * terrain->windSpeed + phase) * 0.6 + sin(scene->time * terrain->windSpeed * 2.3 + phase * 1.7) * 0.25;
    const float3 wind = normalize(float3(1.0, 0.0, 0.35)) * terrain->windStrength * sway;
    const float bend = along * along;
    const float3 world = foot + across * side * width + float3(0.0, height * along, 0.0) + (facing * 0.25 * height + wind * height) * bend;

    output.world = world;
    output.position = mul(scene->viewProjection, float4(world, 1.0));
    output.normal = normalize(lerp(normalAt(terrain, local), facing, 0.4));
    output.along = along;
    return output;
}

[shader("fragment")]
float4 grassFragment(GrassOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    let scene = data.scene;
    let terrain = data.terrain;
    const float3 color = lerp(terrain->grassColor, terrain->grassTipColor, input.along);
    float3 normal = normalize(input.normal);
    // Seen from behind, the blade faces the other way.
    if (!frontFacing)
    {
        normal.xz = -normal.xz;
    }
    const float2 screenUv = input.position.xy / float2(scene->viewportWidth, scene->viewportHeight);
    Surface surface;
    surface.position = input.world;
    surface.normal = normal;
    surface.view = normalize(scene->cameraPosition - input.world);
    surface.diffuseColor = color;
    surface.f0 = float3(0.04);
    surface.roughness = 0.75;
    surface.alpha = surface.roughness * surface.roughness;
    // Darker at the foot, where the blades shade each other.
    surface.occlusion = lerp(0.35, 1.0, input.along) * ambientOcclusionTexture.Sample(clampSampler, screenUv).r;
    surface.viewDistance = -mul(scene->view, float4(input.world, 1.0)).z;
    surface.fragmentCoordinate = input.position.xy;
    const float3 luminance = shadeSurface(scene, surface) * scene->exposure;
    return float4(applyAtmosphere(scene, luminance, surface.position), 1.0);
}
