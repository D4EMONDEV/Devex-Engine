#pragma once

#include "Device.hpp"
#include "Vulkan.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace devex::render::vulkan {

// The descriptor sets shared by the frame's shaders.
//
// Set 0, global and updated while frames using it are in flight: every texture in one array
// indexed by materials, the image based lighting maps, the sky, and their samplers.
// Set 1, one per frame context: the images produced earlier in the same frame, namely the shadow
// map, the scene color, the mask of the selected objects, the motion and the normals of the
// prepass, the ambient occlusion, and the image the previous frame resolved.
class DescriptorSets
{
public:
    // Global set bindings, matching shaders/common.slang.
    static constexpr std::uint32_t texturesBinding = 0;
    static constexpr std::uint32_t materialSamplerBinding = 1;
    static constexpr std::uint32_t specularBinding = 2;
    static constexpr std::uint32_t irradianceBinding = 3;
    static constexpr std::uint32_t brdfBinding = 4;
    static constexpr std::uint32_t clampSamplerBinding = 5;
    static constexpr std::uint32_t skyBinding = 6;
    static constexpr std::uint32_t skySamplerBinding = 7;
    // Frame set bindings.
    static constexpr std::uint32_t shadowMapBinding = 0;
    static constexpr std::uint32_t shadowSamplerBinding = 1;
    static constexpr std::uint32_t sceneColorBinding = 2;
    static constexpr std::uint32_t selectionMaskBinding = 3;
    static constexpr std::uint32_t velocityBinding = 4;
    static constexpr std::uint32_t normalBinding = 5;
    static constexpr std::uint32_t ambientOcclusionBinding = 6;
    static constexpr std::uint32_t historyBinding = 7;
    static constexpr std::uint32_t depthBinding = 8;
    static constexpr std::uint32_t resolvedBinding = 9;
    static constexpr std::uint32_t bloomBinding = 10;
    static constexpr std::uint32_t localShadowBinding = 11;
    // How many halvings the bloom is built from.
    static constexpr std::uint32_t bloomLevels = 5;

    // The images of one frame context, all of them sampled by later passes of the frame.
    struct FrameImages
    {
        VkImageView shadowMap = VK_NULL_HANDLE;
        VkImageView sceneColor = VK_NULL_HANDLE;
        VkImageView selectionMask = VK_NULL_HANDLE;
        VkImageView velocity = VK_NULL_HANDLE;
        VkImageView normal = VK_NULL_HANDLE;
        VkImageView ambientOcclusion = VK_NULL_HANDLE;
        VkImageView history = VK_NULL_HANDLE;
        VkImageView depth = VK_NULL_HANDLE;
        // What the tonemapping reads: the image the antialiasing resolved, or the scene itself.
        VkImageView resolved = VK_NULL_HANDLE;
        std::array<VkImageView, bloomLevels> bloom{};
        // The shadows of the local lights, side by side in one image.
        VkImageView localShadowMap = VK_NULL_HANDLE;

        [[nodiscard]] bool operator==(const FrameImages&) const noexcept = default;
    };

    [[nodiscard]] static core::Result<DescriptorSets> create(const Device& device,
                                                             std::uint32_t textureCapacity,
                                                             std::uint32_t frameCount);

    DescriptorSets(DescriptorSets&& other) noexcept;
    DescriptorSets& operator=(DescriptorSets&& other) noexcept;
    ~DescriptorSets();

    DescriptorSets(const DescriptorSets&) = delete;
    DescriptorSets& operator=(const DescriptorSets&) = delete;

    [[nodiscard]] VkDescriptorSetLayout globalLayout() const noexcept;
    [[nodiscard]] VkDescriptorSetLayout frameLayout() const noexcept;
    [[nodiscard]] VkDescriptorSet global() const noexcept;
    [[nodiscard]] VkDescriptorSet frame(std::uint32_t index) const noexcept;
    [[nodiscard]] std::uint32_t textureCapacity() const noexcept;

    // Views must be in the shader read-only layout whenever a frame samples them.
    void setTexture(std::uint32_t slot, VkImageView view) noexcept;
    void setEnvironment(VkImageView specular, VkImageView irradiance, VkImageView sky) noexcept;
    void setBrdfLut(VkImageView view) noexcept;
    // Only while no frame using the set is in flight.
    void setFrameImages(std::uint32_t frame, const FrameImages& images) noexcept;

private:
    DescriptorSets() = default;
    void destroy() noexcept;
    void writeImage(VkDescriptorSet set, std::uint32_t binding, std::uint32_t element,
                    VkImageView view,
                    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) const noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkSampler m_materialSampler = VK_NULL_HANDLE;
    VkSampler m_clampSampler = VK_NULL_HANDLE;
    VkSampler m_skySampler = VK_NULL_HANDLE;
    VkSampler m_shadowSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_globalLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_frameLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_globalPool = VK_NULL_HANDLE;
    VkDescriptorPool m_framePool = VK_NULL_HANDLE;
    VkDescriptorSet m_global = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> m_frames;
    std::uint32_t m_textureCapacity = 0;
};

} // namespace devex::render::vulkan
