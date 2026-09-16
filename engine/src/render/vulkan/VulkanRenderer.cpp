#include "VulkanRenderer.hpp"

#include "Commands.hpp"
#include "GpuData.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <limits>
#include <utility>

namespace devex::render::vulkan {
namespace {

// volk loads device functions globally, so a second device would replace those of the first.
std::atomic<bool> rendererExists{false};

constexpr std::uint64_t noTimeout = std::numeric_limits<std::uint64_t>::max();

[[nodiscard]] VkFormat toVulkanFormat(asset::TextureFormat format) noexcept
{
    switch (format)
    {
    case asset::TextureFormat::Rgba8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case asset::TextureFormat::Rgba8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case asset::TextureFormat::Bc5Unorm:
        return VK_FORMAT_BC5_UNORM_BLOCK;
    case asset::TextureFormat::Bc7Unorm:
        return VK_FORMAT_BC7_UNORM_BLOCK;
    case asset::TextureFormat::Bc7Srgb:
        return VK_FORMAT_BC7_SRGB_BLOCK;
    }
    return VK_FORMAT_UNDEFINED;
}

// A 1x1 uncompressed texture of one color.
[[nodiscard]] asset::TextureData solidTexture(std::array<std::uint8_t, 4> rgba)
{
    asset::TextureData texture{.format = asset::TextureFormat::Rgba8Unorm};
    asset::TextureMip& mip = texture.mips.emplace_back();
    mip.width = 1;
    mip.height = 1;
    for (const std::uint8_t channel : rgba)
    {
        mip.bytes.push_back(static_cast<std::byte>(channel));
    }
    return texture;
}

} // namespace

core::Result<std::unique_ptr<VulkanRenderer>> VulkanRenderer::create(
    const platform::Platform& platform, platform::Window& window, const RendererConfig& config)
{
    if (rendererExists.load())
    {
        return core::makeError(core::ErrorCode::InvalidState, "a Renderer already exists");
    }

    const core::Result<std::span<const char* const>> extensions =
        platform.vulkanInstanceExtensions();
    if (!extensions)
    {
        return std::unexpected(extensions.error());
    }

    core::Result<Instance> instance = Instance::create({
        .applicationName = config.applicationName,
        .requiredExtensions = *extensions,
        .validation = config.validation,
    });
    if (!instance)
    {
        return std::unexpected(instance.error());
    }

    const core::Result<std::uint64_t> surfaceHandle =
        window.createVulkanSurface(instance->handle());
    if (!surfaceHandle)
    {
        return std::unexpected(surfaceHandle.error());
    }
    Surface surface(instance->handle(), std::bit_cast<VkSurfaceKHR>(*surfaceHandle));

    core::Result<Device> device =
        Device::create(instance->handle(), surface.handle(), config.preferredGpu);
    if (!device)
    {
        return std::unexpected(device.error());
    }

    core::Result<Allocator> allocator = Allocator::create(*instance, *device);
    if (!allocator)
    {
        return std::unexpected(allocator.error());
    }

    core::Result<UploadContext> upload = UploadContext::create(*device);
    if (!upload)
    {
        return std::unexpected(upload.error());
    }

    std::filesystem::path shaderDirectory = config.shaderDirectory.empty()
                                                ? platform.baseDirectory() / "shaders"
                                                : config.shaderDirectory;

    std::unique_ptr<VulkanRenderer> renderer(new VulkanRenderer(
        window, config, std::move(shaderDirectory), std::move(*instance), std::move(surface),
        std::move(*device), std::move(*allocator), std::move(*upload)));

    const GpuInfo& gpu = renderer->gpu();
    DEVEX_LOG_INFO("GPU: {} ({}, Vulkan {}, {} {})", gpu.name, toString(gpu.type), gpu.apiVersion,
                   gpu.driverName, gpu.driverVersion);

    if (core::Result<void> contexts = renderer->createFrameContexts(); !contexts)
    {
        return std::unexpected(contexts.error());
    }
    if (core::Result<void> defaults = renderer->createDefaultResources(); !defaults)
    {
        return std::unexpected(defaults.error());
    }
    if (const math::Extent2D pixelSize = window.pixelSize();
        pixelSize.width > 0 && pixelSize.height > 0)
    {
        if (core::Result<void> swapchain = renderer->recreateSwapchain(pixelSize); !swapchain)
        {
            return std::unexpected(swapchain.error());
        }
    }
    return renderer;
}

VulkanRenderer::VulkanRenderer(platform::Window& window, const RendererConfig& config,
                               std::filesystem::path shaderDirectory, Instance instance,
                               Surface surface, Device device, Allocator allocator,
                               UploadContext upload) noexcept
    : m_window(window)
    , m_requestedPresentMode(config.presentMode)
    , m_shaderDirectory(std::move(shaderDirectory))
    , m_instance(std::move(instance))
    , m_surface(std::move(surface))
    , m_device(std::move(device))
    , m_allocator(std::move(allocator))
    , m_upload(std::move(upload))
{
    rendererExists.store(true);
}

VulkanRenderer::~VulkanRenderer()
{
    DEVEX_ASSERT_MSG(!m_imguiInitialized, "call shutdownImGui before destroying the renderer");
    const VkDevice device = m_device.handle();
    if (device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(device);
        destroyPresentSemaphores();
        for (FrameContext& frame : m_frames)
        {
            vkDestroySemaphore(device, frame.imageAcquired, nullptr);
            vkDestroyFence(device, frame.completed, nullptr);
            vkDestroyCommandPool(device, frame.commandPool, nullptr);
        }
    }
    rendererExists.store(false);
}

const GpuInfo& VulkanRenderer::gpu() const noexcept
{
    return m_device.gpu();
}

PresentMode VulkanRenderer::presentMode() const noexcept
{
    return m_swapchain ? m_swapchain->presentMode() : m_requestedPresentMode;
}

core::Result<MeshHandle> VulkanRenderer::createMesh(const asset::MeshData& mesh)
{
    if (core::Result<void> valid = asset::validate(mesh); !valid)
    {
        return std::unexpected(valid.error());
    }

    std::vector<GpuVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const asset::Vertex& vertex : mesh.vertices)
    {
        vertices.push_back({vertex.position, vertex.uv.x, vertex.normal, vertex.uv.y});
    }
    const VkDeviceSize vertexBytes = vertices.size() * sizeof(GpuVertex);
    const VkDeviceSize indexBytes = mesh.indices.size() * sizeof(std::uint32_t);

