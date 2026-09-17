#include "EnvironmentBaker.hpp"

#include "Commands.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>

namespace devex::render::vulkan {
namespace {

constexpr std::uint32_t equirectangularBinding = 0;
constexpr std::uint32_t radianceBinding = 1;
constexpr std::uint32_t facesBinding = 2;
constexpr std::uint32_t tableBinding = 3;

constexpr std::uint32_t prefilterSamples = 256;
constexpr std::uint32_t irradianceSamples = 512;
constexpr std::uint32_t brdfSamples = 1024;

[[nodiscard]] std::uint32_t groupCount(std::uint32_t size) noexcept
{
    return (size + 7) / 8;
}

} // namespace

core::Result<std::unique_ptr<EnvironmentBaker>> EnvironmentBaker::create(
    const Device& device, const Allocator& allocator, UploadContext& upload,
    const std::filesystem::path& shaderDirectory)
{
    std::unique_ptr<EnvironmentBaker> baker(new EnvironmentBaker(device, allocator, upload));
    const VkDevice handle = device.handle();

    const VkSamplerCreateInfo equirectangularSamplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE,
    };
    DEVEX_VK_TRY(vkCreateSampler, handle, &equirectangularSamplerInfo, nullptr,
                 &baker->m_equirectangularSampler);
    VkSamplerCreateInfo cubeSamplerInfo = equirectangularSamplerInfo;
    cubeSamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    DEVEX_VK_TRY(vkCreateSampler, handle, &cubeSamplerInfo, nullptr, &baker->m_cubeSampler);

