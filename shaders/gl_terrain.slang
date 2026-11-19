// Terrains for OpenGL, translated into GLSL ES 3.00 and GLSL 3.30, as webgpu_terrain.slang does for WebGPU: the blocks,
// the ground and the grass of terrain_shading.slang, lit as gl_mesh.slang lights the meshes, with their shadows, and
// veiled by the fog. A terrain binds its maps; the textures of its layers are drawn into three arrays, one layer of each
// per layer of the terrain. OpenGL ES 3.0 has no storage buffers: the blocks are instances whose corner, size and level
// arrive as attributes, and the tiles of grass too, each the instance of all its blades.

import gl_scene;
import surface_shading;
import terrain_shading;

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
    // The local lights that shine on it, a byte each, as an object has them.
    uint2 lights;
    uint padding0;
    uint padding1;
};

// What a draw reads: the camera, which the blocks slide their vertices by, also when they cast shadows; and for grass,
// the blades of a tile and how much wider they are, and the time the wind blows by.
struct TerrainDrawData
{
    float3 cameraPosition;
    uint bladesPerTile;
    float bladeWidthScale;
    float time;
    float padding0;
    float padding1;
};

struct ViewData
{
    float4x4 viewProjection;
};

ConstantBuffer<TerrainData> terrain;
ConstantBuffer<TerrainDrawData> terrainDraw;
ConstantBuffer<ViewData> shadowView;
Sampler2D heightMap;
Sampler2D slopeMap;
Sampler2D paintMap0;
Sampler2D paintMap1;
Sampler2D detailMap;
Sampler2DArray layerColors;
Sampler2DArray layerSurfaces;
Sampler2DArray layerNormals;

// A block, or a tile of grass, read as attributes: its corner and its size, then its level of detail.
struct NodeInput
{
    [[vk::location(0)]] float3 cornerAndSize : POSITION;
    [[vk::location(1)]] uint level : BLENDINDICES;
};

TerrainNode nodeOf(NodeInput input)
{
    TerrainNode node;
    node.corner = input.cornerAndSize.xy;
    node.size = input.cornerAndSize.z;
    node.level = input.level;
    return node;
}

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
            return slopeMap.SampleLevel(uv, 0.0);
        case terrainPaint0:
            return paintMap0.SampleLevel(uv, 0.0);
        case terrainPaint1:
            return paintMap1.SampleLevel(uv, 0.0);
        default:
            return detailMap.SampleLevel(uv, 0.0);
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
            return layerColors.SampleGrad(at, slopeX, slopeY);
        case layerMetallicRoughness:
            return layerSurfaces.SampleGrad(at, slopeX, slopeY);
        default:
            return layerNormals.SampleGrad(at, slopeX, slopeY);
        }
    }
};

TerrainVertex vertexOf(NodeInput input, uint vertexIndex)
{
    BoundMaps maps;
    return terrainVertex(maps, shape(), nodeOf(input), terrain.gridCells, vertexIndex, terrainDraw.cameraPosition);
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
ShadowOutput groundShadowVertex(NodeInput input, uint vertexIndex : SV_VulkanVertexID)
{
    const TerrainVertex vertex = vertexOf(input, vertexIndex);
    ShadowOutput output;
    output.position = mul(shadowView.viewProjection, float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
void groundShadowFragment(ShadowOutput input)
{
    if (holeAt(input.local))
    {
        discard;
    }
}

// The ground, shaded.

struct GroundOutput
{
    float4 position : SV_Position;
    float3 world : POSITION;
    float2 local : TEXCOORD0;
};

[shader("vertex")]
GroundOutput groundVertex(NodeInput input, uint vertexIndex : SV_VulkanVertexID)
{
    const TerrainVertex vertex = vertexOf(input, vertexIndex);
    GroundOutput output;
    output.position = mul(scene.viewProjection, float4(vertex.world, 1.0));
    output.world = vertex.world;
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
float4 groundFragment(GroundOutput input) : SV_Target
{
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
    const float3 luminance = surfaceLight(surface, terrain.lights);
    return float4(applyFog(luminance * scene.exposure, surface.position), 1.0);
}

// Grass: the blades of terrain_shading.slang over the tiles of the draw, each tile the instance of all its blades.

struct GrassOutput
{
    float4 position : SV_Position;
    float3 world : POSITION;
    float3 normal : NORMAL;
    // From 0 at the foot of the blade to 1 at its tip.
    float along : TEXCOORD0;
};

[shader("vertex")]
GrassOutput grassVertex(NodeInput input, uint vertexIndex : SV_VulkanVertexID, uint instanceIndex : SV_VulkanInstanceID)
{
    BoundMaps maps;
    const GrassBlade blade = bladeVertex(maps, shape(), nodeOf(input), instanceIndex % terrainDraw.bladesPerTile,
                                         terrainDraw.bladeWidthScale, vertexIndex, terrainDraw.cameraPosition, terrainDraw.time);
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
    const float3 luminance = surfaceLight(surface, terrain.lights);
    return float4(applyFog(luminance * scene.exposure, surface.position), 1.0);
}
