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
};

struct DrawPushConstants
{
    VkDeviceAddress scene = 0;
    VkDeviceAddress vertices = 0;
    math::Mat4 world{1.0f};
    std::uint32_t material = 0;
    std::uint32_t cascade = 0;
};

struct SkyPushConstants
{
    VkDeviceAddress scene = 0;
};

struct TonemapPushConstants
{
    std::uint32_t tonemapper = 0;
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

static_assert(sizeof(GpuLight) == 48);
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
static_assert(sizeof(GpuSceneData) == 624);

// Vulkan guarantees 128 bytes of push constants on every device.
static_assert(sizeof(DrawPushConstants) == 88);
static_assert(offsetof(DrawPushConstants, world) == 16);
static_assert(offsetof(DrawPushConstants, material) == 80);
static_assert(sizeof(LuminancePushConstants) == 16);
static_assert(sizeof(BakePushConstants) == 24);

} // namespace devex::render::vulkan