    core::Result<Buffer> vertexBuffer = Buffer::create(
        m_allocator, {
                         .size = vertexBytes,
                         .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                  VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     });
    if (!vertexBuffer)
    {
        return std::unexpected(vertexBuffer.error());
    }
    core::Result<Buffer> indexBuffer = Buffer::create(
        m_allocator, {
                         .size = indexBytes,
                         .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     });
    if (!indexBuffer)
    {
        return std::unexpected(indexBuffer.error());
    }
    core::Result<Buffer> staging = Buffer::create(m_allocator, {
                                                                   .size = vertexBytes + indexBytes,
                                                                   .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                                   .hostVisible = true,
                                                               });
    if (!staging)
    {
        return std::unexpected(staging.error());
    }

    const std::span<std::byte> stagingBytes = staging->mappedBytes();
    std::memcpy(stagingBytes.data(), vertices.data(), vertexBytes);
    std::memcpy(stagingBytes.data() + vertexBytes, mesh.indices.data(), indexBytes);

    const core::Result<void> uploaded = m_upload.submit([&](VkCommandBuffer commandBuffer) {
        const VkBufferCopy vertexCopy{.srcOffset = 0, .dstOffset = 0, .size = vertexBytes};
        vkCmdCopyBuffer(commandBuffer, staging->handle(), vertexBuffer->handle(), 1, &vertexCopy);
        const VkBufferCopy indexCopy{.srcOffset = vertexBytes, .dstOffset = 0, .size = indexBytes};
        vkCmdCopyBuffer(commandBuffer, staging->handle(), indexBuffer->handle(), 1, &indexCopy);
    });
    if (!uploaded)
    {
        return std::unexpected(uploaded.error());
    }

    GpuMesh gpuMesh{
        .vertices = std::move(*vertexBuffer),
        .indices = std::move(*indexBuffer),
    };
    for (const asset::Submesh& submesh : asset::submeshesOf(mesh))
    {
        gpuMesh.submeshes.push_back({submesh.firstIndex, submesh.indexCount});
    }
    return m_meshes.insert(std::move(gpuMesh));
}

void VulkanRenderer::destroyMesh(MeshHandle mesh)
{
    if (std::optional<GpuMesh> removed = m_meshes.remove(mesh))
    {
        m_retiredMeshes.push_back({.mesh = std::move(*removed), .retiredAtFrame = m_frameIndex});
    }
}

