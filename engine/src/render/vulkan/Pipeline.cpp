#include "Pipeline.hpp"

#include <devex/core/Path.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <utility>
#include <vector>

namespace devex::render::vulkan {
namespace {

[[nodiscard]] core::Result<std::vector<std::uint32_t>> readSpirv(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return core::makeError(core::ErrorCode::NotFound, "cannot open shader '{}'",
                               core::toUtf8(path));
    }
    const std::streamsize size = file.tellg();
    if (size <= 0 || size % sizeof(std::uint32_t) != 0)
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' is not a SPIR-V module",
                               core::toUtf8(path));
    }

    std::vector<std::uint32_t> words(static_cast<std::size_t>(size) / sizeof(std::uint32_t));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), size))
    {
        return core::makeError(core::ErrorCode::Io, "cannot read shader '{}'", core::toUtf8(path));
    }
    return words;
}

// Destroys the shader module once the pipeline that needs it has been created.
class ShaderModule
{
public:
    [[nodiscard]] static core::Result<ShaderModule> load(VkDevice device,
                                                         const std::filesystem::path& path)
    {
        const core::Result<std::vector<std::uint32_t>> spirv = readSpirv(path);
        if (!spirv)
        {
            return std::unexpected(spirv.error());
        }
        const VkShaderModuleCreateInfo moduleInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = spirv->size() * sizeof(std::uint32_t),
            .pCode = spirv->data(),
        };
        VkShaderModule module = VK_NULL_HANDLE;
        DEVEX_VK_TRY(vkCreateShaderModule, device, &moduleInfo, nullptr, &module);
        return ShaderModule(device, module);
    }

    ShaderModule(ShaderModule&& other) noexcept
        : m_device(other.m_device)
        , m_module(std::exchange(other.m_module, VK_NULL_HANDLE))
    {
    }

    ShaderModule& operator=(ShaderModule&&) = delete;

    ~ShaderModule()
    {
        if (m_module != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(m_device, m_module, nullptr);
        }
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    [[nodiscard]] VkShaderModule handle() const noexcept
    {
        return m_module;
    }

private:
    ShaderModule(VkDevice device, VkShaderModule module) noexcept
        : m_device(device)
        , m_module(module)
    {
    }

    VkDevice m_device;
    VkShaderModule m_module;
};

[[nodiscard]] core::Result<VkPipelineLayout> createLayout(VkDevice device,
                                                          std::span<const VkDescriptorSetLayout> setLayouts,
                                                          std::uint32_t pushConstantSize,
                                                          VkShaderStageFlags stages)
{
    const VkPushConstantRange pushConstants{
        .stageFlags = stages,
        .offset = 0,
        .size = pushConstantSize,
    };
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = static_cast<std::uint32_t>(setLayouts.size()),
        .pSetLayouts = setLayouts.data(),
        .pushConstantRangeCount = pushConstantSize > 0 ? 1u : 0u,
        .pPushConstantRanges = pushConstantSize > 0 ? &pushConstants : nullptr,
    };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    DEVEX_VK_TRY(vkCreatePipelineLayout, device, &layoutInfo, nullptr, &layout);
    return layout;
}

} // namespace

Pipeline::Pipeline(VkDevice device, VkPipelineLayout layout, VkPipeline pipeline) noexcept
    : m_device(device)
    , m_layout(layout)
    , m_pipeline(pipeline)
{
}

Pipeline::Pipeline(Pipeline&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_layout(std::exchange(other.m_layout, VK_NULL_HANDLE))
    , m_pipeline(std::exchange(other.m_pipeline, VK_NULL_HANDLE))
{
}

Pipeline& Pipeline::operator=(Pipeline&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
        m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
    }
    return *this;
}

Pipeline::~Pipeline()
{
    destroy();
}

void Pipeline::destroy() noexcept
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
        m_pipeline = VK_NULL_HANDLE;
        m_layout = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
    }
}

VkPipelineLayout Pipeline::layout() const noexcept
{
    return m_layout;
}

VkPipeline Pipeline::handle() const noexcept
{
    return m_pipeline;
}

core::Result<Pipeline> createGraphicsPipeline(VkDevice device, const GraphicsPipelineConfig& config)
{
    const core::Result<ShaderModule> shaderModule = ShaderModule::load(device, config.shaderPath);
    if (!shaderModule)
    {
        return std::unexpected(shaderModule.error());
    }
    const core::Result<VkPipelineLayout> layout =
        createLayout(device, config.setLayouts, config.pushConstantSize,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    std::uint32_t stageCount = 0;
    stages[stageCount++] = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = shaderModule->handle(),
        .pName = config.vertexEntry,
    };
    if (config.fragmentEntry != nullptr)
    {
        stages[stageCount++] = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = shaderModule->handle(),
            .pName = config.fragmentEntry,
        };
    }

    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = config.topology,
    };
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = config.depthClamp ? VK_TRUE : VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = config.cullMode,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = config.depthBias ? VK_TRUE : VK_FALSE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = config.samples,
    };
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = config.depthTest ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = config.depthWrite ? VK_TRUE : VK_FALSE,
        .depthCompareOp = config.depthCompare,
    };
    const VkPipelineColorBlendAttachmentState colorAttachment{
        .blendEnable = config.alphaBlend ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const bool hasColor = config.colorFormat != VK_FORMAT_UNDEFINED;
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = hasColor ? 1u : 0u,
        .pAttachments = hasColor ? &colorAttachment : nullptr,
    };
    std::array<VkDynamicState, 3> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                                                VK_DYNAMIC_STATE_DEPTH_BIAS};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = config.depthBias ? 3u : 2u,
        .pDynamicStates = dynamicStates.data(),
    };
    const VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = hasColor ? 1u : 0u,
        .pColorAttachmentFormats = hasColor ? &config.colorFormat : nullptr,
        .depthAttachmentFormat = config.depthFormat,
    };

    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = stageCount,
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = *layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                          nullptr, &pipeline);
        result < VK_SUCCESS)
    {
        vkDestroyPipelineLayout(device, *layout, nullptr);
        return vulkanError("vkCreateGraphicsPipelines", result);
    }
    return Pipeline(device, *layout, pipeline);
}

core::Result<Pipeline> createComputePipeline(VkDevice device, const ComputePipelineConfig& config)
{
    const core::Result<ShaderModule> shaderModule = ShaderModule::load(device, config.shaderPath);
    if (!shaderModule)
    {
        return std::unexpected(shaderModule.error());
    }
    const core::Result<VkPipelineLayout> layout = createLayout(
        device, config.setLayouts, config.pushConstantSize, VK_SHADER_STAGE_COMPUTE_BIT);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }

    const VkComputePipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage =
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shaderModule->handle(),
                .pName = config.entry,
            },
        .layout = *layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (const VkResult result =
            vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
        result < VK_SUCCESS)
    {
        vkDestroyPipelineLayout(device, *layout, nullptr);
        return vulkanError("vkCreateComputePipelines", result);
    }
    return Pipeline(device, *layout, pipeline);
}

} // namespace devex::render::vulkan
