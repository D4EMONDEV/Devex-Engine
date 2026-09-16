#include "Pipeline.hpp"

#include "GpuData.hpp"

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
    ShaderModule(VkDevice device, VkShaderModule module) noexcept
        : m_device(device)
        , m_module(module)
    {
    }

    ~ShaderModule()
    {
        vkDestroyShaderModule(m_device, m_module, nullptr);
    }

    ShaderModule(const ShaderModule&) = delete;
    ShaderModule& operator=(const ShaderModule&) = delete;

    [[nodiscard]] VkShaderModule handle() const noexcept
    {
        return m_module;
    }

private:
    VkDevice m_device;
    VkShaderModule m_module;
};

} // namespace

GraphicsPipeline::GraphicsPipeline(VkDevice device, VkPipelineLayout layout,
                                   VkPipeline pipeline) noexcept
    : m_device(device)
    , m_layout(layout)
    , m_pipeline(pipeline)
{
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
    : m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_layout(std::exchange(other.m_layout, VK_NULL_HANDLE))
    , m_pipeline(std::exchange(other.m_pipeline, VK_NULL_HANDLE))
{
}

GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept
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

GraphicsPipeline::~GraphicsPipeline()
{
    destroy();
}

void GraphicsPipeline::destroy() noexcept
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

VkPipelineLayout GraphicsPipeline::layout() const noexcept
{
    return m_layout;
}

VkPipeline GraphicsPipeline::handle() const noexcept
{
    return m_pipeline;
}

core::Result<GraphicsPipeline> createMeshPipeline(VkDevice device, const MeshPipelineConfig& config)
{
    const core::Result<std::vector<std::uint32_t>> spirv = readSpirv(config.shaderPath);
    if (!spirv)
    {
        return std::unexpected(spirv.error());
    }

    const VkShaderModuleCreateInfo moduleInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv->size() * sizeof(std::uint32_t),
        .pCode = spirv->data(),
    };
    VkShaderModule moduleHandle = VK_NULL_HANDLE;
    DEVEX_VK_TRY(vkCreateShaderModule, device, &moduleInfo, nullptr, &moduleHandle);
    const ShaderModule shaderModule(device, moduleHandle);

    const VkPushConstantRange pushConstants{
        .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(DrawPushConstants),
    };
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushConstants,
    };
    VkPipelineLayout layout = VK_NULL_HANDLE;
    DEVEX_VK_TRY(vkCreatePipelineLayout, device, &layoutInfo, nullptr, &layout);

    const std::array stages{
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = shaderModule.handle(),
            .pName = "vertexMain",
        },
        VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = shaderModule.handle(),
            .pName = "fragmentMain",
        },
    };

    // Vertices are pulled from buffers in the shader.
    const VkPipelineVertexInputStateCreateInfo vertexInput{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
    };
    const VkPipelineInputAssemblyStateCreateInfo inputAssembly{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    };
    const VkPipelineViewportStateCreateInfo viewport{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterization{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisample{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    // Reversed depth: nearer surfaces have greater depth values.
    const VkPipelineDepthStencilStateCreateInfo depthStencil{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL,
    };
    const VkPipelineColorBlendAttachmentState colorAttachment{
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
    const VkPipelineColorBlendStateCreateInfo colorBlend{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorAttachment,
    };
    const std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };
    const VkPipelineRenderingCreateInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &config.colorFormat,
        .depthAttachmentFormat = config.depthFormat,
    };

    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = static_cast<std::uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewport,
        .pRasterizationState = &rasterization,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = layout,
    };
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (const VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo,
                                                          nullptr, &pipeline);
        result < VK_SUCCESS)
    {
        vkDestroyPipelineLayout(device, layout, nullptr);
        return vulkanError("vkCreateGraphicsPipelines", result);
    }
    return GraphicsPipeline(device, layout, pipeline);
}

} // namespace devex::render::vulkan
