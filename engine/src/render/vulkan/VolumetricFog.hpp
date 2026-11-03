#pragma once

#include "DescriptorSets.hpp"
#include "Device.hpp"
#include "Memory.hpp"
#include "Pipeline.hpp"
#include "Upload.hpp"
#include "Vulkan.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace devex::render::vulkan {

// The volumetric fog of the main view, in froxels (shaders/fog.slang): every frame, each cell of a
// grid following the view is lit and mixed with the previous frames, in one of two images that take
// turns, then each column is integrated into the image the surfaces and the sky read. Every image
// stays in the general layout, written by compute and sampled by the frames.
class VolumetricFogRenderer
{
public:
    static constexpr std::uint32_t width = 160;
    static constexpr std::uint32_t height = 90;
    static constexpr std::uint32_t depth = 64;

    [[nodiscard]] static core::Result<std::unique_ptr<VolumetricFogRenderer>> create(const Device& device, const Allocator& allocator,
                                                                                    UploadContext& upload, const DescriptorSets& descriptors,
                                                                                    const std::filesystem::path& shaderDirectory);

    ~VolumetricFogRenderer();

    VolumetricFogRenderer(const VolumetricFogRenderer&) = delete;
    VolumetricFogRenderer& operator=(const VolumetricFogRenderer&) = delete;

    // Records the fog of a frame, after its shadows and before anything samples the integrated image:
    // `scene` is the address of its scene data, `fog` of its GpuVolumetricFog. `frame` is the frame
    // set, for the shadows of the sun.
    void record(VkCommandBuffer commands, VkDescriptorSet global, VkDescriptorSet frame, VkDeviceAddress scene, VkDeviceAddress fog);

    [[nodiscard]] VkImageView integratedView() const noexcept;

private:
    explicit VolumetricFogRenderer(const Device& device) noexcept;

    // A set of the fog's own: the image a pass writes, and the one it reads.
    [[nodiscard]] VkDescriptorSet makeSet(VkImageView written, VkImageView read);

    const Device& m_device;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::optional<Pipeline> m_light;
    std::optional<Pipeline> m_integrate;
    // The lit froxels, taking turns: a frame writes one and reads the other as its history.
    std::array<std::optional<Image>, 2> m_froxels;
    std::optional<Image> m_integrated;
    std::array<VkDescriptorSet, 2> m_lightSets{};
    std::array<VkDescriptorSet, 2> m_integrateSets{};
    std::uint32_t m_current = 0;
};

} // namespace devex::render::vulkan
