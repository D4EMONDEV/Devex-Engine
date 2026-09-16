#pragma once

#include "Vulkan.hpp"

#include <devex/math/Math.hpp>

#include <cstddef>

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

struct GpuSceneData
{
    math::Mat4 viewProjection{1.0f};
    math::Vec3 lightDirection{0.0f, -1.0f, 0.0f};
    float ambient = 0.0f;
};

struct DrawPushConstants
{
    VkDeviceAddress scene = 0;
    VkDeviceAddress vertices = 0;
    math::Mat4 world{1.0f};
};

static_assert(sizeof(GpuVertex) == 32);
static_assert(offsetof(GpuVertex, u) == 12);
static_assert(offsetof(GpuVertex, normal) == 16);
static_assert(offsetof(GpuVertex, v) == 28);

static_assert(sizeof(GpuSceneData) == 80);
static_assert(offsetof(GpuSceneData, lightDirection) == 64);
static_assert(offsetof(GpuSceneData, ambient) == 76);

// Vulkan guarantees 128 bytes of push constants on every device.
static_assert(sizeof(DrawPushConstants) == 80);
static_assert(offsetof(DrawPushConstants, vertices) == 8);
static_assert(offsetof(DrawPushConstants, world) == 16);

} // namespace devex::render::vulkan
