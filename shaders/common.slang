// Data shared by the frame's shaders. Layouts must match engine/src/render/vulkan/GpuData.hpp,
// and bindings engine/src/render/vulkan/DescriptorSets.hpp.
module common;

public static const float pi = 3.14159265358979;
public static const uint cascadeCount = 4;
public static const uint alphaModeMask = 1;
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
};

public struct DrawData
{
    public SceneData* scene;
    public Vertex* vertices;
    public float4x4 world;
    public uint material;
    public uint cascade;
    public uint objectId;
    public uint padding;
};

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
[[vk::binding(2, 1)]]
public Texture2D sceneColor;
// Coverage of the selected objects, outlined by the overlay.
[[vk::binding(3, 1)]]
public Texture2D selectionMask;

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
