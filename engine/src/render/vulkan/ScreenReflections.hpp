#pragma once

#include "DescriptorSets.hpp"
#include "Device.hpp"
#include "Memory.hpp"
#include "Pipeline.hpp"
#include "Upload.hpp"
#include "Vulkan.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace devex::render::vulkan {

// What the surfaces of the main view reflect of the screen, in environmentLight (shaders/pbr.slang):
// the scene of the previous frame at half its size, then smaller and blurrier for rougher surfaces.
// The image stays in the general layout, written by compute and by blits, and sampled by the frames.
class ScreenReflectionRenderer
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<ScreenReflectionRenderer>> create(const Device& device, const Allocator& allocator,
                                                                                       UploadContext& upload, const DescriptorSets& descriptors,
                                                                                       const std::filesystem::path& shaderDirectory);

    ~ScreenReflectionRenderer();

    ScreenReflectionRenderer(const ScreenReflectionRenderer&) = delete;
    ScreenReflectionRenderer& operator=(const ScreenReflectionRenderer&) = delete;

    // Makes the image half the size of the scene, waiting for the frames that still read the old one.
    [[nodiscard]] core::Result<void> ensure(math::Extent2D sceneExtent);
    // Records filling it from the scene of a frame, after the surfaces of that frame read it, with the
    // exposure the scene was drawn at. `frame` is the frame set, which names the scene.
    void record(VkCommandBuffer commands, VkDescriptorSet global, VkDescriptorSet frame, float exposure);
    // Whatever it holds is too old to reflect: a frame that does not reflect did not fill it.
    void forget() noexcept;

    // Whether a frame filled it, for the next one to read.
    [[nodiscard]] bool filled() const noexcept;
    [[nodiscard]] float exposure() const noexcept;
    [[nodiscard]] std::uint32_t levels() const noexcept;
    [[nodiscard]] VkImageView view() const noexcept;

private:
    ScreenReflectionRenderer(const Device& device, const Allocator& allocator, UploadContext& upload) noexcept;

    const Device& m_device;
    const Allocator& m_allocator;
    UploadContext& m_upload;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet m_set = VK_NULL_HANDLE;
    std::optional<Pipeline> m_shrink;
    std::optional<Image> m_image;
    math::Extent2D m_sceneExtent;
    bool m_filled = false;
    float m_exposure = 1.0f;
};

} // namespace devex::render::vulkan
