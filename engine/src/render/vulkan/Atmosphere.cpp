#include "Atmosphere.hpp"

#include "Commands.hpp"
#include "GpuData.hpp"

#include <array>
#include <utility>

namespace devex::render::vulkan {

namespace {

constexpr VkFormat tableFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
constexpr std::uint32_t transmittanceWidth = 256;
constexpr std::uint32_t transmittanceHeight = 64;
constexpr std::uint32_t multiScatteringSize = 32;
constexpr std::uint32_t skyViewWidth = 192;
constexpr std::uint32_t skyViewHeight = 108;
constexpr std::uint32_t aerialSize = 32;
constexpr std::uint32_t setCount = 6 + AtmosphereRenderer::cubeMipCount;

struct AtmospherePushConstants
{
    VkDeviceAddress air = 0;
    VkDeviceAddress scene = 0;
    std::uint32_t size = 0;
    float roughness = 0.0f;
    std::uint32_t sampleCount = 0;
    std::uint32_t padding = 0;
};

std::uint32_t groups(std::uint32_t size)
{
    return (size + 7) / 8;
}

// Writes of a pass waited for by the passes that read them, on the GPU and in the frame.
void barrier(VkCommandBuffer commands, VkPipelineStageFlags2 sourceStages, VkAccessFlags2 sourceAccess, VkPipelineStageFlags2 destinationStages,
             VkAccessFlags2 destinationAccess)
{
    const VkMemoryBarrier2 memory{
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = sourceStages,
        .srcAccessMask = sourceAccess,
        .dstStageMask = destinationStages,
        .dstAccessMask = destinationAccess,
    };
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &memory,
    };
    vkCmdPipelineBarrier2(commands, &dependency);
}

void computeToCompute(VkCommandBuffer commands)
{
    barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
}

} // namespace

