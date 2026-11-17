// Terrains for WebGPU, compiled to WGSL: the blocks, the ground and the grass of terrain_shading.slang, lit as
// webgpu_mesh.slang lights the meshes, with their shadows, and veiled by the fog. A terrain binds its maps; the textures
// of its layers are drawn into three arrays, one layer of each per layer of the terrain, since WebGPU has no bindless
// textures. Its blocks are its instances, from the first one its draw names.

import surface_shading;
import terrain_shading;
import webgpu_scene;

struct TerrainData
{
    float3 origin;
    float size;
    uint resolution;
    uint layerCount;
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
    float leafRange;
    // Cells on a side of a block.
    uint gridCells;
    float3 grassColor;
    float grassHeight;
    float3 grassTipColor;
    float grassWidth;
    float4 baseColorFactors[8];
    // The metallic, the roughness and the scale of the normal map of each layer.
    float4 layerFactors[8];
};

// What a draw reads: the camera, which the blocks slide their vertices by, also when they cast shadows; and for grass,
// its first tile, the blades of a tile and how much wider they are, and the time the wind blows by.
struct DrawData
{
    float3 cameraPosition;
    uint firstTile;
    uint bladesPerTile;
    float bladeWidthScale;
    float time;
    float padding;
};

struct TerrainShadowView
{
    float4x4 viewProjection;
};

[[vk::binding(0, 1)]]
ConstantBuffer<TerrainData> terrain;
[[vk::binding(1, 1)]]
Texture2D heightMap;
[[vk::binding(2, 1)]]
Texture2D slopeMap;
[[vk::binding(3, 1)]]
Texture2D paintMap0;
[[vk::binding(4, 1)]]
Texture2D paintMap1;
[[vk::binding(5, 1)]]
Texture2D detailMap;
[[vk::binding(6, 1)]]
Texture2DArray layerColors;
[[vk::binding(7, 1)]]
Texture2DArray layerSurfaces;
[[vk::binding(8, 1)]]
Texture2DArray layerNormals;
[[vk::binding(9, 1)]]
StructuredBuffer<TerrainNode> nodes;
[[vk::binding(10, 1)]]
StructuredBuffer<TerrainNode> tiles;
[[vk::binding(11, 1)]]
SamplerState mapSampler;
[[vk::binding(12, 1)]]
SamplerState layerSampler;

[[vk::binding(0, 2)]]
ConstantBuffer<DrawData> draw;
[[vk::binding(1, 2)]]
ConstantBuffer<TerrainShadowView> shadowView;

TerrainShape shape()
{
    TerrainShape result;
    result.origin = terrain.origin;
    result.size = terrain.size;
    result.resolution = terrain.resolution;
    result.layerCount = terrain.layerCount;
    result.textureSize = terrain.textureSize;
    result.blendSharpness = terrain.blendSharpness;
    result.triplanar = terrain.triplanar;
    result.autoBaseLayer = terrain.autoBaseLayer;
    result.autoSlopeLayer = terrain.autoSlopeLayer;
    result.autoSlope = terrain.autoSlope;
    result.autoHeightLayer = terrain.autoHeightLayer;
    result.autoHeight = terrain.autoHeight;
    result.autoBlend = terrain.autoBlend;
    result.windStrength = terrain.windStrength;
    result.windSpeed = terrain.windSpeed;
    result.grassDistance = terrain.grassDistance;
    result.grassColor = terrain.grassColor;
    result.grassHeight = terrain.grassHeight;
    result.grassTipColor = terrain.grassTipColor;
    result.grassWidth = terrain.grassWidth;
    result.leafRange = terrain.leafRange;
    return result;
}

struct BoundMaps : ITerrainMaps
{
    float heightAt(int2 sample)
    {
        return heightMap.Load(int3(sample, 0)).r;
    }

    float4 map(uint kind, float2 uv)
    {
        switch (kind)
        {
        case terrainSlopes:
            return slopeMap.SampleLevel(mapSampler, uv, 0.0);
        case terrainPaint0:
            return paintMap0.SampleLevel(mapSampler, uv, 0.0);
        case terrainPaint1:
            return paintMap1.SampleLevel(mapSampler, uv, 0.0);
        default:
            return detailMap.SampleLevel(mapSampler, uv, 0.0);
        }
    }
};

struct ArrayLayers : ITerrainLayers
{
    float4 baseColorFactor(uint layer)
    {
        return terrain.baseColorFactors[layer];
    }

