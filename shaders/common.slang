// Data shared by the frame's shaders. Layouts must match engine/src/render/vulkan/GpuData.hpp,
// and bindings engine/src/render/vulkan/DescriptorSets.hpp.
module common;

public static const float pi = 3.14159265358979;
public static const uint cascadeCount = 4;
public static const uint alphaModeMask = 1;
public static const uint alphaModeBlend = 2;
public static const uint sunEnabled = 1;
public static const uint sunCastsShadows = 2;

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
    public float padding;
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
    public float padding0;
    public float padding1;
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
};

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
// The image the tonemapping reads: what the antialiasing resolved, or the scene as it was drawn.
[[vk::binding(9, 1)]]
public Texture2D resolvedTexture;
// The halved images the bloom is built from, the largest first.
[[vk::binding(10, 1)]]
public Texture2D bloomChain[5];

public float4 sampleTexture(uint index, float2 uv)
{
    return textures[NonUniformResourceIndex(index)].Sample(materialSampler, uv);
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