core::Result<std::unique_ptr<AtmosphereRenderer>> AtmosphereRenderer::create(const Device& device, const Allocator& allocator,
                                                                             UploadContext& upload, const DescriptorSets& descriptors,
                                                                             const std::filesystem::path& shaderDirectory)
{
    std::unique_ptr<AtmosphereRenderer> atmosphere(new AtmosphereRenderer(device));
    const VkDevice handle = device.handle();

    // What a pass writes, as a table or as layers, and what it reads besides the global set; a pass
    // binds only what it uses.
    const auto binding = [](std::uint32_t index, VkDescriptorType type) {
        return VkDescriptorSetLayoutBinding{
            .binding = index,
            .descriptorType = type,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    };
    const std::array bindings{
        binding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE),
        binding(2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
        binding(3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE),
    };
    const std::array<VkDescriptorBindingFlags, 4> flags{
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
    };
    const VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = static_cast<std::uint32_t>(flags.size()),
        .pBindingFlags = flags.data(),
    };
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .pNext = &flagsInfo,
        .bindingCount = static_cast<std::uint32_t>(bindings.size()),
        .pBindings = bindings.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorSetLayout, handle, &layoutInfo, nullptr, &atmosphere->m_layout);
    const std::array poolSizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 2 * setCount},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 2 * setCount},
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = setCount,
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorPool, handle, &poolInfo, nullptr, &atmosphere->m_pool);

    // The global set for the tables the shaders share with the frames, the frame set unused, and
    // the atmosphere's own.
    const std::array setLayouts{descriptors.globalLayout(), descriptors.frameLayout(), atmosphere->m_layout};
    for (const auto& [entry, pipeline] : {std::pair<const char*, std::optional<Pipeline>*>{"transmittanceTable", &atmosphere->m_transmittance},
                                          {"multiScatteringTableMain", &atmosphere->m_multiScattering},
                                          {"skyViewTableMain", &atmosphere->m_skyView},
                                          {"aerialPerspectiveTable", &atmosphere->m_aerialPerspective},
                                          {"skyCube", &atmosphere->m_skyCube},
                                          {"prefilterSky", &atmosphere->m_prefilter},
                                          {"irradianceSky", &atmosphere->m_irradiance}})
    {
        core::Result<Pipeline> created = createComputePipeline(handle, {
                                                                           .shaderPath = shaderDirectory / "atmosphere.spv",
                                                                           .entry = entry,
                                                                           .setLayouts = setLayouts,
                                                                           .pushConstantSize = sizeof(AtmospherePushConstants),
                                                                       });
        if (!created)
        {
            return std::unexpected(created.error());
        }
        *pipeline = std::move(*created);
    }

    const auto image = [&](std::optional<Image>& target, const ImageConfig& config) -> core::Result<void> {
        core::Result<Image> created = Image::create(device, allocator, config);
        if (!created)
        {
            return std::unexpected(created.error());
        }
        target = std::move(*created);
        return {};
    };
    constexpr VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    for (core::Result<void> made :
         {image(atmosphere->m_transmittanceTable, {.format = tableFormat, .extent = {transmittanceWidth, transmittanceHeight}, .usage = usage}),
          image(atmosphere->m_multiScatteringTable, {.format = tableFormat, .extent = {multiScatteringSize, multiScatteringSize}, .usage = usage}),
          image(atmosphere->m_skyViewTable, {.format = tableFormat, .extent = {skyViewWidth, skyViewHeight}, .usage = usage}),
          image(atmosphere->m_aerialTable, {.format = tableFormat, .extent = {aerialSize, aerialSize}, .usage = usage, .layers = aerialSize}),
          image(atmosphere->m_specularCube,
                {.format = tableFormat, .extent = {cubeSize, cubeSize}, .usage = usage, .mipLevels = cubeMipCount, .layers = 6, .cube = true}),
          image(atmosphere->m_irradianceCube,
                {.format = tableFormat, .extent = {irradianceSize, irradianceSize}, .usage = usage, .layers = 6, .cube = true})})
    {
        if (!made)
        {
            return std::unexpected(made.error());
        }
    }
    // Black until a frame with an atmosphere fills them, and in the general layout from now on.
    const std::array images{atmosphere->m_transmittanceTable->handle(), atmosphere->m_multiScatteringTable->handle(),
                            atmosphere->m_skyViewTable->handle(),       atmosphere->m_aerialTable->handle(),
                            atmosphere->m_specularCube->handle(),       atmosphere->m_irradianceCube->handle()};
    if (core::Result<void> ready = upload.submit([&](VkCommandBuffer commands) {
            for (const VkImage each : images)
            {
                transitionImage(commands, each, ImageState::Undefined, ImageState::TransferDestination);
                const VkClearColorValue black{};
                const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                    .levelCount = VK_REMAINING_MIP_LEVELS,
                                                    .layerCount = VK_REMAINING_ARRAY_LAYERS};
                vkCmdClearColorImage(commands, each, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
                transitionImage(commands, each, ImageState::TransferDestination, ImageState::ComputeStorage);
            }
        });
        !ready)
    {
        return std::unexpected(ready.error());
    }

    AtmosphereRenderer& made = *atmosphere;
    const VkImageView multiScattering = made.m_multiScatteringTable->view();
    const VkImageView skyCube = made.m_specularCube->subview(VK_IMAGE_VIEW_TYPE_CUBE, 0, 1, 0, 6);
    made.m_transmittanceSet = made.makeSet(made.m_transmittanceTable->view(), VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE);
    made.m_multiScatteringSet = made.makeSet(multiScattering, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE);
    made.m_skyViewSet = made.makeSet(made.m_skyViewTable->view(), VK_NULL_HANDLE, multiScattering, VK_NULL_HANDLE);
    made.m_aerialSet = made.makeSet(VK_NULL_HANDLE, made.m_aerialTable->view(), multiScattering, VK_NULL_HANDLE);
    for (std::uint32_t mip = 0; mip < cubeMipCount; ++mip)
    {
        made.m_cubeSets[mip] = made.makeSet(VK_NULL_HANDLE, made.m_specularCube->subview(VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1, 0, 6),
                                            VK_NULL_HANDLE, mip > 0 ? skyCube : VK_NULL_HANDLE);
    }
    made.m_irradianceSet = made.makeSet(VK_NULL_HANDLE, made.m_irradianceCube->subview(VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 6),
                                        VK_NULL_HANDLE, skyCube);
    for (const VkDescriptorSet set : {made.m_transmittanceSet, made.m_multiScatteringSet, made.m_skyViewSet, made.m_aerialSet, made.m_irradianceSet})
    {
        if (set == VK_NULL_HANDLE)
        {
            return core::makeError(core::ErrorCode::Graphics, "the descriptor sets of the atmosphere cannot be allocated");
        }
    }
    return atmosphere;
}

AtmosphereRenderer::AtmosphereRenderer(const Device& device) noexcept
    : m_device(device)
{
}

AtmosphereRenderer::~AtmosphereRenderer()
{
    const VkDevice device = m_device.handle();
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
}