std::uint32_t VulkanRenderer::submeshCount(MeshHandle mesh) const noexcept
{
    const GpuMesh* const found = m_meshes.find(mesh);
    return found != nullptr ? static_cast<std::uint32_t>(found->submeshes.size()) : 0;
}

core::Result<TextureHandle> VulkanRenderer::createTexture(const asset::TextureData& texture)
{
    std::uint32_t slot = 0;
    if (!m_freeTextureSlots.empty())
    {
        slot = m_freeTextureSlots.back();
    }
    else if (m_nextTextureSlot < m_bindless->textureCapacity())
    {
        slot = m_nextTextureSlot;
    }
    else
    {
        return core::makeError(core::ErrorCode::OutOfMemory, "all {} texture slots are in use",
                               m_bindless->textureCapacity());
    }

    core::Result<GpuTexture> uploaded = uploadTexture(texture, slot);
    if (!uploaded)
    {
        return std::unexpected(uploaded.error());
    }
    if (!m_freeTextureSlots.empty())
    {
        m_freeTextureSlots.pop_back();
    }
    else
    {
        ++m_nextTextureSlot;
    }
    // Materials referring to a destroyed handle never refer to this one, but a new texture may
    // replace one of theirs: they are resolved again.
    m_materialsChanged = true;
    return m_textures.insert(std::move(*uploaded));
}

void VulkanRenderer::destroyTexture(TextureHandle texture)
{
    if (std::optional<GpuTexture> removed = m_textures.remove(texture))
    {
        m_retiredTextures.push_back({.texture = std::move(*removed), .retiredAtFrame = m_frameIndex});
        m_materialsChanged = true;
    }
}

MaterialHandle VulkanRenderer::createMaterial(const MaterialDesc& material)
{
    m_materialsChanged = true;
    return m_materials.insert(material);
}

void VulkanRenderer::updateMaterial(MaterialHandle handle, const MaterialDesc& material)
{
    if (MaterialDesc* const existing = m_materials.find(handle))
    {
        *existing = material;
        m_materialsChanged = true;
    }
}

void VulkanRenderer::destroyMaterial(MaterialHandle material)
{
    // The default material outlives every user material.
    if (material != m_defaultMaterial && m_materials.remove(material))
    {
        m_materialsChanged = true;
    }
}

RenderWorld& VulkanRenderer::beginFrame() noexcept
{
    m_world.reset();
    return m_world;
}

core::Result<void> VulkanRenderer::endFrame()
{
    // A queued ImGui frame is only valid for this frame, even if it ends up skipped.
    const bool drawImGui = std::exchange(m_imguiDrawQueued, false);
    const math::Extent2D windowPixelSize = m_window.pixelSize();
    if (windowPixelSize.width == 0 || windowPixelSize.height == 0)
    {
        return {};
    }

    if (!m_swapchain || !m_depthImage || !m_meshPipeline || m_swapchainOutdated ||
        windowPixelSize != m_swapchainWindowPixelSize)
    {
        if (core::Result<void> recreated = recreateSwapchain(windowPixelSize); !recreated)
        {
            return recreated;
        }
        if (!m_swapchain || m_swapchainOutdated)
        {
            return {};
        }
    }

    const VkDevice device = m_device.handle();
    FrameContext& frame = m_frames[m_frameIndex % framesInFlight];
    DEVEX_VK_TRY(vkWaitForFences, device, 1, &frame.completed, VK_TRUE, noTimeout);
    releaseRetiredResources();

    std::uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device, m_swapchain->handle(), noTimeout,
                                                    frame.imageAcquired, VK_NULL_HANDLE,
                                                    &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR)
    {
        m_swapchainOutdated = true;
        return {};
    }
    if (acquired < VK_SUCCESS)
    {
        return vulkanError("vkAcquireNextImageKHR", acquired);
    }
    if (acquired == VK_SUBOPTIMAL_KHR)
    {
        m_swapchainOutdated = true;
    }

    DEVEX_VK_TRY(vkResetFences, device, 1, &frame.completed);
    if (core::Result<void> materials = updateFrameMaterials(frame); !materials)
    {
        return materials;
    }
    writeSceneData(frame);
    core::Result<std::uint32_t> recorded = recordFrame(frame, imageIndex, drawImGui);
    if (!recorded)
    {
        return std::unexpected(recorded.error());
    }
    m_lastDrawCalls = *recorded;

    const VkSemaphore presentSemaphore = m_presentSemaphores[imageIndex];
    const VkSemaphoreSubmitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = frame.imageAcquired,
        .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    const VkCommandBufferSubmitInfo commandBufferInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        .commandBuffer = frame.commandBuffer,
    };
    const VkSemaphoreSubmitInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
        .semaphore = presentSemaphore,
        .stageMask = VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT,
    };
    const VkSubmitInfo2 submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
        .waitSemaphoreInfoCount = 1,
        .pWaitSemaphoreInfos = &waitInfo,
        .commandBufferInfoCount = 1,
        .pCommandBufferInfos = &commandBufferInfo,
        .signalSemaphoreInfoCount = 1,
        .pSignalSemaphoreInfos = &signalInfo,
    };
    DEVEX_VK_TRY(vkQueueSubmit2, m_device.queue(), 1, &submitInfo, frame.completed);

    const VkSwapchainKHR swapchain = m_swapchain->handle();
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &presentSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain,
        .pImageIndices = &imageIndex,
    };
    const VkResult presented = vkQueuePresentKHR(m_device.queue(), &presentInfo);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR)
    {
        m_swapchainOutdated = true;
    }
    else if (presented < VK_SUCCESS)
    {
        return vulkanError("vkQueuePresentKHR", presented);
    }

    ++m_frameIndex;
    return {};
}

