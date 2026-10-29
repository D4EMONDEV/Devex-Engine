// Data shared by the frame's shaders. Layouts must match engine/src/render/vulkan/GpuData.hpp,
// and bindings engine/src/render/vulkan/DescriptorSets.hpp.
module common;

public static const float pi = 3.14159265358979;
public static const uint cascadeCount = 4;
public static const uint alphaModeMask = 1;
public static const uint alphaModeBlend = 2;
public static const uint sunEnabled = 1;
public static const uint sunCastsShadows = 2;
public static const uint atmosphereAir = 1;
public static const uint atmosphereFog = 2;

public struct Vertex
{
    public float3 position;
    public float u;
    public float3 normal;
    public float v;
    public float4 tangent;
};

// How a vertex of a skinned mesh follows its bones.
public struct VertexSkin
{
    public uint4 joints;
    public float4 weights;
};

public struct Material
{
    public float4 baseColorFactor;
    public float3 emissiveFactor;
    public float alphaCutoff;
    public uint baseColorTexture;
    public uint metallicRoughnessTexture;
    public uint normalTexture;
    public uint occlusionTexture;
    public uint emissiveTexture;
    public float metallicFactor;
    public float roughnessFactor;
    public float normalScale;
    public float occlusionStrength;
    public uint alphaMode;
    public uint doubleSided;
    // Where the uniforms of its shader start in SceneData::materialParameters, one slot of four
    // numbers each.
    public uint parameterOffset;
};

public struct Light
{
    public float3 position;
    public float range;
    // Direction in which a spot light shines.
    public float3 direction;
    // Spot attenuation: saturate(cos * scale + offset), with scale 0 and offset 1 for points.
    public float spotScale;
    // Luminous intensity in candelas.
    public float3 intensity;
    public float spotOffset;
    // The first view of the shadow atlas this light owns, or -1 when it casts none. A point light
    // owns the six that follow, one per face of the cube around it.
    public int firstShadowView;
    public float shadowFar;
    public float padding0;
    public float padding1;
};

// One view of the shadow atlas: where it looks from, and the part of the atlas it was given.
public struct ShadowView
{
    public float4x4 viewProjection;
    // Offset and size in the atlas, in texture coordinates.
    public float4 tile;
};

public struct Cluster
{
    public uint offset;
    public uint count;
};

// An irradiance volume of the frame, in the space of its box.
public struct IrradianceVolume
{
    // From the world into the box, in meters along its axes, from its center.
    public float4x4 worldToBox;
    public float3 halfSize;
    public float blendDistance;
    public uint countX;
    public uint countY;
    public uint countZ;
    public float intensity;
    // Seven groups of four numbers a probe: nine harmonics of red, green and blue, then one unused.
    public float4* probes;
    // How far a surface reads the volume off itself along its normal, in cells of the grid.
    public float normalBias;
    public float padding;
};

// The air of the frame, in kilometers and per kilometer, with the sun above it and the camera in it,
// and the fog, in meters.
public struct Atmosphere
{
    public float bottomRadius;
    public float topRadius;
    public float rayleighHeight;
    public float mieHeight;
    public float3 rayleighScattering;
    public float mieScattering;
    public float3 ozoneAbsorption;
    public float mieAbsorption;
    public float3 groundAlbedo;
    public float mieAnisotropy;
    // Above the air, in lux; and the cosine of the radius of its disk.
    public float3 sunIlluminance;
    public float sunCosRadius;
    // Towards the sun, and how far the camera stands from the center of the planet.
    public float3 sunDirection;
    public float cameraRadius;
    public float sunDiskIntensity;
    public float aerialScale;
    public float aerialDistance;
    public float padding0;
    public float3 fogColor;
    public float fogDensity;
    public float fogHeight;
    public float fogFalloff;
    public float fogStart;
    public float fogMaxOpacity;
    public float fogSunScattering;
    public float fogSunAnisotropy;
    public float fogSkyBlend;
    public float padding1;
};

// A reflection probe of the frame, in the space of its box.
public struct ReflectionProbe
{
    // From the world into the box, in meters along its axes, from its center.
    public float4x4 worldToBox;
    public float3 halfSize;
    public float blendDistance;
    // Where it was captured from, in the box.
    public float3 capturePosition;
    // Turns the values of its map into nits.
    public float intensity;
    // Its slot among reflectionProbeMaps.
    public uint map;
    public uint boxProjection;
    public float padding0;
    public float padding1;
};