VkDescriptorSet AtmosphereRenderer::makeSet(VkImageView table, VkImageView layers, VkImageView multiScattering, VkImageView cube)
{
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &m_layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_device.handle(), &allocateInfo, &set) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }
    const std::array<std::pair<VkImageView, VkDescriptorType>, 4> views{{
        {table, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
        {layers, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
        {multiScattering, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
        {cube, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
    }};
    for (std::uint32_t index = 0; index < views.size(); ++index)
    {
        if (views[index].first == VK_NULL_HANDLE)
        {
            continue;
        }
        const VkDescriptorImageInfo imageInfo{.imageView = views[index].first, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = index,
            .descriptorCount = 1,
            .descriptorType = views[index].second,
            .pImageInfo = &imageInfo,
        };
        vkUpdateDescriptorSets(m_device.handle(), 1, &write, 0, nullptr);
    }
    return set;
}

void AtmosphereRenderer::record(VkCommandBuffer commands, VkDescriptorSet global, VkDeviceAddress air, VkDeviceAddress scene,
                                std::uint64_t airKey, std::uint64_t skyKey, std::uint64_t viewKey)
{
    if (m_airKey == airKey && m_skyKey == skyKey && m_viewKey == viewKey)
    {
        return;
    }
    // The frames before may still sample what this one writes.
    barrier(commands, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
    const auto dispatch = [&](const Pipeline& pipeline, VkDescriptorSet set, const AtmospherePushConstants& constants, std::uint32_t x,
                              std::uint32_t y, std::uint32_t z) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 1, &global, 0, nullptr);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 2, 1, &set, 0, nullptr);
        vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
        vkCmdDispatch(commands, x, y, z);
    };
    const AtmospherePushConstants constants{.air = air, .scene = scene};

    // The air changed: what it lets through, then the light it scatters many times, which reads it.
    if (m_airKey != airKey)
    {
        dispatch(*m_transmittance, m_transmittanceSet, constants, groups(transmittanceWidth), groups(transmittanceHeight), 1);
        computeToCompute(commands);
        dispatch(*m_multiScattering, m_multiScatteringSet, constants, groups(multiScatteringSize), groups(multiScatteringSize), 1);
        computeToCompute(commands);
        m_airKey = airKey;
        m_skyKey.reset();
        m_viewKey.reset();
    }
    // The sky and the haze around the camera, which follow it and the sun.
    if (m_viewKey != viewKey)
    {
        dispatch(*m_skyView, m_skyViewSet, constants, groups(skyViewWidth), groups(skyViewHeight), 1);
        dispatch(*m_aerialPerspective, m_aerialSet, constants, groups(aerialSize), groups(aerialSize), aerialSize);
        m_viewKey = viewKey;
    }
    // The light the sky gives the scene, once it changed enough to show.
    if (m_skyKey != skyKey)
    {
        computeToCompute(commands);
        AtmospherePushConstants cube = constants;
        cube.size = cubeSize;
        dispatch(*m_skyCube, m_cubeSets[0], cube, groups(cubeSize), groups(cubeSize), 6);
        computeToCompute(commands);
        for (std::uint32_t mip = 1; mip < cubeMipCount; ++mip)
        {
            AtmospherePushConstants level = constants;
            level.size = cubeSize >> mip;
            level.roughness = static_cast<float>(mip) / static_cast<float>(cubeMipCount - 1);
            level.sampleCount = 64;
            dispatch(*m_prefilter, m_cubeSets[mip], level, groups(level.size), groups(level.size), 6);
        }
        AtmospherePushConstants irradiance = constants;
        irradiance.size = irradianceSize;
        irradiance.sampleCount = 128;
        dispatch(*m_irradiance, m_irradianceSet, irradiance, groups(irradianceSize), groups(irradianceSize), 6);
        m_skyKey = skyKey;
    }
    // Sampled by the surfaces, the sky and the passes of the next frames.
    barrier(commands, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
}

VkImageView AtmosphereRenderer::transmittanceView() const noexcept
{
    return m_transmittanceTable->view();
}

VkImageView AtmosphereRenderer::skyViewView() const noexcept
{
    return m_skyViewTable->view();
}

VkImageView AtmosphereRenderer::aerialPerspectiveView() const noexcept
{
    return m_aerialTable->view();
}

VkImageView AtmosphereRenderer::specularView() const noexcept
{
    return m_specularCube->view();
}

VkImageView AtmosphereRenderer::irradianceView() const noexcept
{
    return m_irradianceCube->view();
}

} // namespace devex::render::vulkan