    float3 factors(uint layer)
    {
        return terrain.layerFactors[layer].xyz;
    }

    float4 read(uint layer, uint kind, float2 uv, float2 slopeX, float2 slopeY)
    {
        const float3 at = float3(uv, float(layer));
        switch (kind)
        {
        case layerBaseColor:
            return layerColors.SampleGrad(layerSampler, at, slopeX, slopeY);
        case layerMetallicRoughness:
            return layerSurfaces.SampleGrad(layerSampler, at, slopeX, slopeY);
        default:
            return layerNormals.SampleGrad(layerSampler, at, slopeX, slopeY);
        }
    }
};

TerrainVertex vertexOf(uint vertexIndex, uint instanceIndex)
{
    BoundMaps maps;
    return terrainVertex(maps, shape(), nodes[instanceIndex], terrain.gridCells, vertexIndex, draw.cameraPosition);
}

bool holeAt(float2 local)
{
    BoundMaps maps;
    return isHole(maps, shape(), local);
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
    const TerrainVertex vertex = vertexOf(vertexIndex, instanceIndex);
    ShadowOutput output;
    output.position = mul(shadowView.viewProjection, float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
void shadowFragment(ShadowOutput input)
{
    if (holeAt(input.local))
    {
        discard;
    }
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
    const TerrainVertex vertex = vertexOf(vertexIndex, instanceIndex);
    SceneOutput output;
    output.position = mul(scene.viewProjection, float4(vertex.world, 1.0));
    output.world = vertex.world;
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
float4 sceneFragment(SceneOutput input) : SV_Target
{
    // The slopes first, where every pixel of a quad goes the same way, as WGSL wants it.
    const float3 worldDx = ddx(input.world);
    const float3 worldDy = ddy(input.world);
    if (holeAt(input.local))
    {
        discard;
    }
    BoundMaps maps;
    ArrayLayers layers;
    const LayerSample ground = terrainGround(maps, layers, shape(), input.world, worldDx, worldDy, input.local);
    Surface surface;
    surface.position = input.world;
    surface.normal = ground.normal;
    surface.view = normalize(scene.cameraPosition - input.world);
    surface.diffuseColor = ground.albedo * (1.0 - ground.metallic);
    surface.f0 = lerp(float3(0.04), ground.albedo, ground.metallic);
    surface.roughness = ground.roughness;
    surface.alpha = ground.roughness * ground.roughness;
    surface.occlusion = 1.0;
    surface.viewDistance = -mul(scene.view, float4(input.world, 1.0)).z;
    surface.fragmentCoordinate = input.position.xy;
    const float3 luminance = surfaceLight(surface);
    return float4(applyFog(luminance * scene.exposure, surface.position), 1.0);
}

// Grass: the blades of terrain_shading.slang over the tiles of the draw.

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
    BoundMaps maps;
    const GrassBlade blade = bladeVertex(maps, shape(), tiles[draw.firstTile + instanceIndex / draw.bladesPerTile],
                                         instanceIndex % draw.bladesPerTile, draw.bladeWidthScale, vertexIndex, draw.cameraPosition,
                                         draw.time);
    GrassOutput output;
    output.position = blade.grows ? mul(scene.viewProjection, float4(blade.world, 1.0)) : float4(0.0, 0.0, 0.0, 1.0);
    output.world = blade.world;
    output.normal = blade.normal;
    output.along = blade.along;
    return output;
}

[shader("fragment")]
float4 grassFragment(GrassOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    const float3 color = lerp(terrain.grassColor, terrain.grassTipColor, input.along);
    float3 normal = normalize(input.normal);
    // Seen from behind, the blade faces the other way.
    if (!frontFacing)
    {
        normal.xz = -normal.xz;
    }
    Surface surface;
    surface.position = input.world;
    surface.normal = normal;
    surface.view = normalize(scene.cameraPosition - input.world);
    surface.diffuseColor = color;
    surface.f0 = float3(0.04);
    surface.roughness = 0.75;
    surface.alpha = surface.roughness * surface.roughness;
    // Darker at the foot, where the blades shade each other.
    surface.occlusion = lerp(0.35, 1.0, input.along);
    surface.viewDistance = -mul(scene.view, float4(input.world, 1.0)).z;
    surface.fragmentCoordinate = input.position.xy;
    const float3 luminance = surfaceLight(surface);
    return float4(applyFog(luminance * scene.exposure, surface.position), 1.0);
}