public struct SceneData
{
    public float4x4 viewProjection;
    public float4x4 view;
    // Clip space to world directions, without the camera position.
    public float4x4 skyInverseViewProjection;
    public float3 cameraPosition;
    // Multiplies every luminance, so that stored values stay within half-float range.
    public float exposure;
    // Direction in which sunlight travels.
    public float3 sunDirection;
    public uint sunFlags;
    // In lux.
    public float3 sunIlluminance;
    // In nits for a sky value of 1.
    public float environmentIntensity;
    public float3 environmentColor;
    public float environmentRotation;
    public float4 cascadeSplits;
    public float4 cascadeTexelSizes;
    public float4x4 cascadeViewProjections[4];
    public float shadowDistance;
    public float specularMipCount;
    public float viewportWidth;
    public float viewportHeight;
    public uint clusterCountX;
    public uint clusterCountY;
    public uint clusterCountZ;
    public uint lightCount;
    public float clusterSliceScale;
    public float clusterSliceBias;
    // Seconds since the renderer started, as the TIME of shaders, and on the previous frame.
    public float time;
    public float previousTime;
    public Material* materials;
    public Light* lights;
    public Cluster* clusters;
    public uint* clusterLights;
    // Maps the pixel requested for picking to the whole 1x1 pick target.
    public float4x4 pickViewProjection;
    // The same view and projection without the jitter of the temporal antialiasing, and the one of
    // the previous frame: what lies between them is the motion of a pixel.
    public float4x4 unjitteredViewProjection;
    public float4x4 previousViewProjection;
    // Takes a point of clip space back to the world, without the jitter.
    public float4x4 inverseViewProjection;
    // The views the local lights were given in the shadow atlas.
    public ShadowView* shadowViews;
    // The uniforms of the materials drawn with shaders of the project.
    public float4* materialParameters;
    // The values objects give the instance uniforms of their shaders, those of each object together.
    public float4* instanceParameters;
    // The reflection probes of the frame, the smallest first.
    public ReflectionProbe* reflectionProbes;
    public uint reflectionProbeCount;
    // The irradiance volumes of the frame, the smallest first.
    public uint irradianceVolumeCount;
    public IrradianceVolume* irradianceVolumes;
    // The air and the fog of the frame, and which of them there are.
    public Atmosphere* atmosphere;
    public uint atmosphereFlags;
    public uint atmospherePadding;
};

// A normal folded into two numbers on an octahedron, which keeps its precision where it matters.
public float2 encodeNormal(float3 normal)
{
    const float3 folded = normal / (abs(normal.x) + abs(normal.y) + abs(normal.z));
    if (folded.z >= 0.0)
    {
        return folded.xy;
    }
    const float2 signs = float2(folded.x >= 0.0 ? 1.0 : -1.0, folded.y >= 0.0 ? 1.0 : -1.0);
    return (1.0 - abs(folded.yx)) * signs;
}

public float3 decodeNormal(float2 folded)
{
    float3 normal = float3(folded.x, folded.y, 1.0 - abs(folded.x) - abs(folded.y));
    if (normal.z < 0.0)
    {
        const float2 signs = float2(normal.x >= 0.0 ? 1.0 : -1.0, normal.y >= 0.0 ? 1.0 : -1.0);
        normal.xy = (1.0 - abs(normal.yx)) * signs;
    }
    return normalize(normal);
}

public struct DrawData
{
    public SceneData* scene;
    public Vertex* vertices;
    public float4x4 world;
    public uint material;
    public uint cascade;
    public uint objectId;
    // Zero for a static mesh; otherwise the vertices follow the bones below, which already hold
    // the transform from the space of the mesh to the world.
    public uint skinned;
    public VertexSkin* skin;
    public float4x4* bones;
    // Where the values the instance gives its instance uniforms start in
    // SceneData::instanceParameters, or noInstanceParameters when it gives none.
    public uint instanceParameters;
    public uint padding0;
    public uint padding1;
    public uint padding2;
};