core::Result<void> VulkanRenderer::createFrameContexts()
{
    const VkDevice device = m_device.handle();
    for (FrameContext& frame : m_frames)
    {
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_device.queueFamily(),
        };
        DEVEX_VK_TRY(vkCreateCommandPool, device, &poolInfo, nullptr, &frame.commandPool);

        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        DEVEX_VK_TRY(vkAllocateCommandBuffers, device, &allocateInfo, &frame.commandBuffer);

        const VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };
        DEVEX_VK_TRY(vkCreateFence, device, &fenceInfo, nullptr, &frame.completed);

        const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        DEVEX_VK_TRY(vkCreateSemaphore, device, &semaphoreInfo, nullptr, &frame.imageAcquired);

        core::Result<Buffer> sceneData =
            Buffer::create(m_allocator, {
                                            .size = sizeof(GpuSceneData),
                                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                            .hostVisible = true,
                                        });
        if (!sceneData)
        {
            return std::unexpected(sceneData.error());
        }
        frame.sceneData = std::move(*sceneData);
    }
    return {};
}

core::Result<void> VulkanRenderer::createDefaultResources()
{
    core::Result<BindlessSet> bindless =
        BindlessSet::create(m_device, std::min(m_device.maxBindlessTextures(), maxTextures));
    if (!bindless)
    {
        return std::unexpected(bindless.error());
    }
    m_bindless = std::move(*bindless);

    core::Result<GpuTexture> white = uploadTexture(solidTexture({255, 255, 255, 255}), whiteTextureSlot);
    if (!white)
    {
        return std::unexpected(white.error());
    }
    m_whiteTexture = std::move(*white);

    // A normal pointing straight out of the surface.
    core::Result<GpuTexture> flatNormal =
        uploadTexture(solidTexture({128, 128, 255, 255}), flatNormalTextureSlot);
    if (!flatNormal)
    {
        return std::unexpected(flatNormal.error());
    }
    m_flatNormalTexture = std::move(*flatNormal);

    m_defaultMaterial = m_materials.insert(MaterialDesc{.baseColorFactor = {0.72f, 0.74f, 0.78f, 1.0f}});
    return {};
}

