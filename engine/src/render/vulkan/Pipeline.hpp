#pragma once

#include "Vulkan.hpp"

#include <cstdint>
#include <filesystem>
#include <span>

namespace devex::render::vulkan {

// Owns a graphics or compute pipeline and its layout.
class Pipeline
{
public:
    Pipeline(VkDevice device, VkPipelineLayout layout, VkPipeline pipeline) noexcept;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;
    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    [[nodiscard]] VkPipelineLayout layout() const noexcept;
    [[nodiscard]] VkPipeline handle() const noexcept;

private:
    void destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
};

struct GraphicsPipelineConfig
{
    // A compiled Slang module holding the entry points.
    std::filesystem::path shaderPath;
    const char* vertexEntry = "vertexMain";
    // Null for a pipeline that only writes depth.
    const char* fragmentEntry = "fragmentMain";
    std::span<const VkDescriptorSetLayout> setLayouts;
    // Push constants visible to both stages; zero for none.
    std::uint32_t pushConstantSize = 0;
    // Undefined for no color attachment.
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkCullModeFlags cullMode = VK_CULL_MODE_BACK_BIT;
    bool depthTest = true;
    bool depthWrite = true;
    VkCompareOp depthCompare = VK_COMPARE_OP_GREATER_OR_EQUAL;
    // Enables depth bias, whose values are set with vkCmdSetDepthBias.
    bool depthBias = false;
    // Clamps depth instead of clipping, for shadow casters beyond the near plane.
    bool depthClamp = false;
};

// Pipelines draw triangle lists without vertex input: shaders pull vertices from buffers, or
// generate them, as fullscreen passes do. Viewport and scissor are dynamic.
[[nodiscard]] core::Result<Pipeline> createGraphicsPipeline(VkDevice device,
                                                            const GraphicsPipelineConfig& config);

struct ComputePipelineConfig
{
    std::filesystem::path shaderPath;
    const char* entry = "computeMain";
    std::span<const VkDescriptorSetLayout> setLayouts;
    std::uint32_t pushConstantSize = 0;
};

[[nodiscard]] core::Result<Pipeline> createComputePipeline(VkDevice device,
                                                           const ComputePipelineConfig& config);

} // namespace devex::render::vulkan
