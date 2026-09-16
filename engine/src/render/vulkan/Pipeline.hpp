#pragma once

#include "Vulkan.hpp"

#include <filesystem>

namespace devex::render::vulkan {

// Owns a graphics pipeline and its layout.
class GraphicsPipeline
{
public:
    GraphicsPipeline(VkDevice device, VkPipelineLayout layout, VkPipeline pipeline) noexcept;

    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;
    ~GraphicsPipeline();

    GraphicsPipeline(const GraphicsPipeline&) = delete;
    GraphicsPipeline& operator=(const GraphicsPipeline&) = delete;

    [[nodiscard]] VkPipelineLayout layout() const noexcept;
    [[nodiscard]] VkPipeline handle() const noexcept;

private:
    void destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

struct MeshPipelineConfig
{
    std::filesystem::path shaderPath;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    // Set 0: the bindless textures.
    VkDescriptorSetLayout textureSetLayout = VK_NULL_HANDLE;
    // False for double-sided materials.
    bool cullBackFaces = true;
};

// Builds the pipeline of shaders/mesh.slang: vertex pulling, bindless textures and reversed depth
// testing.
[[nodiscard]] core::Result<GraphicsPipeline> createMeshPipeline(VkDevice device,
                                                               const MeshPipelineConfig& config);

} // namespace devex::render::vulkan
