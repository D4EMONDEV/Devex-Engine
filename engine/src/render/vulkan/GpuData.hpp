#pragma once

#include "Vulkan.hpp"

#include <devex/math/Math.hpp>

#include <cstddef>
#include <cstdint>

// CPU mirrors of the structures read by shaders/mesh.slang. Every member sits at the same offset
// in the C++ and Slang layouts, which the static assertions below keep true.
namespace devex::render::vulkan {

struct GpuVertex
{
    math::Vec3 position{0.0f};
    float u = 0.0f;
    math::Vec3 normal{0.0f};
    float v = 0.0f;
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

struct GpuSceneData
{
    math::Mat4 viewProjection{1.0f};
    math::Vec3 lightDirection{0.0f, -1.0f, 0.0f};
    float ambient = 0.0f;
    // Array of GpuMaterial.
    VkDeviceAddress materials = 0;
};

struct DrawPushConstants
{
    VkDeviceAddress scene = 0;
    VkDeviceAddress vertices = 0;
    math::Mat4 world{1.0f};
    std::uint32_t material = 0;
    std::uint32_t padding = 0;
};

static_assert(sizeof(GpuVertex) == 32);
static_assert(offsetof(GpuVertex, u) == 12);
static_assert(offsetof(GpuVertex, normal) == 16);
static_assert(offsetof(GpuVertex, v) == 28);

static_assert(sizeof(GpuMaterial) == 80);
static_assert(offsetof(GpuMaterial, emissiveFactor) == 16);
static_assert(offsetof(GpuMaterial, alphaCutoff) == 28);
static_assert(offsetof(GpuMaterial, baseColorTexture) == 32);
static_assert(offsetof(GpuMaterial, emissiveTexture) == 48);
static_assert(offsetof(GpuMaterial, occlusionStrength) == 64);
static_assert(offsetof(GpuMaterial, doubleSided) == 72);

static_assert(sizeof(GpuSceneData) == 88);
static_assert(offsetof(GpuSceneData, lightDirection) == 64);
static_assert(offsetof(GpuSceneData, ambient) == 76);
static_assert(offsetof(GpuSceneData, materials) == 80);

// Vulkan guarantees 128 bytes of push constants on every device.
static_assert(sizeof(DrawPushConstants) == 88);
static_assert(offsetof(DrawPushConstants, vertices) == 8);
static_assert(offsetof(DrawPushConstants, world) == 16);
static_assert(offsetof(DrawPushConstants, material) == 80);

} // namespace devex::render::vulkan