// Normals transform with the cofactor matrix, which stays correct under non-uniform scales. Its
// sign follows the determinant so that mirrored transforms keep outward normals.
public float3 transformNormal(float4x4 world, float3 normal)
{
    const float3x3 columns = transpose(float3x3(world));
    const float3 transformed = cross(columns[1], columns[2]) * normal.x + cross(columns[2], columns[0]) * normal.y +
                               cross(columns[0], columns[1]) * normal.z;
    return dot(columns[0], cross(columns[1], columns[2])) < 0.0 ? -transformed : transformed;
}

// -1 for a transform that mirrors, which turns the bitangents of tangents around.
public float handedness(float4x4 world)
{
    const float3x3 columns = transpose(float3x3(world));
    return sign(dot(columns[0], cross(columns[1], columns[2])) + 1e-12);
}

// The transform a vertex follows: the world matrix of the instance, or the blend of the bones of a
// skinned mesh, which already reach the world.
public float4x4 vertexTransform(DrawData draw, uint vertexIndex)
{
    if (draw.skinned == 0)
    {
        return draw.world;
    }
    const VertexSkin skin = draw.skin[vertexIndex];
    float4x4 blended = draw.bones[skin.joints.x] * skin.weights.x;
    blended += draw.bones[skin.joints.y] * skin.weights.y;
    blended += draw.bones[skin.joints.z] * skin.weights.z;
    blended += draw.bones[skin.joints.w] * skin.weights.w;
    // Vertices left without weights stay where the instance is.
    const float total = skin.weights.x + skin.weights.y + skin.weights.z + skin.weights.w;
    return total > 0.0001 ? blended : draw.world;
}

// Set 0: textures and image based lighting, shared by every frame.
[[vk::binding(0, 0)]]
public Texture2D textures[];
[[vk::binding(1, 0)]]
public SamplerState materialSampler;
[[vk::binding(2, 0)]]
public TextureCube specularEnvironment;
[[vk::binding(3, 0)]]
public TextureCube irradianceEnvironment;
[[vk::binding(4, 0)]]
public Texture2D brdfLut;
[[vk::binding(5, 0)]]
public SamplerState clampSampler;
[[vk::binding(6, 0)]]
public Texture2D skyTexture;
[[vk::binding(7, 0)]]
public SamplerState skySampler;
// The sampler of each texture, beside it: smooth, or the nearest pixel for pixel art.
[[vk::binding(8, 0)]]
public SamplerState textureSamplers[];
// The reflections of the reflection probes, prefiltered for each roughness as the sky's are.
[[vk::binding(9, 0)]]
public TextureCube reflectionProbeMaps[];
// The tables of the atmosphere: what the air lets through from a height towards a direction, the
// light of the sky around the camera, and the haze in front of it, slice by slice of distance. Kept
// in the general layout, as their passes write them.
[[vk::binding(10, 0)]]
public Texture2D atmosphereTransmittance;
[[vk::binding(11, 0)]]
public Texture2D skyViewTable;
[[vk::binding(12, 0)]]
public Texture2DArray aerialPerspective;

// Set 1: images produced earlier in the frame.
[[vk::binding(0, 1)]]
public Texture2DArray shadowMap;
[[vk::binding(1, 1)]]
public SamplerComparisonState shadowSampler;
// The shadows of the local lights, side by side in one image.
[[vk::binding(11, 1)]]
public Texture2D localShadowMap;
[[vk::binding(2, 1)]]
public Texture2D sceneColor;
// Coverage of the selected objects, outlined by the overlay.
[[vk::binding(3, 1)]]
public Texture2D selectionMask;
// Where every pixel was on the previous frame, in texture coordinates.
[[vk::binding(4, 1)]]
public Texture2D velocityTexture;
// The normals of the prepass, folded into two numbers.
[[vk::binding(5, 1)]]
public Texture2D normalTexture;
// How much of the sky each pixel sees, from 0 in a corner to 1 in the open.
[[vk::binding(6, 1)]]
public Texture2D ambientOcclusionTexture;
// What the previous frame resolved, which the temporal antialiasing carries forward.
[[vk::binding(7, 1)]]
public Texture2D historyTexture;
// The depth of the scene, as the prepass wrote it: 1 at the near plane, 0 infinitely far away.
[[vk::binding(8, 1)]]
public Texture2D depthTexture;
// The same depth while the blended surfaces and the particles are drawn over the scene, which
// test against it without writing it.
[[vk::binding(12, 1)]]
public Texture2D sceneDepth;
// The image the tonemapping reads: what the antialiasing resolved, or the scene as it was drawn.
[[vk::binding(9, 1)]]
public Texture2D resolvedTexture;
// The halved images the bloom is built from, the largest first.
[[vk::binding(10, 1)]]
public Texture2D bloomChain[5];
// What the depth of field and the motion blur read: the resolved image, and the images they pass
// along between them (see shaders/lens.slang), kept in the general layout.
[[vk::binding(13, 1)]]
public Texture2D postSource;
[[vk::binding(14, 1)]]
public Texture2D postChain[7];