    const std::array bindings{
        VkDescriptorSetLayoutBinding{.binding = equirectangularBinding,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        VkDescriptorSetLayoutBinding{.binding = radianceBinding,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        VkDescriptorSetLayoutBinding{.binding = facesBinding,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        VkDescriptorSetLayoutBinding{.binding = tableBinding,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
        VkDescriptorSetLayoutBinding{.binding = 4,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                     .pImmutableSamplers = &baker->m_equirectangularSampler},
        VkDescriptorSetLayoutBinding{.binding = 5,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                                     .descriptorCount = 1,
                                     .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                                     .pImmutableSamplers = &baker->m_cubeSampler},
    };
    // Each entry point uses only some of the images.
    const std::array<VkDescriptorBindingFlags, 6> flags{
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT,
        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT, 0, 0,
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
    DEVEX_VK_TRY(vkCreateDescriptorSetLayout, handle, &layoutInfo, nullptr, &baker->m_layout);

    const std::array poolSizes{
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, .descriptorCount = 2},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 2},
        VkDescriptorPoolSize{.type = VK_DESCRIPTOR_TYPE_SAMPLER, .descriptorCount = 2},
    };
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = static_cast<std::uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
    };
    DEVEX_VK_TRY(vkCreateDescriptorPool, handle, &poolInfo, nullptr, &baker->m_pool);
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = baker->m_pool,
        .descriptorSetCount = 1,
        .pSetLayouts = &baker->m_layout,
    };
    DEVEX_VK_TRY(vkAllocateDescriptorSets, handle, &allocateInfo, &baker->m_set);

    const std::array setLayouts{baker->m_layout};
    const auto createPipeline = [&](const char* entry, std::optional<Pipeline>& pipeline) -> core::Result<void> {
        core::Result<Pipeline> created = createComputePipeline(handle, {
                                                                           .shaderPath = shaderDirectory / "ibl.spv",
                                                                           .entry = entry,
                                                                           .setLayouts = setLayouts,
                                                                           .pushConstantSize = sizeof(BakePushConstants),
                                                                       });
        if (!created)
        {
            return std::unexpected(created.error());
        }
        pipeline = std::move(*created);
        return {};
    };
    for (const auto& [entry, pipeline] :
         {std::pair<const char*, std::optional<Pipeline>*>{"equirectangularToCube", &baker->m_toCube},
          {"prefilterSpecular", &baker->m_prefilter},
          {"irradiance", &baker->m_irradiance},
          {"brdfTable", &baker->m_brdf}})
    {
        if (core::Result<void> created = createPipeline(entry, *pipeline); !created)
        {
            return std::unexpected(created.error());
        }
    }

    // Placeholders stay in the general layout, valid for both sampled and storage descriptors.
    const ImageConfig placeholderConfig{
        .format = format,
        .extent = {1, 1},
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    };
    core::Result<Image> placeholder = Image::create(device, allocator, placeholderConfig);
    ImageConfig cubeConfig = placeholderConfig;
    cubeConfig.layers = 6;
    cubeConfig.cube = true;
    core::Result<Image> placeholderCube = Image::create(device, allocator, cubeConfig);
    if (!placeholder || !placeholderCube)
    {
        return std::unexpected(!placeholder ? placeholder.error() : placeholderCube.error());
    }
    baker->m_placeholder = std::move(*placeholder);
    baker->m_placeholderCube = std::move(*placeholderCube);
    const std::array placeholders{baker->m_placeholder->handle(), baker->m_placeholderCube->handle()};
    if (core::Result<void> ready = upload.submit([&](VkCommandBuffer commands) {
            for (const VkImage image : placeholders)
            {
                transitionImage(commands, image, ImageState::Undefined, ImageState::ComputeStorage);
            }
        });
        !ready)
    {
        return std::unexpected(ready.error());
    }
    return baker;
}

EnvironmentBaker::EnvironmentBaker(const Device& device, const Allocator& allocator,
                                   UploadContext& upload) noexcept
    : m_device(device)
    , m_allocator(allocator)
    , m_upload(upload)
{
}

EnvironmentBaker::~EnvironmentBaker()
{
    const VkDevice device = m_device.handle();
    m_toCube.reset();
    m_prefilter.reset();
    m_irradiance.reset();
    m_brdf.reset();
    vkDestroyDescriptorPool(device, m_pool, nullptr);
    vkDestroyDescriptorSetLayout(device, m_layout, nullptr);
    vkDestroySampler(device, m_equirectangularSampler, nullptr);
    vkDestroySampler(device, m_cubeSampler, nullptr);
}

void EnvironmentBaker::writeSet(VkImageView equirectangular, VkImageView radiance,
                                VkImageView faces, VkImageView table) noexcept
{
    std::array<VkDescriptorImageInfo, 4> images{};
    std::array<VkWriteDescriptorSet, 4> writes{};
    std::uint32_t count = 0;
    const auto add = [&](std::uint32_t binding, VkImageView view, VkDescriptorType type,
                         VkImageLayout layout) {
        if (view == VK_NULL_HANDLE)
        {
            const bool cube = binding == radianceBinding || binding == facesBinding;
            const Image& placeholder = cube ? *m_placeholderCube : *m_placeholder;
            view = binding == facesBinding
                       ? placeholder.subview(VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 6)
                       : placeholder.view();
            layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        images[count] = {.imageView = view, .imageLayout = layout};
        writes[count] = {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_set,
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = &images[count],
        };
        ++count;
    };
    add(equirectangularBinding, equirectangular, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    add(radianceBinding, radiance, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    add(facesBinding, faces, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    add(tableBinding, table, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_IMAGE_LAYOUT_GENERAL);
    vkUpdateDescriptorSets(m_device.handle(), count, writes.data(), 0, nullptr);
}

core::Result<void> EnvironmentBaker::dispatch(const Pipeline& pipeline,
                                              const BakePushConstants& constants,
                                              std::uint32_t layers)
{
    return m_upload.submit([&](VkCommandBuffer commands) {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 1,
                                &m_set, 0, nullptr);
        vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                           sizeof(constants), &constants);
        vkCmdDispatch(commands, groupCount(constants.size), groupCount(constants.size), layers);
    });
}

core::Result<Image> EnvironmentBaker::bakeBrdfTable()
{
    core::Result<Image> table =
        Image::create(m_device, m_allocator,
                      {
                          .format = format,
                          .extent = {brdfTableSize, brdfTableSize},
                          .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                      });
    if (!table)
    {
        return table;
    }
    const VkImage handle = table->handle();
    if (core::Result<void> ready = m_upload.submit([handle](VkCommandBuffer commands) {
            transitionImage(commands, handle, ImageState::Undefined, ImageState::ComputeStorage);
        });
        !ready)
    {
        return std::unexpected(ready.error());
    }

    writeSet(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, table->view());
    if (core::Result<void> baked = dispatch(*m_brdf, {.size = brdfTableSize, .sampleCount = brdfSamples}, 1);
        !baked)
    {
        return std::unexpected(baked.error());
    }

    if (core::Result<void> done = m_upload.submit([handle](VkCommandBuffer commands) {
            transitionImage(commands, handle, ImageState::ComputeStorage, ImageState::ShaderReadOnly);
        });
        !done)
    {
        return std::unexpected(done.error());
    }
    return table;
}

core::Result<EnvironmentMaps> EnvironmentBaker::bake(VkImageView equirectangular,
                                                     std::uint32_t equirectangularWidth)
{
    const std::uint32_t radianceMips = std::bit_width(radianceSize);
    const auto cube = [&](std::uint32_t size, std::uint32_t mips) {
        return Image::create(m_device, m_allocator,
                             {
                                 .format = format,
                                 .extent = {size, size},
                                 .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                                 .mipLevels = mips,
                                 .layers = 6,
                                 .cube = true,
                             });
    };
    core::Result<Image> radiance = cube(radianceSize, radianceMips);
    core::Result<Image> specular = cube(specularSize, specularMipCount);
    core::Result<Image> irradiance = cube(irradianceSize, 1);
    if (!radiance || !specular || !irradiance)
    {
        return std::unexpected(!radiance ? radiance.error()
                                         : !specular ? specular.error() : irradiance.error());
    }

    const std::array outputs{radiance->handle(), specular->handle(), irradiance->handle()};
    if (core::Result<void> ready = m_upload.submit([&](VkCommandBuffer commands) {
            for (const VkImage image : outputs)
            {
                transitionImage(commands, image, ImageState::Undefined, ImageState::ComputeStorage);
            }
        });
        !ready)
    {
        return std::unexpected(ready.error());
    }

    // Every level of the radiance cube samples the equirectangular map at a matching detail.
    for (std::uint32_t mip = 0; mip < radianceMips; ++mip)
    {
        const std::uint32_t size = std::max(radianceSize >> mip, 1u);
        writeSet(equirectangular, VK_NULL_HANDLE,
                 radiance->subview(VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1, 0, 6), VK_NULL_HANDLE);
        const float sourceLod =
            std::max(std::log2(static_cast<float>(equirectangularWidth) / (4.0f * static_cast<float>(size))), 0.0f);
        if (core::Result<void> baked = dispatch(*m_toCube, {.size = size, .sourceLod = sourceLod}, 6); !baked)
        {
            return std::unexpected(baked.error());
        }
    }

    const VkImage radianceHandle = radiance->handle();
    if (core::Result<void> readable = m_upload.submit([radianceHandle](VkCommandBuffer commands) {
            transitionImage(commands, radianceHandle, ImageState::ComputeStorage, ImageState::ComputeReadOnly);
        });
        !readable)
    {
        return std::unexpected(readable.error());
    }

    for (std::uint32_t mip = 0; mip < specularMipCount; ++mip)
    {
        writeSet(VK_NULL_HANDLE, radiance->view(),
                 specular->subview(VK_IMAGE_VIEW_TYPE_2D_ARRAY, mip, 1, 0, 6), VK_NULL_HANDLE);
        const BakePushConstants constants{
            .size = std::max(specularSize >> mip, 1u),
            .roughness = static_cast<float>(mip) / static_cast<float>(specularMipCount - 1),
            .sampleCount = prefilterSamples,
            .sourceSize = static_cast<float>(radianceSize),
        };
        if (core::Result<void> baked = dispatch(*m_prefilter, constants, 6); !baked)
        {
            return std::unexpected(baked.error());
        }
    }

    writeSet(VK_NULL_HANDLE, radiance->view(), irradiance->subview(VK_IMAGE_VIEW_TYPE_2D_ARRAY, 0, 1, 0, 6),
             VK_NULL_HANDLE);
    if (core::Result<void> baked = dispatch(*m_irradiance,
                                            {.size = irradianceSize,
                                             .sampleCount = irradianceSamples,
                                             .sourceSize = static_cast<float>(radianceSize)},
                                            6);
        !baked)
    {
        return std::unexpected(baked.error());
    }

    const std::array results{specular->handle(), irradiance->handle()};
    if (core::Result<void> done = m_upload.submit([&](VkCommandBuffer commands) {
            for (const VkImage image : results)
            {
                transitionImage(commands, image, ImageState::ComputeStorage, ImageState::ShaderReadOnly);
            }
        });
        !done)
    {
        return std::unexpected(done.error());
    }
    return EnvironmentMaps{.specular = std::move(*specular), .irradiance = std::move(*irradiance)};
}

} // namespace devex::render::vulkan