core::Result<VulkanRenderer::GpuTexture> VulkanRenderer::uploadTexture(
    const asset::TextureData& texture, std::uint32_t slot)
{
    if (core::Result<void> valid = asset::validate(texture); !valid)
    {
        return std::unexpected(valid.error());
    }

    const asset::TextureMip& base = texture.mips.front();
    const auto mipLevels = static_cast<std::uint32_t>(texture.mips.size());
    core::Result<Image> image =
        Image::create(m_device, m_allocator,
                      {
                          .format = toVulkanFormat(texture.format),
                          .extent = {base.width, base.height},
                          .usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                          .mipLevels = mipLevels,
                      });
    if (!image)
    {
        return std::unexpected(image.error());
    }

    VkDeviceSize totalBytes = 0;
    std::vector<VkBufferImageCopy> copies;
    for (std::uint32_t level = 0; level < mipLevels; ++level)
    {
        const asset::TextureMip& mip = texture.mips[level];
        copies.push_back({
            .bufferOffset = totalBytes,
            .imageSubresource =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .mipLevel = level,
                    .layerCount = 1,
                },
            .imageExtent = {mip.width, mip.height, 1},
        });
        totalBytes += mip.bytes.size();
    }

    core::Result<Buffer> staging = Buffer::create(m_allocator, {
                                                                   .size = totalBytes,
                                                                   .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                                                   .hostVisible = true,
                                                               });
    if (!staging)
    {
        return std::unexpected(staging.error());
    }
    const std::span<std::byte> stagingBytes = staging->mappedBytes();
    for (std::uint32_t level = 0; level < mipLevels; ++level)
    {
        const std::vector<std::byte>& bytes = texture.mips[level].bytes;
        std::memcpy(stagingBytes.data() + copies[level].bufferOffset, bytes.data(), bytes.size());
    }

    const VkImage imageHandle = image->handle();
    const core::Result<void> uploaded = m_upload.submit([&](VkCommandBuffer commandBuffer) {
        transitionImage(commandBuffer, imageHandle, ImageState::Undefined,
                        ImageState::TransferDestination, mipLevels);
        vkCmdCopyBufferToImage(commandBuffer, staging->handle(), imageHandle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(copies.size()), copies.data());
        transitionImage(commandBuffer, imageHandle, ImageState::TransferDestination,
                        ImageState::ShaderReadOnly, mipLevels);
    });
    if (!uploaded)
    {
        return std::unexpected(uploaded.error());
    }

    m_bindless->setTexture(slot, image->view());
    return GpuTexture{.image = std::move(*image), .slot = slot};
}

GpuMaterial VulkanRenderer::toGpuMaterial(const MaterialDesc& material) const noexcept
{
    const auto slotOf = [this](TextureHandle texture, std::uint32_t fallback) {
        const GpuTexture* const found = m_textures.find(texture);
        return found != nullptr ? found->slot : fallback;
    };
    return GpuMaterial{
        .baseColorFactor = material.baseColorFactor,
        .emissiveFactor = material.emissiveFactor,
        .alphaCutoff = material.alphaCutoff,
        .baseColorTexture = slotOf(material.baseColorTexture, whiteTextureSlot),
        .metallicRoughnessTexture = slotOf(material.metallicRoughnessTexture, whiteTextureSlot),
        .normalTexture = slotOf(material.normalTexture, flatNormalTextureSlot),
        .occlusionTexture = slotOf(material.occlusionTexture, whiteTextureSlot),
        .emissiveTexture = slotOf(material.emissiveTexture, whiteTextureSlot),
        .metallicFactor = material.metallicFactor,
        .roughnessFactor = material.roughnessFactor,
        .normalScale = material.normalScale,
        .occlusionStrength = material.occlusionStrength,
        .alphaMode = static_cast<std::uint32_t>(material.alphaMode),
        .doubleSided = material.doubleSided ? 1u : 0u,
    };
}

core::Result<void> VulkanRenderer::updateFrameMaterials(FrameContext& frame)
{
    if (m_materialsChanged)
    {
        const GpuMaterial defaultMaterial = toGpuMaterial(*m_materials.find(m_defaultMaterial));
        m_gpuMaterials.assign(m_materials.slotCount(), defaultMaterial);
        m_materials.forEach([this](MaterialHandle handle, const MaterialDesc& material) {
            m_gpuMaterials[handle.index] = toGpuMaterial(material);
        });
        ++m_materialVersion;
        m_materialsChanged = false;
    }
    if (frame.materialVersion == m_materialVersion)
    {
        return {};
    }

    const VkDeviceSize requiredBytes = m_gpuMaterials.size() * sizeof(GpuMaterial);
    if (!frame.materials || frame.materials->size() < requiredBytes)
    {
        // Grows geometrically; the fence of this frame was waited for, so no GPU work reads it.
        const VkDeviceSize capacity =
            std::max<VkDeviceSize>(requiredBytes * 2, 64 * sizeof(GpuMaterial));
        frame.materials.reset();
        core::Result<Buffer> buffer =
            Buffer::create(m_allocator, {
                                            .size = capacity,
                                            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                            .hostVisible = true,
                                        });
        if (!buffer)
        {
            return std::unexpected(buffer.error());
        }
        frame.materials = std::move(*buffer);
    }
    std::memcpy(frame.materials->mappedBytes().data(), m_gpuMaterials.data(), requiredBytes);
    frame.materialVersion = m_materialVersion;
    return {};
}