public float4 sampleTexture(uint index, float2 uv)
{
    return textures[NonUniformResourceIndex(index)].Sample(textureSamplers[NonUniformResourceIndex(index)], uv);
}

// The same at a mip level, where the slopes of the coordinates are unknown, as in vertex shaders.
public float4 sampleTextureLevel(uint index, float2 uv, float level)
{
    return textures[NonUniformResourceIndex(index)].SampleLevel(textureSamplers[NonUniformResourceIndex(index)], uv,
                                                                level);
}

// The same with the slopes of the coordinates given, for mappings that jump.
public float4 sampleTextureGrad(uint index, float2 uv, float2 slopeX, float2 slopeY)
{
    return textures[NonUniformResourceIndex(index)].SampleGrad(textureSamplers[NonUniformResourceIndex(index)], uv,
                                                               slopeX, slopeY);
}

// Rotates a direction around the vertical axis, to turn the environment.
public float3 rotateAroundY(float3 direction, float angle)
{
    const float c = cos(angle);
    const float s = sin(angle);
    return float3(c * direction.x + s * direction.z, direction.y, -s * direction.x + c * direction.z);
}

// Equirectangular coordinates of a direction: the image center looks along -Z, the top up.
public float2 equirectangularUv(float3 direction)
{
    const float u = 0.5 + atan2(direction.x, -direction.z) / (2.0 * pi);
    const float v = acos(clamp(direction.y, -1.0, 1.0)) / pi;
    return float2(u, v);
}

// A triangle covering the screen, from vertex indices 0 to 2.
public float2 fullscreenUv(uint vertexIndex)
{
    return float2((vertexIndex << 1) & 2, vertexIndex & 2);
}

// The atmosphere, after Hillaire, "A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique" (2020), as Unreal's SkyAtmosphere: lengths in kilometers, the planet centered below the
// camera, Y up.
public static const float2 transmittanceTableSize = float2(256.0, 64.0);
public static const float2 multiScatteringTableSize = float2(32.0, 32.0);
public static const float2 skyViewTableSize = float2(192.0, 108.0);
public static const float aerialSlices = 32.0;

// The first distance along a ray to a sphere around the origin; negative when it misses it.
public float raySphereNearest(float3 origin, float3 direction, float radius)
{
    const float b = dot(origin, direction);
    const float c = dot(origin, origin) - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0)
    {
        return -1.0;
    }
    const float root = sqrt(discriminant);
    const float near = -b - root;
    const float far = -b + root;
    return near >= 0.0 ? near : (far >= 0.0 ? far : -1.0);
}

// Coordinates of a table that keep the centers of its first and last texels at 0 and 1.
public float unitToTexels(float u, float size)
{
    return (u + 0.5 / size) * (size / (size + 1.0));
}

public float texelsToUnit(float u, float size)
{
    return (u - 0.5 / size) * (size / (size - 1.0));
}

// Where the table of transmittance keeps a height and the cosine of a direction with the zenith,
// after Bruneton.
public float2 transmittanceUv(Atmosphere* air, float radius, float cosZenith)
{
    const float horizon = sqrt(max(0.0, air.topRadius * air.topRadius - air.bottomRadius * air.bottomRadius));
    const float rho = sqrt(max(0.0, radius * radius - air.bottomRadius * air.bottomRadius));
    const float discriminant = radius * radius * (cosZenith * cosZenith - 1.0) + air.topRadius * air.topRadius;
    const float distance = max(0.0, -radius * cosZenith + sqrt(max(discriminant, 0.0)));
    const float nearest = air.topRadius - radius;
    const float farthest = rho + horizon;
    return float2((distance - nearest) / max(farthest - nearest, 1e-6), rho / max(horizon, 1e-6));
}

public float3 transmittanceTowards(Atmosphere* air, float radius, float cosZenith)
{
    return atmosphereTransmittance.SampleLevel(clampSampler, transmittanceUv(air, radius, cosZenith), 0.0).rgb;
}

