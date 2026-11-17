// Terrains for Vulkan: the blocks, the ground and the grass of terrain_shading.slang, reading the maps and the
// materials of the layers among the textures of the scene, in every pass: the prepass, the shadows, the picking and
// the shading.

import common;
import pbr;
import terrain_shading;

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


struct TerrainData
{
    SceneData* scene;
    Terrain* terrain;
    TerrainNode* nodes;
    // The squares where grass grows; their level is how many blades each one gets.
    TerrainNode* tiles;
    uint cascade;
    // Cells on a side of a block.
    uint gridCells;
    // Grass: blades per tile, and how much wider they are to cover for the ones left out far away.
    uint bladesPerTile;
    float bladeWidthScale;
};

[[vk::push_constant]]
ConstantBuffer<TerrainData> data;

TerrainShape shapeOf(Terrain* terrain)
{
    TerrainShape shape;
    shape.origin = terrain->origin;
    shape.size = terrain->size;
    shape.resolution = terrain->resolution;
    shape.layerCount = terrain->layerCount;
    shape.textureSize = terrain->textureSize;
    shape.blendSharpness = terrain->blendSharpness;
    shape.triplanar = terrain->triplanar;
    shape.autoBaseLayer = terrain->autoBaseLayer;
    shape.autoSlopeLayer = terrain->autoSlopeLayer;
    shape.autoSlope = terrain->autoSlope;
    shape.autoHeightLayer = terrain->autoHeightLayer;
    shape.autoHeight = terrain->autoHeight;
    shape.autoBlend = terrain->autoBlend;
    shape.windStrength = terrain->windStrength;
    shape.windSpeed = terrain->windSpeed;
    shape.grassDistance = terrain->grassDistance;
    shape.grassColor = terrain->grassColor;
    shape.grassHeight = terrain->grassHeight;
    shape.grassTipColor = terrain->grassTipColor;
    shape.grassWidth = terrain->grassWidth;
    shape.leafRange = terrain->leafRange;
    return shape;
}

// The maps of a terrain among the textures of the scene.
struct BindlessMaps : ITerrainMaps
{
    uint heights;
    uint4 slots;

    float heightAt(int2 sample)
    {
        return textures[NonUniformResourceIndex(heights)].Load(int3(sample, 0)).r;
    }

    float4 map(uint kind, float2 uv)
    {
        return textures[NonUniformResourceIndex(slots[kind])].SampleLevel(clampSampler, uv, 0.0);
    }
};

BindlessMaps mapsOf(Terrain* terrain)
{
    BindlessMaps maps;
    maps.heights = terrain->heights;
    maps.slots = uint4(terrain->slopes, terrain->paint0, terrain->paint1, terrain->detail);
    return maps;
}

// The layers of a terrain: materials of the scene.
struct BindlessLayers : ITerrainLayers
{
    SceneData* scene;
    uint4 materials[2];

    Material materialOf(uint layer)
    {
        return scene->materials[materials[layer / 4][layer % 4]];
    }

    float4 baseColorFactor(uint layer)
    {
        return materialOf(layer).baseColorFactor;
    }

    float3 factors(uint layer)
    {
        const Material material = materialOf(layer);
        return float3(material.metallicFactor, material.roughnessFactor, material.normalScale);
    }

    // With the slopes the hardware takes itself.
    float4 read(uint layer, uint kind, float2 uv, float2 slopeX, float2 slopeY)
    {
        const Material material = materialOf(layer);
        const uint texture = kind == layerBaseColor ? material.baseColorTexture
                           : kind == layerMetallicRoughness ? material.metallicRoughnessTexture
                                                            : material.normalTexture;
        return sampleTexture(texture, uv);
    }
};

TerrainVertex vertexOf(uint vertexIndex, uint instanceIndex)
{
    return terrainVertex(mapsOf(data.terrain), shapeOf(data.terrain), data.nodes[instanceIndex], data.gridCells, vertexIndex,
                         data.scene->cameraPosition);
}