core::Result<void> VulkanRenderer::recreateSwapchain(math::Extent2D windowPixelSize)
{
    const VkDevice device = m_device.handle();
    DEVEX_VK_TRY(vkDeviceWaitIdle, device);

    core::Result<Swapchain> swapchain =
        Swapchain::create(m_device, m_surface.handle(),
                          {
                              .windowPixelSize = windowPixelSize,
                              .presentMode = m_requestedPresentMode,
                              .previous = m_swapchain ? m_swapchain->handle() : VK_NULL_HANDLE,
                          });
    if (!swapchain)
    {
        // The window can lose its drawable area between the size check and this point: retry on
        // a later frame.
        if (swapchain.error().code == core::ErrorCode::InvalidState)
        {
            m_swapchainOutdated = true;
            return {};
        }
        return std::unexpected(swapchain.error());
    }

    const bool isFirstSwapchain = !m_swapchain.has_value();
    const VkFormat previousFormat = m_swapchain ? m_swapchain->format() : VK_FORMAT_UNDEFINED;
    m_swapchain = std::move(*swapchain);

    destroyPresentSemaphores();
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (std::uint32_t image = 0; image < m_swapchain->imageCount(); ++image)
    {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        DEVEX_VK_TRY(vkCreateSemaphore, device, &semaphoreInfo, nullptr, &semaphore);
        m_presentSemaphores.push_back(semaphore);
    }

    m_depthImage.reset();
    core::Result<Image> depthImage =
        Image::create(m_device, m_allocator,
                      {
                          .format = depthFormat,
                          .extent = m_swapchain->extent(),
                          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                          .aspect = VK_IMAGE_ASPECT_DEPTH_BIT,
                      });
    if (!depthImage)
    {
        return std::unexpected(depthImage.error());
    }
    m_depthImage = std::move(*depthImage);
    // The depth image stays in its attachment layout for its whole life: every frame clears it,
    // so frames in flight never need a barrier on the image they share.
    const VkImage depthHandle = m_depthImage->handle();
    if (core::Result<void> transitioned = m_upload.submit([depthHandle](VkCommandBuffer commands) {
            transitionImage(commands, depthHandle, ImageState::Undefined,
                            ImageState::DepthAttachment);
        });
        !transitioned)
    {
        return transitioned;
    }

    if (!m_meshPipeline || !m_doubleSidedPipeline || previousFormat != m_swapchain->format())
    {
        m_meshPipeline.reset();
        m_doubleSidedPipeline.reset();
        MeshPipelineConfig pipelineConfig{
            .shaderPath = m_shaderDirectory / "mesh.spv",
            .colorFormat = m_swapchain->format(),
            .depthFormat = depthFormat,
            .textureSetLayout = m_bindless->layout(),
        };
        core::Result<GraphicsPipeline> pipeline = createMeshPipeline(device, pipelineConfig);
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        m_meshPipeline = std::move(*pipeline);

        pipelineConfig.cullBackFaces = false;
        core::Result<GraphicsPipeline> doubleSided = createMeshPipeline(device, pipelineConfig);
        if (!doubleSided)
        {
            return std::unexpected(doubleSided.error());
        }
        m_doubleSidedPipeline = std::move(*doubleSided);
    }

    m_swapchainWindowPixelSize = windowPixelSize;
    m_swapchainOutdated = false;

    const math::Extent2D extent = m_swapchain->extent();
    if (isFirstSwapchain)
    {
        DEVEX_LOG_INFO("Swapchain {}x{} with {} images, {} presentation", extent.width,
                       extent.height, m_swapchain->imageCount(),
                       toString(m_swapchain->presentMode()));
        if (m_swapchain->presentMode() != m_requestedPresentMode)
        {
            DEVEX_LOG_WARNING("The display does not support {} presentation, using FIFO",
                              toString(m_requestedPresentMode));
        }
    }
    else
    {
        DEVEX_LOG_TRACE("Swapchain recreated at {}x{}", extent.width, extent.height);
    }
    return {};
}