// Where the table of the sky keeps a direction seen from the camera: across, its angle from the sun
// around the vertical, finer towards the sun; down, its angle from the zenith, finer at the horizon.
public float2 skyViewUv(Atmosphere* air, float radius, float cosViewZenith, float cosSunAzimuth, bool groundHit)
{
    const float horizon = sqrt(max(radius * radius - air.bottomRadius * air.bottomRadius, 0.0));
    const float beta = acos(clamp(horizon / radius, -1.0, 1.0));
    const float zenithHorizon = pi - beta;
    const float viewZenith = acos(clamp(cosViewZenith, -1.0, 1.0));
    float v;
    if (!groundHit)
    {
        v = (1.0 - sqrt(max(1.0 - viewZenith / zenithHorizon, 0.0))) * 0.5;
    }
    else
    {
        v = sqrt(saturate((viewZenith - zenithHorizon) / max(beta, 1e-6))) * 0.5 + 0.5;
    }
    const float u = sqrt(saturate(-cosSunAzimuth * 0.5 + 0.5));
    return float2(unitToTexels(u, skyViewTableSize.x), unitToTexels(v, skyViewTableSize.y));
}

// The light of the sky in a direction from the camera, in nits, without the disk of the sun.
public float3 atmosphereSky(Atmosphere* air, float3 direction)
{
    const float3 up = float3(0.0, 1.0, 0.0);
    const float3 position = float3(0.0, air.cameraRadius, 0.0);
    const float cosViewZenith = direction.y;
    // The angle between the sun and the direction, around the vertical.
    float2 sunOnPlane = float2(1.0, 0.0);
    const float3 across = cross(up, direction);
    if (dot(across, across) > 1e-8)
    {
        const float3 side = normalize(across);
        const float3 forward = normalize(cross(side, up));
        sunOnPlane = float2(dot(air.sunDirection, forward), dot(air.sunDirection, side));
        sunOnPlane = dot(sunOnPlane, sunOnPlane) > 1e-8 ? normalize(sunOnPlane) : float2(1.0, 0.0);
    }
    const bool groundHit = raySphereNearest(position, direction, air.bottomRadius) >= 0.0;
    const float2 uv = skyViewUv(air, air.cameraRadius, cosViewZenith, sunOnPlane.x, groundHit);
    return skyViewTable.SampleLevel(clampSampler, uv, 0.0).rgb;
}

// The disk of the sun in a direction, in nits: its illuminance through the air spread over its disk.
public float3 atmosphereSunDisk(Atmosphere* air, float3 direction)
{
    const float3 position = float3(0.0, air.cameraRadius, 0.0);
    if (dot(direction, air.sunDirection) < air.sunCosRadius || raySphereNearest(position, direction, air.bottomRadius) >= 0.0)
    {
        return float3(0.0);
    }
    const float solidAngle = 2.0 * pi * max(1.0 - air.sunCosRadius, 1e-7);
    // Darker towards its edge, as the sun is.
    const float edge = saturate((dot(direction, air.sunDirection) - air.sunCosRadius) / max(1.0 - air.sunCosRadius, 1e-7));
    const float limb = 0.4 + 0.6 * sqrt(sqrt(edge));
    return air.sunIlluminance * transmittanceTowards(air, air.cameraRadius, direction.y) / solidAngle * air.sunDiskIntensity * limb;
}

// What the air between the camera and a point sends towards the camera, and the share of the point
// it lets through: the haze of what is far away, from the table of slices of distance.
public float3 atmosphereHaze(SceneData* scene, float3 exposed, float3 position)
{
    Atmosphere* air = scene.atmosphere;
    const float kilometers = length(position - scene.cameraPosition) * 0.001 * air.aerialScale;
    if (kilometers <= 0.0)
    {
        return exposed;
    }
    const float4 clip = mul(scene.unjitteredViewProjection, float4(position, 1.0));
    const float2 uv = clip.xy / max(clip.w, 1e-6) * 0.5 + 0.5;
    // The slices are finer near the camera: the first holds the first meters of haze.
    const float slice = sqrt(saturate(kilometers / air.aerialDistance)) * aerialSlices - 0.5;
    const float lower = clamp(floor(slice), 0.0, aerialSlices - 1.0);
    const float upper = min(lower + 1.0, aerialSlices - 1.0);
    const float blend = saturate(slice - lower);
    float4 haze = lerp(aerialPerspective.SampleLevel(clampSampler, float3(uv, lower), 0.0),
                       aerialPerspective.SampleLevel(clampSampler, float3(uv, upper), 0.0), blend);
    // Before the middle of the first slice, the haze grows from nothing.
    haze *= saturate((slice + 0.5) * 2.0);
    return exposed * (1.0 - haze.a) + haze.rgb * scene.exposure;
}

