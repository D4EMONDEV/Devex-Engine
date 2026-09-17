#pragma once

#include "Device.hpp"
#include "GpuData.hpp"
#include "Memory.hpp"
#include "Pipeline.hpp"
#include "Upload.hpp"
#include "Vulkan.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace devex::render::vulkan {

// Image based lighting derived from one environment.
struct EnvironmentMaps
{
    // Reflections prefiltered for increasing roughness, one mip level per step.
    Image specular;
    // Average incoming radiance around each normal, which a Lambertian surface reflects.
    Image irradiance;
};

// Precomputes image based lighting on the GPU with compute shaders, synchronously. The results are
// new images, so frames in flight keep using the previous ones safely.
class EnvironmentBaker
{
public:
    static constexpr std::uint32_t radianceSize = 512;
    static constexpr std::uint32_t specularSize = 256;
    static constexpr std::uint32_t specularMipCount = 6;
    static constexpr std::uint32_t irradianceSize = 32;
    static constexpr std::uint32_t brdfTableSize = 128;
    static constexpr VkFormat format = VK_FORMAT_R16G16B16A16_SFLOAT;

    [[nodiscard]] static core::Result<std::unique_ptr<EnvironmentBaker>> create(
        const Device& device, const Allocator& allocator, UploadContext& upload,
        const std::filesystem::path& shaderDirectory);

    ~EnvironmentBaker();

    EnvironmentBaker(const EnvironmentBaker&) = delete;
    EnvironmentBaker& operator=(const EnvironmentBaker&) = delete;

    // The table indexed by the cosine between normal and view, and by roughness.
    [[nodiscard]] core::Result<Image> bakeBrdfTable();

    // From an equirectangular image in the shader read-only layout, with its mip levels.
    [[nodiscard]] core::Result<EnvironmentMaps> bake(VkImageView equirectangular,
                                                     std::uint32_t equirectangularWidth);

private:
    EnvironmentBaker(const Device& device, const Allocator& allocator, UploadContext& upload) noexcept;

    void writeSet(VkImageView equirectangular, VkImageView radiance, VkImageView faces,
                  VkImageView table) noexcept;
    [[nodiscard]] core::Result<void> dispatch(const Pipeline& pipeline, const BakePushConstants& constants,
                                              std::uint32_t layers);

    const Device& m_device;
    const Allocator& m_allocator;
    UploadContext& m_upload;
    VkSampler m_equirectangularSampler = VK_NULL_HANDLE;
    VkSampler m_cubeSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    std::optional<Pipeline> m_toCube;
    std::optional<Pipeline> m_prefilter;
    std::optional<Pipeline> m_irradiance;
    std::optional<Pipeline> m_brdf;
    // Stand-ins for the images a dispatch does not use, so that every descriptor stays valid.
    std::optional<Image> m_placeholder;
    std::optional<Image> m_placeholderCube;
};

} // namespace devex::render::vulkan