void VulkanRenderer::writeSceneData(const FrameContext& frame) const noexcept
{
    const math::Extent2D extent = m_swapchain->extent();
    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);

    // Vulkan clip space points Y down, while the engine convention points it up.
    math::Mat4 clipCorrection{1.0f};
    clipCorrection[1][1] = -1.0f;

    const RenderCamera& camera = m_world.camera;
    const float lightLength = math::length(m_world.lightDirection);

    GpuSceneData scene;
    scene.viewProjection =
        clipCorrection *
        math::perspectiveReverseZ(camera.verticalFov, aspectRatio, camera.nearPlane) * camera.view;
    scene.lightDirection = lightLength > 0.0f ? m_world.lightDirection / lightLength
                                              : math::Vec3{0.0f, -1.0f, 0.0f};
    scene.ambient = m_world.ambient;
    scene.materials = frame.materials->deviceAddress();

    // The fence of this frame context has been waited for, so no GPU work reads the buffer.
    std::memcpy(frame.sceneData->mappedBytes().data(), &scene, sizeof(scene));
}

core::Result<std::uint32_t> VulkanRenderer::recordFrame(const FrameContext& frame,
                                                        std::uint32_t imageIndex,
                                                        bool drawImGui) const
{
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    DEVEX_VK_TRY(vkResetCommandPool, m_device.handle(), frame.commandPool, 0);

    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    DEVEX_VK_TRY(vkBeginCommandBuffer, commandBuffer, &beginInfo);

    const VkImage backbuffer = m_swapchain->image(imageIndex);
    transitionImage(commandBuffer, backbuffer, ImageState::AcquiredBackbuffer,
                    ImageState::ColorAttachment);

    const math::Vec4& clearColor = m_world.clearColor;
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_swapchain->imageView(imageIndex),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {clearColor.r, clearColor.g, clearColor.b,
                                             clearColor.a}}},
    };
    // With reversed depth, 0 is infinitely far away.
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = m_depthImage->view(),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .clearValue = {.depthStencil = {.depth = 0.0f}},
    };
    const math::Extent2D extent = m_swapchain->extent();
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.offset = {0, 0}, .extent = {extent.width, extent.height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    const VkViewport viewport{
        .width = static_cast<float>(extent.width),
        .height = static_cast<float>(extent.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    const VkRect2D scissor{.offset = {0, 0}, .extent = {extent.width, extent.height}};
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Both pipelines share the same layout, so the textures stay bound when switching.
    const VkDescriptorSet textureSet = m_bindless->handle();
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipeline->layout(),
                            0, 1, &textureSet, 0, nullptr);
    const VkDeviceAddress sceneData = frame.sceneData->deviceAddress();
    const GraphicsPipeline* boundPipeline = nullptr;
    std::uint32_t drawCalls = 0;
    for (const MeshInstance& instance : m_world.meshes)
    {
        const GpuMesh* const mesh = m_meshes.find(instance.mesh);
        DEVEX_ASSERT_MSG(mesh != nullptr, "the render world references a destroyed mesh");
        if (mesh == nullptr || instance.submesh >= mesh->submeshes.size())
        {
            continue;
        }

        const MaterialDesc* const material = m_materials.find(instance.material);
        const MaterialHandle materialHandle = material != nullptr ? instance.material
                                                                  : m_defaultMaterial;
        const GraphicsPipeline* const pipeline =
            material != nullptr && material->doubleSided ? &*m_doubleSidedPipeline
                                                         : &*m_meshPipeline;
        if (pipeline != boundPipeline)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());
            boundPipeline = pipeline;
        }

        const DrawPushConstants constants{
            .scene = sceneData,
            .vertices = mesh->vertices.deviceAddress(),
            .world = instance.transform,
            .material = materialHandle.index,
        };
        vkCmdPushConstants(commandBuffer, pipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        const SubmeshRange& submesh = mesh->submeshes[instance.submesh];
        vkCmdBindIndexBuffer(commandBuffer, mesh->indices.handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        ++drawCalls;
    }
    vkCmdEndRendering(commandBuffer);

    // Tools are drawn over the scene, without depth.
    if (ImDrawData* const drawData = drawImGui ? ImGui::GetDrawData() : nullptr;
        drawData != nullptr && drawData->Valid)
    {
        const VkRenderingAttachmentInfo overlayAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = m_swapchain->imageView(imageIndex),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        };
        const VkRenderingInfo overlayInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.offset = {0, 0}, .extent = {extent.width, extent.height}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &overlayAttachment,
        };
        vkCmdBeginRendering(commandBuffer, &overlayInfo);
        ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
        vkCmdEndRendering(commandBuffer);
    }

    transitionImage(commandBuffer, backbuffer, ImageState::ColorAttachment, ImageState::Present);
    DEVEX_VK_TRY(vkEndCommandBuffer, commandBuffer);
    return drawCalls;
}