bool holeAt(float2 local)
{
    return isHole(mapsOf(data.terrain), shapeOf(data.terrain), local);
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
    const TerrainVertex vertex = vertexOf(vertexIndex, instanceIndex);
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
    if (holeAt(input.local))
    {
        discard;
    }
    PrepassTargets output;
    output.velocity = (input.current.xy / input.current.w - input.previous.xy / input.previous.w) * 0.5;
    output.normal = encodeNormal(terrainNormal(mapsOf(data.terrain), shapeOf(data.terrain), input.local));
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
    const TerrainVertex vertex = vertexOf(vertexIndex, instanceIndex);
    ShadowOutput output;
    output.position = mul(data.scene->cascadeViewProjections[data.cascade], float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

[shader("vertex")]
ShadowOutput localShadowVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const TerrainVertex vertex = vertexOf(vertexIndex, instanceIndex);
    ShadowOutput output;
    output.position = mul(data.scene->shadowViews[data.cascade].viewProjection, float4(vertex.world, 1.0));
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

// Picking, for the editor.

[shader("vertex")]
ShadowOutput pickVertex(uint vertexIndex : SV_VertexID, uint instanceIndex : SV_InstanceID)
{
    const TerrainVertex vertex = vertexOf(vertexIndex, instanceIndex);
    ShadowOutput output;
    output.position = mul(data.scene->pickViewProjection, float4(vertex.world, 1.0));
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
uint pickFragment(ShadowOutput input) : SV_Target
{
    if (holeAt(input.local))
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
    const TerrainVertex vertex = vertexOf(vertexIndex, instanceIndex);
    SceneOutput output;
    output.position = mul(data.scene->viewProjection, float4(vertex.world, 1.0));
    output.world = vertex.world;
    output.local = vertex.local;
    return output;
}

[shader("fragment")]
float4 sceneFragment(SceneOutput input) : SV_Target
{
    let scene = data.scene;
    let terrain = data.terrain;
    if (holeAt(input.local))
    {
        discard;
    }
    BindlessLayers layers;
    layers.scene = scene;
    layers.materials = terrain->layers;
    const LayerSample ground = terrainGround(mapsOf(terrain), layers, shapeOf(terrain), input.world, ddx(input.world), ddy(input.world),
                                             input.local);

    const float2 screenUv = input.position.xy / float2(scene->viewportWidth, scene->viewportHeight);
    Surface surface;
    surface.position = input.world;
    surface.normal = ground.normal;
    surface.view = normalize(scene->cameraPosition - input.world);
    surface.diffuseColor = ground.albedo * (1.0 - ground.metallic);
    surface.f0 = lerp(float3(0.04), ground.albedo, ground.metallic);
    surface.roughness = ground.roughness;
    surface.alpha = ground.roughness * ground.roughness;
    surface.occlusion = ambientOcclusionTexture.Sample(clampSampler, screenUv).r;
    surface.viewDistance = -mul(scene->view, float4(input.world, 1.0)).z;
    surface.fragmentCoordinate = input.position.xy;
    const float3 luminance = shadeSurface(scene, surface) * scene->exposure;
    return float4(applyAtmosphere(scene, luminance, surface.position), 1.0);
}

// Grass: the blades of terrain_shading.slang over the tiles of the frame.

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
    const GrassBlade blade = bladeVertex(mapsOf(data.terrain), shapeOf(data.terrain), data.tiles[instanceIndex / data.bladesPerTile],
                                         instanceIndex % data.bladesPerTile, data.bladeWidthScale, vertexIndex, scene->cameraPosition,
                                         scene->time);
    GrassOutput output;
    output.position = blade.grows ? mul(scene->viewProjection, float4(blade.world, 1.0)) : float4(0.0, 0.0, 0.0, 1.0);
    output.world = blade.world;
    output.normal = blade.normal;
    output.along = blade.along;
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
    const float3 luminance = shadeSurfaceOffScreen(scene, surface) * scene->exposure;
    return float4(applyAtmosphere(scene, luminance, surface.position), 1.0);
}