// Phase of Henyey-Greenstein: the share of the light turned by an angle, forward for g towards 1.
public float henyeyGreenstein(float cosAngle, float g)
{
    const float g2 = g * g;
    return (1.0 - g2) / (4.0 * pi * pow(max(1.0 + g2 - 2.0 * g * cosAngle, 1e-4), 1.5));
}

// The height fog between the camera and a point `distance` meters away in a direction: what it
// hides of the point, from 0 to its most, integrated along the ray as its density falls with height.
public float fogOpacity(Atmosphere* air, float3 origin, float3 direction, float distance)
{
    const float span = distance - air.fogStart;
    if (span <= 0.0 || air.fogDensity <= 0.0)
    {
        return 0.0;
    }
    const float thinning = 1.0 / max(air.fogFalloff, 1e-3);
    const float startHeight = origin.y + direction.y * air.fogStart - air.fogHeight;
    const float density = air.fogDensity * exp(clamp(-startHeight * thinning, -80.0, 80.0));
    const float rise = direction.y * span * thinning;
    const float depth = abs(rise) > 1e-4 ? density * span * (1.0 - exp(clamp(-rise, -80.0, 80.0))) / rise : density * span;
    return min(1.0 - exp(-depth), air.fogMaxOpacity);
}

// The light the fog sends towards the camera, in nits: the sky around it and the sun through it.
public float3 fogLight(SceneData* scene, float3 direction)
{
    Atmosphere* air = scene.atmosphere;
    const float3 sky = scene.environmentColor * scene.environmentIntensity;
    const float3 flat = normalize(float3(direction.x, 0.0, direction.z) + float3(0.0, 1e-4, 0.0));
    const float angle = -scene.environmentRotation;
    const float3 ambient = (irradianceEnvironment.SampleLevel(clampSampler, rotateAroundY(float3(0.0, 1.0, 0.0), angle), 0.0).rgb +
                            irradianceEnvironment.SampleLevel(clampSampler, rotateAroundY(flat, angle), 0.0).rgb) *
                           0.5 * sky;
    float3 sun = float3(0.0);
    if ((scene.sunFlags & sunEnabled) != 0)
    {
        sun = scene.sunIlluminance * henyeyGreenstein(dot(direction, -scene.sunDirection), air.fogSunAnisotropy) * air.fogSunScattering;
    }
    const float3 lit = air.fogColor * (ambient + sun);
    if (air.fogSkyBlend <= 0.0)
    {
        return lit;
    }
    // The sky at the horizon behind it, which the far fog melts into.
    const float3 level = normalize(float3(direction.x, max(direction.y, 0.02), direction.z));
    const float3 behind = (scene.atmosphereFlags & atmosphereAir) != 0
                              ? atmosphereSky(air, level)
                              : specularEnvironment.SampleLevel(clampSampler, rotateAroundY(level, angle), 2.0).rgb * sky;
    return lerp(lit, behind, saturate(air.fogSkyBlend));
}

// What the air and the fog make of a surface: hazed by the distance, then hidden by the fog. Takes
// and gives colours already exposed.
public float3 applyAtmosphere(SceneData* scene, float3 exposed, float3 position)
{
    if (scene.atmosphereFlags == 0)
    {
        return exposed;
    }
    float3 color = exposed;
    if ((scene.atmosphereFlags & atmosphereAir) != 0)
    {
        color = atmosphereHaze(scene, color, position);
    }
    if ((scene.atmosphereFlags & atmosphereFog) != 0)
    {
        const float3 offset = position - scene.cameraPosition;
        const float distance = length(offset);
        const float3 direction = offset / max(distance, 1e-4);
        const float opacity = fogOpacity(scene.atmosphere, scene.cameraPosition, direction, distance);
        color = lerp(color, fogLight(scene, direction) * scene.exposure, opacity);
    }
    return color;
}
