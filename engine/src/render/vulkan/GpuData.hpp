#pragma once

#include "Vulkan.hpp"

#include <devex/math/Math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

// CPU mirrors of the structures read by shaders/common.slang and the passes. Every member sits at
// the same offset in the C++ and Slang layouts, which the static assertions below keep true.
namespace devex::render::vulkan {

struct GpuVertex
{
    math::Vec3 position{0.0f};
    float u = 0.0f;
    math::Vec3 normal{0.0f};
    float v = 0.0f;
    math::Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// Texture fields are indices into the bindless texture array.
struct GpuMaterial
{
    math::Vec4 baseColorFactor{1.0f};
    math::Vec3 emissiveFactor{0.0f};
    float alphaCutoff = 0.5f;
    std::uint32_t baseColorTexture = 0;
    std::uint32_t metallicRoughnessTexture = 0;
    std::uint32_t normalTexture = 0;
    std::uint32_t occlusionTexture = 0;
    std::uint32_t emissiveTexture = 0;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    // 0 opaque, 1 mask, 2 blend.
    std::uint32_t alphaMode = 0;
    std::uint32_t doubleSided = 0;
    float padding = 0.0f;
};

struct GpuLight
{
    math::Vec3 position{0.0f};
    float range = 0.0f;
    math::Vec3 direction{0.0f, 0.0f, -1.0f};
    float spotScale = 0.0f;
    math::Vec3 intensity{0.0f};
    float spotOffset = 1.0f;
    // The first view of the shadow atlas this light owns, or -1 when it casts none. A point light
    // owns the six that follow, one per face of the cube around it.
    std::int32_t firstShadowView = -1;
    // How far the light reaches, which its views project between the near plane and it.
    float shadowFar = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

// One view of the shadow atlas: where it looks from, and the part of the atlas it was given.
struct GpuShadowView
{
    math::Mat4 viewProjection{1.0f};
    // Offset and size in the atlas, in texture coordinates.
    math::Vec4 tile{0.0f};
};

struct GpuCluster
{
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

inline constexpr std::uint32_t sunEnabledFlag = 1;
inline constexpr std::uint32_t sunCastsShadowsFlag = 2;

struct GpuSceneData
{
    math::Mat4 viewProjection{1.0f};
    math::Mat4 view{1.0f};
    math::Mat4 skyInverseViewProjection{1.0f};
    math::Vec3 cameraPosition{0.0f};
    float exposure = 1.0f;
    math::Vec3 sunDirection{0.0f, -1.0f, 0.0f};
    std::uint32_t sunFlags = 0;
    math::Vec3 sunIlluminance{0.0f};
    float environmentIntensity = 0.0f;
    math::Vec3 environmentColor{1.0f};
    float environmentRotation = 0.0f;
    math::Vec4 cascadeSplits{0.0f};
    math::Vec4 cascadeTexelSizes{0.0f};
    std::array<math::Mat4, 4> cascadeViewProjections{};
    float shadowDistance = 0.0f;
    float specularMipCount = 1.0f;
    float viewportWidth = 1.0f;
    float viewportHeight = 1.0f;
    std::uint32_t clusterCountX = 1;
    std::uint32_t clusterCountY = 1;
    std::uint32_t clusterCountZ = 1;
    std::uint32_t lightCount = 0;
    float clusterSliceScale = 0.0f;
    float clusterSliceBias = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
    VkDeviceAddress materials = 0;
    VkDeviceAddress lights = 0;
    VkDeviceAddress clusters = 0;
    VkDeviceAddress clusterLights = 0;
    // Maps the pixel requested for picking to the whole 1x1 pick target.
    math::Mat4 pickViewProjection{1.0f};
    // The same view and projection without the jitter of the temporal antialiasing, and the one of
    // the previous frame: what lies between them is the motion of a pixel.
    math::Mat4 unjitteredViewProjection{1.0f};
    math::Mat4 previousViewProjection{1.0f};
    // Takes a point of clip space back to the world, without the jitter.
    math::Mat4 inverseViewProjection{1.0f};
    // The views the local lights were given in the shadow atlas.
    VkDeviceAddress shadowViews = 0;
};

// How a vertex of a skinned mesh follows its bones, beside the vertex itself.
struct GpuVertexSkin
{
    std::array<std::uint32_t, 4> joints{};
    math::Vec4 weights{0.0f};
};

struct DrawPushConstants
{
    VkDeviceAddress scene = 0;
    VkDeviceAddress vertices = 0;
    math::Mat4 world{1.0f};
    std::uint32_t material = 0;
    std::uint32_t cascade = 0;
    // Written by the pick pass.
    std::uint32_t objectId = 0;
    // Whether the vertices follow the bones below rather than the world matrix. Shaders read the
    // flag instead of comparing the addresses, which would need 64-bit integers in SPIR-V.
    std::uint32_t skinned = 0;
    VkDeviceAddress skin = 0;
    VkDeviceAddress bones = 0;
};

// The prepass also needs where the instance stood on the previous frame. Vulkan 1.4 guarantees
// 256 bytes of push constants, which these fit in.
struct PrepassPushConstants
{
    DrawPushConstants draw;
    math::Mat4 previousWorld{1.0f};
    VkDeviceAddress previousBones = 0;
};

struct GpuOverlayVertex
{
    math::Vec3 position{0.0f};
    float padding = 0.0f;
    math::Vec4 color{1.0f};
};

struct OverlayPushConstants
{
    VkDeviceAddress scene = 0;
    VkDeviceAddress vertices = 0;
    math::Vec4 outlineColor{1.0f};
    float depthScale = 1.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
    float padding2 = 0.0f;
};

// One vertex of the interface. Its layout matches UiVertex in shaders/ui.slang.
struct GpuUiVertex
{
    math::Vec2 position{0.0f};
    math::Vec2 uv{0.0f};
    math::Vec4 color{1.0f};
};

struct UiPushConstants
{
    VkDeviceAddress vertices = 0;
    VkDeviceAddress indices = 0;
    math::Vec2 inverseViewport{0.0f};
    std::uint32_t kind = 0;
    std::uint32_t texture = 0;
    math::Vec4 rect{0.0f};
    float radius = 0.0f;
    float sharpness = 1.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

struct SkyPushConstants
{
    VkDeviceAddress scene = 0;
};

struct AoPushConstants
{
    VkDeviceAddress scene = 0;
    float radius = 0.5f;
    float intensity = 1.0f;
    float frame = 0.0f;
    float padding = 0.0f;
};

struct TaaPushConstants
{
    float blend = 0.9f;
    float historyValid = 0.0f;
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
};

struct BloomPushConstants
{
    std::uint32_t source = 0;
    float texelWidth = 0.0f;
    float texelHeight = 0.0f;
    float strength = 1.0f;
};

struct TonemapPushConstants
{
    std::uint32_t tonemapper = 0;
    // How much of the bloom is added to the image; 0 leaves it alone.
    float bloom = 0.0f;
    float vignette = 0.0f;
    float grain = 0.0f;
    float chromatic = 0.0f;
    float time = 0.0f;
    std::uint32_t colorTable = 0;
    float colorTableSize = 0.0f;
    // The size of the image drawn into, smaller than the scene for a capture.
    float targetWidth = 0.0f;
    float targetHeight = 0.0f;
};

struct LuminancePushConstants
{
    VkDeviceAddress output = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct BakePushConstants
{
    std::uint32_t size = 0;
    float roughness = 0.0f;
    std::uint32_t sampleCount = 0;
    float sourceLod = 0.0f;
    float sourceSize = 0.0f;
    float padding = 0.0f;
};

static_assert(sizeof(GpuVertex) == 48);
static_assert(offsetof(GpuVertex, normal) == 16);
static_assert(offsetof(GpuVertex, tangent) == 32);

static_assert(sizeof(GpuMaterial) == 80);
static_assert(offsetof(GpuMaterial, baseColorTexture) == 32);
static_assert(offsetof(GpuMaterial, emissiveTexture) == 48);
static_assert(offsetof(GpuMaterial, doubleSided) == 72);

static_assert(sizeof(GpuLight) == 64);
static_assert(sizeof(GpuShadowView) == 80);
static_assert(offsetof(GpuLight, direction) == 16);
static_assert(offsetof(GpuLight, intensity) == 32);

static_assert(sizeof(GpuCluster) == 8);

static_assert(offsetof(GpuSceneData, cameraPosition) == 192);
static_assert(offsetof(GpuSceneData, sunDirection) == 208);
static_assert(offsetof(GpuSceneData, environmentColor) == 240);
static_assert(offsetof(GpuSceneData, cascadeSplits) == 256);
static_assert(offsetof(GpuSceneData, cascadeViewProjections) == 288);
static_assert(offsetof(GpuSceneData, shadowDistance) == 544);
static_assert(offsetof(GpuSceneData, clusterCountX) == 560);
static_assert(offsetof(GpuSceneData, clusterSliceScale) == 576);
static_assert(offsetof(GpuSceneData, materials) == 592);
static_assert(offsetof(GpuSceneData, pickViewProjection) == 624);
static_assert(offsetof(GpuSceneData, unjitteredViewProjection) == 688);
static_assert(sizeof(GpuSceneData) == 888);

// Vulkan guarantees 128 bytes of push constants on every device.
static_assert(sizeof(DrawPushConstants) == 112);
static_assert(offsetof(DrawPushConstants, world) == 16);
static_assert(offsetof(DrawPushConstants, material) == 80);
static_assert(offsetof(DrawPushConstants, objectId) == 88);
static_assert(offsetof(DrawPushConstants, skin) == 96);
static_assert(offsetof(DrawPushConstants, bones) == 104);
static_assert(sizeof(GpuVertexSkin) == 32);
static_assert(sizeof(GpuOverlayVertex) == 32);
static_assert(offsetof(OverlayPushConstants, depthScale) == 32);
static_assert(sizeof(OverlayPushConstants) == 48);
static_assert(sizeof(LuminancePushConstants) == 16);
static_assert(sizeof(BakePushConstants) == 24);

} // namespace devex::render::vulkan