RendererStats VulkanRenderer::stats() const noexcept
{
    RendererStats stats{
        .drawCalls = m_lastDrawCalls,
        .meshCount = m_meshes.size(),
        .textureCount = m_textures.size(),
        // The default material is not counted.
        .materialCount = m_materials.size() - 1,
        .swapchainExtent = m_swapchain ? m_swapchain->extent() : math::Extent2D{},
    };

    const VkPhysicalDeviceMemoryProperties* memory = nullptr;
    vmaGetMemoryProperties(m_allocator.handle(), &memory);
    std::array<VmaBudget, VK_MAX_MEMORY_HEAPS> budgets{};
    vmaGetHeapBudgets(m_allocator.handle(), budgets.data());
    for (std::uint32_t heap = 0; heap < memory->memoryHeapCount; ++heap)
    {
        if ((memory->memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
        {
            stats.gpuMemoryUsage += budgets[heap].usage;
            stats.gpuMemoryBudget += budgets[heap].budget;
        }
    }
    return stats;
}

core::Result<void> VulkanRenderer::initializeImGui()
{
    DEVEX_ASSERT(!m_imguiInitialized);
    DEVEX_ASSERT_MSG(ImGui::GetCurrentContext() != nullptr, "create an ImGui context first");
    if (!m_swapchain)
    {
        return core::makeError(core::ErrorCode::InvalidState,
                               "ImGui needs a visible window to know the swapchain format");
    }

    m_imguiColorFormat = m_swapchain->format();
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = requiredApiVersion;
    info.Instance = m_instance.handle();
    info.PhysicalDevice = m_device.physicalDevice();
    info.Device = m_device.handle();
    info.QueueFamily = m_device.queueFamily();
    info.Queue = m_device.queue();
    // The backend creates a small pool for the textures it binds.
    info.DescriptorPoolSize = 16;
    info.MinImageCount = std::max(m_swapchain->imageCount(), 2u);
    info.ImageCount = info.MinImageCount;
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &m_imguiColorFormat,
    };
    info.CheckVkResultFn = [](VkResult result) {
        if (result < VK_SUCCESS)
        {
            DEVEX_LOG_ERROR("ImGui Vulkan backend: {}", toString(result));
        }
    };

    if (!ImGui_ImplVulkan_Init(&info))
    {
        return core::makeError(core::ErrorCode::Graphics, "cannot initialize ImGui for Vulkan");
    }
    m_imguiInitialized = true;
    return {};
}

void VulkanRenderer::shutdownImGui() noexcept
{
    if (m_imguiInitialized)
    {
        vkDeviceWaitIdle(m_device.handle());
        ImGui_ImplVulkan_Shutdown();
        m_imguiInitialized = false;
        m_imguiDrawQueued = false;
    }
}

void VulkanRenderer::beginImGuiFrame()
{
    DEVEX_ASSERT(m_imguiInitialized);
    ImGui_ImplVulkan_NewFrame();
}

void VulkanRenderer::queueImGuiDrawData() noexcept
{
    m_imguiDrawQueued = m_imguiInitialized;
}

void VulkanRenderer::releaseRetiredResources() noexcept
{
    // Frames before the retirement may still run on the GPU; they have all completed once as
    // many frames as can be in flight have started since.
    std::erase_if(m_retiredMeshes, [this](const RetiredMesh& retired) {
        return m_frameIndex >= retired.retiredAtFrame + framesInFlight;
    });
    std::erase_if(m_retiredTextures, [this](const RetiredTexture& retired) {
        if (m_frameIndex < retired.retiredAtFrame + framesInFlight)
        {
            return false;
        }
        // The slot must not keep pointing at a destroyed view.
        m_bindless->setTexture(retired.texture.slot, m_whiteTexture->image.view());
        m_freeTextureSlots.push_back(retired.texture.slot);
        return true;
    });
}

void VulkanRenderer::destroyPresentSemaphores() noexcept
{
    for (const VkSemaphore semaphore : m_presentSemaphores)
    {
        vkDestroySemaphore(m_device.handle(), semaphore, nullptr);
    }
    m_presentSemaphores.clear();
}

} // namespace devex::render::vulkan
