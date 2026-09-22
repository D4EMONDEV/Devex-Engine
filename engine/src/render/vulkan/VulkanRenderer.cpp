#include "VulkanRenderer.hpp"

#include "Commands.hpp"
#include "GpuData.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>
#include <devex/render/Photometry.hpp>

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace devex::render::vulkan {
namespace {

// volk loads device functions globally, so a second device would replace those of the first.
std::atomic<bool> rendererExists{false};

constexpr std::uint64_t noTimeout = std::numeric_limits<std::uint64_t>::max();

// Local lights beyond this distance share the last cluster slice.
constexpr float clusterFarPlane = 500.0f;

// Scene lines are drawn this much closer in depth, so that lines lying on a surface stay visible.
constexpr float sceneLineDepthScale = 1.002f;

// Linear orange, like the selection of most editors.
constexpr math::Vec4 outlineColor{1.0f, 0.42f, 0.05f, 1.0f};

static_assert(sizeof(ClusterRange) == sizeof(GpuCluster));

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
    case asset::TextureFormat::Rgba16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case asset::TextureFormat::R8Unorm:
        return VK_FORMAT_R8_UNORM;
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

// Scales and moves clip space so that one pixel of an image covers the whole viewport.
[[nodiscard]] math::Mat4 pixelSelectionMatrix(math::Extent2D extent, std::uint32_t x, std::uint32_t y) noexcept
{
    const auto width = static_cast<float>(extent.width);
    const auto height = static_cast<float>(extent.height);
    // Vulkan normalized device coordinates put -1 at the top.
    const float centerX = (static_cast<float>(x) + 0.5f) / width * 2.0f - 1.0f;
    const float centerY = (static_cast<float>(y) + 0.5f) / height * 2.0f - 1.0f;
    math::Mat4 matrix{1.0f};
    matrix[0][0] = width;
    matrix[1][1] = height;
    matrix[3][0] = -centerX * width;
    matrix[3][1] = -centerY * height;
    return matrix;
}

[[nodiscard]] math::Mat4 projectionMatrix(const RenderCamera& camera, float aspectRatio) noexcept
{
    // Vulkan clip space points Y down, while the engine convention points it up.
    math::Mat4 clipCorrection{1.0f};
    clipCorrection[1][1] = -1.0f;
    return clipCorrection * math::perspectiveReverseZ(camera.verticalFov, aspectRatio, camera.nearPlane);
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
    , m_requestedSamples(config.msaaSamples)
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
        vertices.push_back({vertex.position, vertex.uv.x, vertex.normal, vertex.uv.y, vertex.tangent});
    }
    std::vector<GpuVertexSkin> skin;
    skin.reserve(mesh.skin.size());
    for (const asset::VertexSkin& vertex : mesh.skin)
    {
        GpuVertexSkin& entry = skin.emplace_back();
        for (std::size_t slot = 0; slot < entry.joints.size(); ++slot)
        {
            entry.joints[slot] = vertex.joints[slot];
        }
        entry.weights = vertex.weights;
    }
    const VkDeviceSize vertexBytes = vertices.size() * sizeof(GpuVertex);
    const VkDeviceSize indexBytes = mesh.indices.size() * sizeof(std::uint32_t);
    const VkDeviceSize skinBytes = skin.size() * sizeof(GpuVertexSkin);

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
    std::optional<Buffer> skinBuffer;
    if (skinBytes > 0)
    {
        core::Result<Buffer> created = Buffer::create(
            m_allocator, {
                             .size = skinBytes,
                             .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                         });
        if (!created)
        {
            return std::unexpected(created.error());
        }
        skinBuffer = std::move(*created);
    }
    core::Result<Buffer> staging = Buffer::create(m_allocator, {
                                                                   .size = vertexBytes + indexBytes + skinBytes,
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
    if (skinBytes > 0)
    {
        std::memcpy(stagingBytes.data() + vertexBytes + indexBytes, skin.data(), skinBytes);
    }

    const core::Result<void> uploaded = m_upload.submit([&](VkCommandBuffer commandBuffer) {
        const VkBufferCopy vertexCopy{.srcOffset = 0, .dstOffset = 0, .size = vertexBytes};
        vkCmdCopyBuffer(commandBuffer, staging->handle(), vertexBuffer->handle(), 1, &vertexCopy);
        const VkBufferCopy indexCopy{.srcOffset = vertexBytes, .dstOffset = 0, .size = indexBytes};
        vkCmdCopyBuffer(commandBuffer, staging->handle(), indexBuffer->handle(), 1, &indexCopy);
        if (skinBytes > 0)
        {
            const VkBufferCopy skinCopy{
                .srcOffset = vertexBytes + indexBytes, .dstOffset = 0, .size = skinBytes};
            vkCmdCopyBuffer(commandBuffer, staging->handle(), skinBuffer->handle(), 1, &skinCopy);
        }
    });
    if (!uploaded)
    {
        return std::unexpected(uploaded.error());
    }

    GpuMesh gpuMesh{
        .vertices = std::move(*vertexBuffer),
        .indices = std::move(*indexBuffer),
        .skin = std::move(skinBuffer),
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
    else if (m_nextTextureSlot < m_descriptors->textureCapacity())
    {
        slot = m_nextTextureSlot;
    }
    else
    {
        return core::makeError(core::ErrorCode::OutOfMemory, "all {} texture slots are in use",
                               m_descriptors->textureCapacity());
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

    if (!m_swapchain || m_swapchainOutdated || windowPixelSize != m_swapchainWindowPixelSize)
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
    const auto frameSlot = static_cast<std::uint32_t>(m_frameIndex % framesInFlight);
    FrameContext& frame = m_frames[frameSlot];
    DEVEX_VK_TRY(vkWaitForFences, device, 1, &frame.completed, VK_TRUE, noTimeout);
    readPickResult(frame);
    releaseRetiredResources();
    updateExposure(frame);
    if (core::Result<void> environment = updateEnvironment(); !environment)
    {
        return environment;
    }

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

    const math::Extent2D viewport = m_world.viewport;
    m_sceneExtent = viewport.width > 0 && viewport.height > 0
                        ? math::Extent2D{std::min(viewport.width, maxViewportSize),
                                         std::min(viewport.height, maxViewportSize)}
                        : m_swapchain->extent();
    const float aspectRatio = static_cast<float>(m_sceneExtent.width) / static_cast<float>(m_sceneExtent.height);
    if (core::Result<void> lights = uploadLights(frame, aspectRatio); !lights)
    {
        return lights;
    }
    if (core::Result<void> bones = uploadBones(frame); !bones)
    {
        return bones;
    }
    if (const std::optional<PickRequest>& pick = m_world.pick; pick)
    {
        // A pixel outside the image shows nothing, which needs no GPU work.
        if (pick->x < m_sceneExtent.width && pick->y < m_sceneExtent.height)
        {
            frame.pickRequest = pick->id;
        }
        else
        {
            m_pickResults.push_back({.request = pick->id});
        }
    }

    const RenderSun& sun = m_world.sun;
    const bool sunEnabled = std::max({sun.illuminance.r, sun.illuminance.g, sun.illuminance.b}) > 0.0f;
    const bool drawShadows = sunEnabled && sun.castShadows && sun.shadowDistance > m_world.camera.nearPlane;
    m_cascades.reset();
    if (drawShadows)
    {
        m_cascades = computeShadowCascades(math::inverse(m_world.camera.view), m_world.camera.verticalFov,
                                           aspectRatio, m_world.camera.nearPlane, sun.shadowDistance,
                                           sun.direction, shadowMapSize);
    }
    writeSceneData(frame, m_cascades);

    core::Result<std::uint32_t> recorded = recordFrame(frame, frameSlot, imageIndex, drawImGui, drawShadows);
    if (!recorded)
    {
        return std::unexpected(recorded.error());
    }
    m_lastDrawCalls = *recorded;
    m_lastLightCount = static_cast<std::uint32_t>(m_world.lights.size());

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

        core::Result<Buffer> pickReadback = Buffer::create(m_allocator, {
                                                                             .size = sizeof(std::uint32_t),
                                                                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                                                             .hostVisible = true,
                                                                         });
        if (!pickReadback)
        {
            return std::unexpected(pickReadback.error());
        }
        frame.pickReadback = std::move(*pickReadback);

        for (const auto& [buffer, bytes] :
             {std::pair<std::optional<Buffer>*, VkDeviceSize>{&frame.sceneData, sizeof(GpuSceneData)},
              {&frame.luminance, VkDeviceSize{luminanceGridWidth} * luminanceGridHeight * sizeof(float)}})
        {
            core::Result<Buffer> created =
                Buffer::create(m_allocator, {
                                                .size = bytes,
                                                .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                         VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                                .hostVisible = true,
                                            });
            if (!created)
            {
                return std::unexpected(created.error());
            }
            *buffer = std::move(*created);
        }
    }
    return {};
}

core::Result<void> VulkanRenderer::createDefaultResources()
{
    m_samples = m_device.sampleCount(m_requestedSamples);

    core::Result<DescriptorSets> descriptors = DescriptorSets::create(
        m_device, std::min(m_device.maxBindlessTextures(), maxTextures),
        static_cast<std::uint32_t>(framesInFlight));
    if (!descriptors)
    {
        return std::unexpected(descriptors.error());
    }
    m_descriptors = std::move(*descriptors);

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

    core::Result<Image> emptyMask = Image::create(m_device, m_allocator,
                                                  {
                                                      .format = selectionMaskFormat,
                                                      .extent = {1, 1},
                                                      .usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                                                  });
    if (!emptyMask)
    {
        return std::unexpected(emptyMask.error());
    }
    const VkImage emptyMaskHandle = emptyMask->handle();
    if (core::Result<void> cleared = m_upload.submit([&](VkCommandBuffer commandBuffer) {
            transitionImage(commandBuffer, emptyMaskHandle, ImageState::Undefined, ImageState::TransferDestination);
            const VkClearColorValue black{};
            const VkImageSubresourceRange range{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1};
            vkCmdClearColorImage(commandBuffer, emptyMaskHandle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
            transitionImage(commandBuffer, emptyMaskHandle, ImageState::TransferDestination, ImageState::ShaderReadOnly);
        });
        !cleared)
    {
        return cleared;
    }
    m_emptySelectionMask = std::move(*emptyMask);

    m_defaultMaterial = m_materials.insert(MaterialDesc{
        .baseColorFactor = {0.72f, 0.74f, 0.78f, 1.0f},
        .roughnessFactor = 0.6f,
    });

    core::Result<std::unique_ptr<EnvironmentBaker>> baker =
        EnvironmentBaker::create(m_device, m_allocator, m_upload, m_shaderDirectory);
    if (!baker)
    {
        return std::unexpected(baker.error());
    }
    m_baker = std::move(*baker);

    core::Result<Image> brdfTable = m_baker->bakeBrdfTable();
    if (!brdfTable)
    {
        return std::unexpected(brdfTable.error());
    }
    m_brdfTable = std::move(*brdfTable);
    m_descriptors->setBrdfLut(m_brdfTable->view());

    // A white sky, tinted and scaled by the environment settings, stands in for sky textures.
    core::Result<EnvironmentMaps> uniform = m_baker->bake(m_whiteTexture->image.view(), 1);
    if (!uniform)
    {
        return std::unexpected(uniform.error());
    }
    m_uniformEnvironment = std::move(*uniform);
    m_descriptors->setEnvironment(m_uniformEnvironment->specular.view(),
                                  m_uniformEnvironment->irradiance.view(),
                                  m_whiteTexture->image.view());
    m_environmentBound = true;

    if (m_samples != VK_SAMPLE_COUNT_1_BIT)
    {
        DEVEX_LOG_DEBUG("Scene rendered with {}x multisampling", static_cast<std::uint32_t>(m_samples));
    }
    return createScenePipelines();
}

core::Result<void> VulkanRenderer::createScenePipelines()
{
    const VkDevice device = m_device.handle();
    const std::array globalOnly{m_descriptors->globalLayout()};
    const std::array bothSets{m_descriptors->globalLayout(), m_descriptors->frameLayout()};

    GraphicsPipelineConfig meshConfig{
        .shaderPath = m_shaderDirectory / "mesh.spv",
        .setLayouts = bothSets,
        .pushConstantSize = sizeof(DrawPushConstants),
        .colorFormat = sceneFormat,
        .depthFormat = depthFormat,
        .samples = m_samples,
    };
    core::Result<Pipeline> mesh = createGraphicsPipeline(device, meshConfig);
    meshConfig.cullMode = VK_CULL_MODE_NONE;
    core::Result<Pipeline> doubleSided = createGraphicsPipeline(device, meshConfig);

    core::Result<Pipeline> shadow = createGraphicsPipeline(
        device, {
                    .shaderPath = m_shaderDirectory / "shadow.spv",
                    .setLayouts = globalOnly,
                    .pushConstantSize = sizeof(DrawPushConstants),
                    .depthFormat = depthFormat,
                    .cullMode = VK_CULL_MODE_NONE,
                    .depthCompare = VK_COMPARE_OP_LESS_OR_EQUAL,
                    .depthBias = true,
                    .depthClamp = m_device.supportsDepthClamp(),
                });

    // The sky covers what no geometry wrote, at the infinitely far depth 0.
    core::Result<Pipeline> sky = createGraphicsPipeline(
        device, {
                    .shaderPath = m_shaderDirectory / "sky.spv",
                    .setLayouts = globalOnly,
                    .pushConstantSize = sizeof(SkyPushConstants),
                    .colorFormat = sceneFormat,
                    .depthFormat = depthFormat,
                    .samples = m_samples,
                    .cullMode = VK_CULL_MODE_NONE,
                    .depthWrite = false,
                });

    core::Result<Pipeline> luminance = createComputePipeline(
        device, {
                    .shaderPath = m_shaderDirectory / "luminance.spv",
                    .entry = "computeMain",
                    .setLayouts = bothSets,
                    .pushConstantSize = sizeof(LuminancePushConstants),
                });

    // Both sides are picked and outlined, so that thin objects can be selected from anywhere.
    core::Result<Pipeline> pick = createGraphicsPipeline(
        device, {
                    .shaderPath = m_shaderDirectory / "pick.spv",
                    .vertexEntry = "pickVertex",
                    .fragmentEntry = "pickFragment",
                    .setLayouts = globalOnly,
                    .pushConstantSize = sizeof(DrawPushConstants),
                    .colorFormat = pickFormat,
                    .depthFormat = depthFormat,
                    .cullMode = VK_CULL_MODE_NONE,
                });
    core::Result<Pipeline> selectionMask = createGraphicsPipeline(
        device, {
                    .shaderPath = m_shaderDirectory / "pick.spv",
                    .vertexEntry = "maskVertex",
                    .fragmentEntry = "maskFragment",
                    .setLayouts = globalOnly,
                    .pushConstantSize = sizeof(DrawPushConstants),
                    .colorFormat = selectionMaskFormat,
                    .cullMode = VK_CULL_MODE_NONE,
                    .depthTest = false,
                    .depthWrite = false,
                });

    for (core::Result<Pipeline>* result : {&mesh, &doubleSided, &shadow, &sky, &luminance, &pick, &selectionMask})
    {
        if (!*result)
        {
            return std::unexpected(result->error());
        }
    }
    m_meshPipeline = std::move(*mesh);
    m_doubleSidedPipeline = std::move(*doubleSided);
    m_shadowPipeline = std::move(*shadow);
    m_skyPipeline = std::move(*sky);
    m_luminancePipeline = std::move(*luminance);
    m_pickPipeline = std::move(*pick);
    m_selectionMaskPipeline = std::move(*selectionMask);
    return {};
}

core::Result<void> VulkanRenderer::createTargetPipelines(VkFormat format)
{
    const VkDevice device = m_device.handle();
    const std::array bothSets{m_descriptors->globalLayout(), m_descriptors->frameLayout()};
    core::Result<Pipeline> tonemap = createGraphicsPipeline(
        device, {
                    .shaderPath = m_shaderDirectory / "tonemap.spv",
                    .setLayouts = bothSets,
                    .pushConstantSize = sizeof(TonemapPushConstants),
                    .colorFormat = format,
                    .cullMode = VK_CULL_MODE_NONE,
                    .depthTest = false,
                    .depthWrite = false,
                });

    // The overlay pass binds the scene depth, which only scene lines test against.
    const GraphicsPipelineConfig overlay{
        .shaderPath = m_shaderDirectory / "overlay.spv",
        .vertexEntry = "colorVertex",
        .fragmentEntry = "colorFragment",
        .pushConstantSize = sizeof(OverlayPushConstants),
        .colorFormat = format,
        .depthFormat = depthFormat,
        .topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        .alphaBlend = true,
        .cullMode = VK_CULL_MODE_NONE,
        .depthTest = false,
        .depthWrite = false,
    };
    GraphicsPipelineConfig sceneLineConfig = overlay;
    sceneLineConfig.depthTest = true;
    core::Result<Pipeline> sceneLines = createGraphicsPipeline(device, sceneLineConfig);
    core::Result<Pipeline> overlayLines = createGraphicsPipeline(device, overlay);
    GraphicsPipelineConfig triangleConfig = overlay;
    triangleConfig.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    core::Result<Pipeline> overlayTriangles = createGraphicsPipeline(device, triangleConfig);
    // The interface is drawn flat over the image, with its own vertices and no depth at all.
    core::Result<Pipeline> interface = createGraphicsPipeline(
        device, {
                    .shaderPath = m_shaderDirectory / "ui.spv",
                    .setLayouts = bothSets,
                    .pushConstantSize = sizeof(UiPushConstants),
                    .colorFormat = format,
                    .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
                    .alphaBlend = true,
                    .cullMode = VK_CULL_MODE_NONE,
                    .depthTest = false,
                    .depthWrite = false,
                });

    GraphicsPipelineConfig outlineConfig = triangleConfig;
    outlineConfig.vertexEntry = "outlineVertex";
    outlineConfig.fragmentEntry = "outlineFragment";
    outlineConfig.setLayouts = bothSets;
    core::Result<Pipeline> outline = createGraphicsPipeline(device, outlineConfig);

    for (core::Result<Pipeline>* result :
         {&tonemap, &sceneLines, &overlayLines, &overlayTriangles, &outline, &interface})
    {
        if (!*result)
        {
            return std::unexpected(result->error());
        }
    }
    m_tonemapPipeline = std::move(*tonemap);
    m_sceneLinePipeline = std::move(*sceneLines);
    m_overlayLinePipeline = std::move(*overlayLines);
    m_overlayTrianglePipeline = std::move(*overlayTriangles);
    m_outlinePipeline = std::move(*outline);
    m_uiPipeline = std::move(*interface);
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

    if (!m_tonemapPipeline || previousFormat != m_swapchain->format())
    {
        if (core::Result<void> pipelines = createTargetPipelines(m_swapchain->format()); !pipelines)
        {
            return pipelines;
        }
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
                        ImageState::TransferDestination);
        vkCmdCopyBufferToImage(commandBuffer, staging->handle(), imageHandle,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                               static_cast<std::uint32_t>(copies.size()), copies.data());
        transitionImage(commandBuffer, imageHandle, ImageState::TransferDestination,
                        ImageState::ShaderReadOnly);
    });
    if (!uploaded)
    {
        return std::unexpected(uploaded.error());
    }

    m_descriptors->setTexture(slot, image->view());
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

core::Result<void> VulkanRenderer::uploadBones(FrameContext& frame) const
{
    const VkDeviceSize bytes = std::max<std::size_t>(m_world.boneMatrices.size(), 1) * sizeof(math::Mat4);
    if (core::Result<void> ensured = ensureHostBuffer(frame.bones, bytes); !ensured)
    {
        return ensured;
    }
    if (!m_world.boneMatrices.empty())
    {
        std::memcpy(frame.bones->mappedBytes().data(), m_world.boneMatrices.data(),
                    m_world.boneMatrices.size() * sizeof(math::Mat4));
    }
    return {};
}

core::Result<void> VulkanRenderer::ensureHostBuffer(std::optional<Buffer>& buffer, VkDeviceSize bytes) const
{
    if (buffer && buffer->size() >= bytes)
    {
        return {};
    }
    // Grows geometrically; the fence of the frame owning the buffer was waited for.
    buffer.reset();
    core::Result<Buffer> created =
        Buffer::create(m_allocator, {
                                        .size = std::max<VkDeviceSize>(bytes * 2, 4096),
                                        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                 VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                                        .hostVisible = true,
                                    });
    if (!created)
    {
        return std::unexpected(created.error());
    }
    buffer = std::move(*created);
    return {};
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
    if (core::Result<void> ensured = ensureHostBuffer(frame.materials, requiredBytes); !ensured)
    {
        return ensured;
    }
    std::memcpy(frame.materials->mappedBytes().data(), m_gpuMaterials.data(), requiredBytes);
    frame.materialVersion = m_materialVersion;
    return {};
}

core::Result<void> VulkanRenderer::updateEnvironment()
{
    const TextureHandle requested =
        m_textures.contains(m_world.environment.sky) ? m_world.environment.sky : TextureHandle{};
    if (m_environmentBound && requested == m_environmentSource)
    {
        return {};
    }
    // A texture that cannot be baked is not tried again every frame.
    if (requested.isValid() && requested == m_failedEnvironment && m_environmentBound &&
        !m_environmentSource.isValid())
    {
        return {};
    }

    if (m_textureEnvironment)
    {
        m_retiredEnvironments.push_back({std::move(*m_textureEnvironment), m_frameIndex});
        m_textureEnvironment.reset();
    }

    if (requested.isValid())
    {
        const GpuTexture* const texture = m_textures.find(requested);
        core::Result<EnvironmentMaps> maps =
            m_baker->bake(texture->image.view(), texture->image.extent().width);
        if (maps)
        {
            m_textureEnvironment = std::move(*maps);
            m_descriptors->setEnvironment(m_textureEnvironment->specular.view(),
                                          m_textureEnvironment->irradiance.view(),
                                          texture->image.view());
            m_environmentSource = requested;
            m_environmentBound = true;
            return {};
        }
        DEVEX_LOG_ERROR("Cannot bake the environment lighting: {}", maps.error());
        m_failedEnvironment = requested;
    }

    m_descriptors->setEnvironment(m_uniformEnvironment->specular.view(),
                                  m_uniformEnvironment->irradiance.view(),
                                  m_whiteTexture->image.view());
    m_environmentSource = {};
    m_environmentBound = true;
    return {};
}

void VulkanRenderer::updateExposure(FrameContext& frame)
{
    const RenderCamera& camera = m_world.camera;
    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds =
        m_exposureInitialized
            ? std::clamp(std::chrono::duration<float>(now - m_lastExposureUpdate).count(), 0.0f, 0.25f)
            : 0.0f;
    m_lastExposureUpdate = now;

    if (!camera.autoExposure)
    {
        m_ev100 = camera.ev100 - camera.exposureCompensation;
        m_targetEv100 = m_ev100;
        m_exposureInitialized = true;
        frame.luminanceMeasured = false;
        return;
    }
    if (!m_exposureInitialized)
    {
        m_ev100 = camera.ev100 - camera.exposureCompensation;
        m_targetEv100 = m_ev100;
        m_exposureInitialized = true;
    }

    // This frame context measured the frame it recorded last time, which has now completed.
    if (frame.luminanceMeasured && frame.exposure > 0.0f)
    {
        const std::span<const std::byte> bytes = frame.luminance->mappedBytes();
        m_luminanceSamples.resize(luminanceGridWidth * luminanceGridHeight);
        std::memcpy(m_luminanceSamples.data(), bytes.data(), m_luminanceSamples.size() * sizeof(float));
        for (float& sample : m_luminanceSamples)
        {
            sample /= frame.exposure;
        }
        if (const float average = averageLuminance(m_luminanceSamples); average > 0.0f)
        {
            m_targetEv100 = std::clamp(ev100FromAverageLuminance(average) - camera.exposureCompensation,
                                       camera.minEv100, camera.maxEv100);
        }
        frame.luminanceMeasured = false;
    }
    m_ev100 = adaptExposure(m_ev100, m_targetEv100, camera.adaptationSpeed, deltaSeconds);
}

core::Result<void> VulkanRenderer::uploadLights(FrameContext& frame, float aspectRatio)
{
    m_gpuLights.clear();
    for (const RenderLight& light : m_world.lights)
    {
        GpuLight gpuLight{
            .position = light.position,
            .range = std::max(light.range, 1e-3f),
            .direction = math::length(light.direction) > 0.0f ? math::normalize(light.direction)
                                                              : math::Vec3{0.0f, 0.0f, -1.0f},
            .intensity = light.intensity,
        };
        if (light.type == LightType::Spot)
        {
            const float cosOuter = std::cos(std::max(light.outerAngle, 1e-3f));
            const float cosInner = std::cos(std::clamp(light.innerAngle, 0.0f, light.outerAngle));
            gpuLight.spotScale = 1.0f / std::max(cosInner - cosOuter, 1e-4f);
            gpuLight.spotOffset = -cosOuter * gpuLight.spotScale;
        }
        m_gpuLights.push_back(gpuLight);
    }

    const ClusterGrid grid{
        .nearPlane = m_world.camera.nearPlane,
        .farPlane = std::max(clusterFarPlane, m_world.camera.nearPlane * 2.0f),
    };
    assignLightsToClusters(grid, m_world.camera.view, m_world.camera.verticalFov, aspectRatio,
                           m_world.lights, m_clusters);

    const VkDeviceSize lightBytes = std::max<std::size_t>(m_gpuLights.size(), 1) * sizeof(GpuLight);
    const VkDeviceSize clusterBytes = m_clusters.clusters.size() * sizeof(GpuCluster);
    const VkDeviceSize indexBytes =
        std::max<std::size_t>(m_clusters.lightIndices.size(), 1) * sizeof(std::uint32_t);
    for (const auto& [buffer, bytes] :
         {std::pair<std::optional<Buffer>*, VkDeviceSize>{&frame.lights, lightBytes},
          {&frame.clusters, clusterBytes},
          {&frame.clusterLights, indexBytes}})
    {
        if (core::Result<void> ensured = ensureHostBuffer(*buffer, bytes); !ensured)
        {
            return ensured;
        }
    }
    std::memcpy(frame.lights->mappedBytes().data(), m_gpuLights.data(), m_gpuLights.size() * sizeof(GpuLight));
    std::memcpy(frame.clusters->mappedBytes().data(), m_clusters.clusters.data(), clusterBytes);
    std::memcpy(frame.clusterLights->mappedBytes().data(), m_clusters.lightIndices.data(),
                m_clusters.lightIndices.size() * sizeof(std::uint32_t));
    return {};
}

void VulkanRenderer::writeSceneData(FrameContext& frame,
                                    const std::optional<ShadowCascades>& cascades) const noexcept
{
    const math::Extent2D extent = m_sceneExtent;
    const float aspectRatio = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    const RenderCamera& camera = m_world.camera;
    const math::Mat4 projection = projectionMatrix(camera, aspectRatio);

    math::Mat4 rotationOnly = camera.view;
    rotationOnly[3] = math::Vec4{0.0f, 0.0f, 0.0f, 1.0f};

    GpuSceneData scene;
    scene.viewProjection = projection * camera.view;
    scene.view = camera.view;
    scene.skyInverseViewProjection = math::inverse(projection * rotationOnly);
    scene.cameraPosition = math::Vec3(math::inverse(camera.view)[3]);
    scene.exposure = exposureFromEv100(m_ev100);
    frame.exposure = scene.exposure;

    const RenderSun& sun = m_world.sun;
    const float sunLength = math::length(sun.direction);
    scene.sunDirection = sunLength > 0.0f ? sun.direction / sunLength : math::Vec3{0.0f, -1.0f, 0.0f};
    scene.sunIlluminance = sun.illuminance;
    if (std::max({sun.illuminance.r, sun.illuminance.g, sun.illuminance.b}) > 0.0f)
    {
        scene.sunFlags |= sunEnabledFlag;
    }
    if (cascades)
    {
        scene.sunFlags |= sunCastsShadowsFlag;
        for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
        {
            scene.cascadeSplits[static_cast<int>(cascade)] = cascades->splitDistances[cascade];
            scene.cascadeTexelSizes[static_cast<int>(cascade)] = cascades->texelSizes[cascade];
            scene.cascadeViewProjections[cascade] = cascades->viewProjections[cascade];
        }
        scene.shadowDistance = sun.shadowDistance;
    }

    const RenderEnvironment& environment = m_world.environment;
    scene.environmentIntensity = environment.intensity;
    scene.environmentColor = environment.color;
    scene.environmentRotation = environment.rotation;
    scene.specularMipCount = static_cast<float>(EnvironmentBaker::specularMipCount);

    scene.viewportWidth = static_cast<float>(extent.width);
    scene.viewportHeight = static_cast<float>(extent.height);
    const ClusterGrid grid;
    scene.clusterCountX = grid.tilesX;
    scene.clusterCountY = grid.tilesY;
    scene.clusterCountZ = grid.slices;
    scene.lightCount = static_cast<std::uint32_t>(m_world.lights.size());
    scene.clusterSliceScale = m_clusters.sliceScale;
    scene.clusterSliceBias = m_clusters.sliceBias;

    scene.materials = frame.materials->deviceAddress();
    scene.lights = frame.lights->deviceAddress();
    scene.clusters = frame.clusters->deviceAddress();
    scene.clusterLights = frame.clusterLights->deviceAddress();
    if (m_world.pick)
    {
        scene.pickViewProjection =
            pixelSelectionMatrix(extent, m_world.pick->x, m_world.pick->y) * scene.viewProjection;
    }

    std::memcpy(frame.sceneData->mappedBytes().data(), &scene, sizeof(scene));
}

std::uint32_t VulkanRenderer::drawMeshes(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData,
                                         VkDeviceAddress boneMatrices, MeshPass pass,
                                         std::uint32_t cascade, std::uint32_t frameSlot) const
{
    const std::array sets{m_descriptors->global(), m_descriptors->frame(frameSlot)};
    const Pipeline* boundPipeline = nullptr;
    std::uint32_t drawCalls = 0;
    for (const MeshInstance& instance : m_world.meshes)
    {
        if (pass == MeshPass::SelectionMask && !instance.outlined)
        {
            continue;
        }
        const GpuMesh* const mesh = m_meshes.find(instance.mesh);
        DEVEX_ASSERT_MSG(mesh != nullptr, "the render world references a destroyed mesh");
        if (mesh == nullptr || instance.submesh >= mesh->submeshes.size())
        {
            continue;
        }

        const MaterialDesc* const material = m_materials.find(instance.material);
        const MaterialHandle materialHandle = material != nullptr ? instance.material : m_defaultMaterial;
        const Pipeline* pipeline = nullptr;
        switch (pass)
        {
        case MeshPass::Scene:
            pipeline = material != nullptr && material->doubleSided ? &*m_doubleSidedPipeline : &*m_meshPipeline;
            break;
        case MeshPass::Shadow:
            pipeline = &*m_shadowPipeline;
            break;
        case MeshPass::Pick:
            pipeline = &*m_pickPipeline;
            break;
        case MeshPass::SelectionMask:
            pipeline = &*m_selectionMaskPipeline;
            break;
        }
        if (pipeline != boundPipeline)
        {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());
            // Only the scene pass reads the images of the frame set.
            vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->layout(), 0,
                                    pass == MeshPass::Scene ? 2u : 1u, sets.data(), 0, nullptr);
            boundPipeline = pipeline;
        }

        // A skinned instance reads its own bones, which already hold the transform to the world.
        const bool skinned = mesh->skin.has_value() && instance.boneCount > 0 && boneMatrices != 0;
        const DrawPushConstants constants{
            .scene = sceneData,
            .vertices = mesh->vertices.deviceAddress(),
            .world = instance.transform,
            .material = materialHandle.index,
            .cascade = cascade,
            .objectId = instance.objectId,
            .skinned = skinned ? 1u : 0u,
            .skin = skinned ? mesh->skin->deviceAddress() : 0,
            .bones = skinned ? boneMatrices + instance.firstBone * sizeof(math::Mat4) : 0,
        };
        vkCmdPushConstants(commandBuffer, pipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        const SubmeshRange& submesh = mesh->submeshes[instance.submesh];
        vkCmdBindIndexBuffer(commandBuffer, mesh->indices.handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, submesh.indexCount, 1, submesh.firstIndex, 0, 0);
        ++drawCalls;
    }
    return drawCalls;
}

core::Result<std::uint32_t> VulkanRenderer::recordFrame(FrameContext& frame, std::uint32_t frameSlot,
                                                        std::uint32_t imageIndex, bool drawImGui,
                                                        bool drawShadows)
{
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    DEVEX_VK_TRY(vkResetCommandPool, m_device.handle(), frame.commandPool, 0);
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    DEVEX_VK_TRY(vkBeginCommandBuffer, commandBuffer, &beginInfo);

    const math::Extent2D extent = m_sceneExtent;
    const math::Extent2D windowExtent = m_swapchain->extent();
    const VkFormat targetFormat = m_swapchain->format();
    const bool multisampled = m_samples != VK_SAMPLE_COUNT_1_BIT;
    const bool toViewport = m_world.viewport.width > 0 && m_world.viewport.height > 0;
    const bool outlines = std::ranges::any_of(m_world.meshes, &MeshInstance::outlined);
    const bool drawOverlay = outlines || !m_world.sceneLines.empty() || !m_world.overlayLines.empty() ||
                             !m_world.overlayTriangles.empty();
    const bool pick = frame.pickRequest.has_value();
    RenderGraph graph(m_device, m_allocator, frame.images);

    ImageState backbufferState = ImageState::AcquiredBackbuffer;
    const RenderGraph::ImageId backbuffer =
        graph.importImage(m_swapchain->image(imageIndex), m_swapchain->imageView(imageIndex),
                          targetFormat, backbufferState);

    std::vector<core::Result<RenderGraph::ImageId>> created;
    const auto create = [&](const ImageConfig& config) -> std::size_t {
        created.push_back(graph.createImage(config));
        return created.size() - 1;
    };
    const std::size_t shadowMapIndex = create({
        .format = depthFormat,
        .extent = {shadowMapSize, shadowMapSize},
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .layers = cascadeCount,
    });
    const std::size_t sceneColorIndex = create({
        .format = sceneFormat,
        .extent = extent,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    });
    const std::size_t depthIndex = create({
        .format = depthFormat,
        .extent = extent,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .samples = m_samples,
    });
    const std::size_t multisampledColorIndex = multisampled ? create({
                                                                  .format = sceneFormat,
                                                                  .extent = extent,
                                                                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                                  .samples = m_samples,
                                                              })
                                                            : sceneColorIndex;
    // The overlay tests lines against single-sampled depth: sample zero of the scene depth.
    const bool resolveDepth = drawOverlay && multisampled;
    const std::size_t resolvedDepthIndex = resolveDepth ? create({
                                                              .format = depthFormat,
                                                              .extent = extent,
                                                              .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                                          })
                                                        : depthIndex;
    const std::size_t selectionMaskIndex = outlines ? create({
                                                          .format = selectionMaskFormat,
                                                          .extent = extent,
                                                          .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                   VK_IMAGE_USAGE_SAMPLED_BIT,
                                                      })
                                                    : 0;
    // The tools sample the viewport image in the format they draw in.
    const VkFormat toolsFormat = m_swapchain->toolsFormat();
    const std::size_t viewportIndex = toViewport ? create({
                                                       .format = targetFormat,
                                                       .extent = extent,
                                                       .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                                VK_IMAGE_USAGE_SAMPLED_BIT,
                                                       .alternateFormat = toolsFormat != targetFormat
                                                                              ? toolsFormat
                                                                              : VK_FORMAT_UNDEFINED,
                                                   })
                                                 : 0;
    const std::size_t pickColorIndex = pick ? create({
                                                  .format = pickFormat,
                                                  .extent = {1, 1},
                                                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                              })
                                            : 0;
    const std::size_t pickDepthIndex = pick ? create({
                                                  .format = depthFormat,
                                                  .extent = {1, 1},
                                                  .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                              })
                                            : 0;
    for (const core::Result<RenderGraph::ImageId>& image : created)
    {
        if (!image)
        {
            return std::unexpected(image.error());
        }
    }
    const RenderGraph::ImageId shadowMap = *created[shadowMapIndex];
    const RenderGraph::ImageId sceneColor = *created[sceneColorIndex];
    const RenderGraph::ImageId depth = *created[depthIndex];
    const RenderGraph::ImageId multisampledColor = *created[multisampledColorIndex];
    const RenderGraph::ImageId sceneDepth = *created[resolvedDepthIndex];
    const RenderGraph::ImageId target = toViewport ? *created[viewportIndex] : backbuffer;

    core::Result<OverlayRanges> overlayRanges = OverlayRanges{};
    if (drawOverlay)
    {
        overlayRanges = uploadOverlay(frame);
        if (!overlayRanges)
        {
            return std::unexpected(overlayRanges.error());
        }
    }

    const bool drawUi = !m_world.uiDraws.empty() && !m_world.uiIndices.empty();
    if (drawUi)
    {
        if (core::Result<void> uploaded = uploadUi(frame); !uploaded)
        {
            return std::unexpected(uploaded.error());
        }
    }

    // The frame set points at this frame's transient images.
    const VkImageView selectionMaskView =
        outlines ? graph.view(*created[selectionMaskIndex]) : m_emptySelectionMask->view();
    if (frame.boundShadowMap != graph.view(shadowMap) || frame.boundSceneColor != graph.view(sceneColor) ||
        frame.boundSelectionMask != selectionMaskView)
    {
        frame.boundShadowMap = graph.view(shadowMap);
        frame.boundSceneColor = graph.view(sceneColor);
        frame.boundSelectionMask = selectionMaskView;
        m_descriptors->setFrameImages(frameSlot, frame.boundShadowMap, frame.boundSceneColor,
                                      frame.boundSelectionMask);
    }

    const VkDeviceAddress sceneData = frame.sceneData->deviceAddress();
    const VkDeviceAddress boneMatrices = frame.bones ? frame.bones->deviceAddress() : 0;
    const std::array sets{m_descriptors->global(), m_descriptors->frame(frameSlot)};
    std::uint32_t drawCalls = 0;
    const auto setViewport = [](VkCommandBuffer commands, math::Extent2D size) {
        const VkViewport viewport{
            .width = static_cast<float>(size.width),
            .height = static_cast<float>(size.height),
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(commands, 0, 1, &viewport);
        const VkRect2D scissor{.offset = {0, 0}, .extent = {size.width, size.height}};
        vkCmdSetScissor(commands, 0, 1, &scissor);
    };

    if (drawShadows)
    {
        const Image* const shadowImage = graph.image(shadowMap);
        graph.addPass("Shadow cascades", {{shadowMap, ImageAccess::DepthAttachment}},
                      [&, shadowImage](VkCommandBuffer commands) {
                          for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                          {
                              const VkRenderingAttachmentInfo depthAttachment{
                                  .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                  .imageView = shadowImage->subview(VK_IMAGE_VIEW_TYPE_2D, 0, 1, cascade, 1),
                                  .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                  .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                  .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                                  .clearValue = {.depthStencil = {.depth = 1.0f}},
                              };
                              const VkRenderingInfo renderingInfo{
                                  .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                                  .renderArea = {.extent = {shadowMapSize, shadowMapSize}},
                                  .layerCount = 1,
                                  .pDepthAttachment = &depthAttachment,
                              };
                              vkCmdBeginRendering(commands, &renderingInfo);
                              setViewport(commands, {shadowMapSize, shadowMapSize});
                              // Steep surfaces need more bias than those facing the light.
                              vkCmdSetDepthBias(commands, 0.0f, 0.0f, 1.5f);
                              drawMeshes(commands, sceneData, boneMatrices, MeshPass::Shadow, cascade, frameSlot);
                              vkCmdEndRendering(commands);
                          }
                      });
    }

    std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> sceneAccesses{
        {multisampledColor, ImageAccess::ColorAttachment},
        {depth, ImageAccess::DepthAttachment},
        {shadowMap, ImageAccess::FragmentRead},
    };
    if (multisampled)
    {
        sceneAccesses.push_back({sceneColor, ImageAccess::ColorAttachment});
    }
    if (resolveDepth)
    {
        sceneAccesses.push_back({sceneDepth, ImageAccess::DepthResolveAttachment});
    }
    graph.addPass("Scene", std::move(sceneAccesses), [&](VkCommandBuffer commands) {
        const VkRenderingAttachmentInfo colorAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = graph.view(multisampledColor),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = multisampled ? VK_RESOLVE_MODE_AVERAGE_BIT : VK_RESOLVE_MODE_NONE,
            .resolveImageView = multisampled ? graph.view(sceneColor) : VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = multisampled ? VK_ATTACHMENT_STORE_OP_DONT_CARE : VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
        };
        // With reversed depth, 0 is infinitely far away.
        const VkRenderingAttachmentInfo depthAttachment{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = graph.view(depth),
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .resolveMode = resolveDepth ? VK_RESOLVE_MODE_SAMPLE_ZERO_BIT : VK_RESOLVE_MODE_NONE,
            .resolveImageView = resolveDepth ? graph.view(sceneDepth) : VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = drawOverlay && !multisampled ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .clearValue = {.depthStencil = {.depth = 0.0f}},
        };
        const VkRenderingInfo renderingInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {.extent = {extent.width, extent.height}},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
            .pDepthAttachment = &depthAttachment,
        };
        vkCmdBeginRendering(commands, &renderingInfo);
        setViewport(commands, extent);
        drawCalls += drawMeshes(commands, sceneData, boneMatrices, MeshPass::Scene, 0, frameSlot);

        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline->handle());
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_skyPipeline->layout(), 0, 1,
                                sets.data(), 0, nullptr);
        const SkyPushConstants sky{.scene = sceneData};
        vkCmdPushConstants(commands, m_skyPipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(sky), &sky);
        vkCmdDraw(commands, 3, 1, 0, 0);
        vkCmdEndRendering(commands);
    });

    if (m_world.camera.autoExposure)
    {
        graph.addPass("Luminance", {{sceneColor, ImageAccess::ComputeRead}}, [&](VkCommandBuffer commands) {
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_luminancePipeline->handle());
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, m_luminancePipeline->layout(), 0,
                                    2, sets.data(), 0, nullptr);
            const LuminancePushConstants constants{
                .output = frame.luminance->deviceAddress(),
                .width = luminanceGridWidth,
                .height = luminanceGridHeight,
            };
            vkCmdPushConstants(commands, m_luminancePipeline->layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               sizeof(constants), &constants);
            vkCmdDispatch(commands, (luminanceGridWidth + 7) / 8, (luminanceGridHeight + 7) / 8, 1);
        });
        frame.luminanceMeasured = true;
    }

    if (pick)
    {
        const RenderGraph::ImageId pickColor = *created[pickColorIndex];
        const RenderGraph::ImageId pickDepth = *created[pickDepthIndex];
        graph.addPass("Pick", {{pickColor, ImageAccess::ColorAttachment}, {pickDepth, ImageAccess::DepthAttachment}},
                      [&, pickColor, pickDepth](VkCommandBuffer commands) {
                          const VkRenderingAttachmentInfo colorAttachment{
                              .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                              .imageView = graph.view(pickColor),
                              .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                              .clearValue = {.color = {.uint32 = {0, 0, 0, 0}}},
                          };
                          const VkRenderingAttachmentInfo depthAttachment{
                              .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                              .imageView = graph.view(pickDepth),
                              .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                              .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                              .clearValue = {.depthStencil = {.depth = 0.0f}},
                          };
                          const VkRenderingInfo renderingInfo{
                              .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                              .renderArea = {.extent = {1, 1}},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &colorAttachment,
                              .pDepthAttachment = &depthAttachment,
                          };
                          vkCmdBeginRendering(commands, &renderingInfo);
                          setViewport(commands, {1, 1});
                          drawMeshes(commands, sceneData, boneMatrices, MeshPass::Pick, 0, frameSlot);
                          vkCmdEndRendering(commands);
                      });
        graph.addPass("Pick readback", {{pickColor, ImageAccess::TransferRead}},
                      [&, pickColor](VkCommandBuffer commands) {
                          const VkBufferImageCopy copy{
                              .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                              .imageExtent = {1, 1, 1},
                          };
                          vkCmdCopyImageToBuffer(commands, graph.handle(pickColor), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                 frame.pickReadback->handle(), 1, &copy);
                      });
    }

    if (outlines)
    {
        const RenderGraph::ImageId selectionMask = *created[selectionMaskIndex];
        graph.addPass("Selection mask", {{selectionMask, ImageAccess::ColorAttachment}},
                      [&, selectionMask](VkCommandBuffer commands) {
                          const VkRenderingAttachmentInfo colorAttachment{
                              .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                              .imageView = graph.view(selectionMask),
                              .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                              .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                              .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                              .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}},
                          };
                          const VkRenderingInfo renderingInfo{
                              .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                              .renderArea = {.extent = {extent.width, extent.height}},
                              .layerCount = 1,
                              .colorAttachmentCount = 1,
                              .pColorAttachments = &colorAttachment,
                          };
                          vkCmdBeginRendering(commands, &renderingInfo);
                          setViewport(commands, extent);
                          drawMeshes(commands, sceneData, boneMatrices, MeshPass::SelectionMask, 0, frameSlot);
                          vkCmdEndRendering(commands);
                      });
    }

    graph.addPass("Tonemap", {{sceneColor, ImageAccess::FragmentRead}, {target, ImageAccess::ColorAttachment}},
                  [&](VkCommandBuffer commands) {
                      const VkRenderingAttachmentInfo colorAttachment{
                          .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                          .imageView = graph.view(target),
                          .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                          .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                      };
                      const VkRenderingInfo renderingInfo{
                          .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                          .renderArea = {.extent = {extent.width, extent.height}},
                          .layerCount = 1,
                          .colorAttachmentCount = 1,
                          .pColorAttachments = &colorAttachment,
                      };
                      vkCmdBeginRendering(commands, &renderingInfo);
                      setViewport(commands, extent);
                      vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline->handle());
                      vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              m_tonemapPipeline->layout(), 0, 2, sets.data(), 0, nullptr);
                      const TonemapPushConstants tonemap{
                          .tonemapper = static_cast<std::uint32_t>(m_world.camera.tonemapper),
                      };
                      vkCmdPushConstants(commands, m_tonemapPipeline->layout(),
                                         VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                         sizeof(tonemap), &tonemap);
                      vkCmdDraw(commands, 3, 1, 0, 0);
                      vkCmdEndRendering(commands);
                  });

    if (drawOverlay)
    {
        std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> overlayAccesses{
            {target, ImageAccess::ColorAttachment},
            {sceneDepth, ImageAccess::DepthAttachment},
        };
        if (outlines)
        {
            overlayAccesses.push_back({*created[selectionMaskIndex], ImageAccess::FragmentRead});
        }
        const OverlayRanges ranges = *overlayRanges;
        graph.addPass("Overlay", std::move(overlayAccesses), [&, ranges](VkCommandBuffer commands) {
            const VkRenderingAttachmentInfo colorAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = graph.view(target),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            };
            const VkRenderingAttachmentInfo depthAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = graph.view(sceneDepth),
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            };
            const VkRenderingInfo renderingInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.extent = {extent.width, extent.height}},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
                .pDepthAttachment = &depthAttachment,
            };
            vkCmdBeginRendering(commands, &renderingInfo);
            setViewport(commands, extent);

            OverlayPushConstants constants{
                .scene = sceneData,
                .vertices = frame.overlayVertices->deviceAddress(),
                .outlineColor = outlineColor,
            };
            // Slang's SV_VertexID does not include the first vertex of a draw: each list gets the
            // address of its own first vertex instead.
            const auto draw = [&](const Pipeline& pipeline, std::uint32_t first, std::size_t count, float depthScale) {
                if (count == 0)
                {
                    return;
                }
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
                constants.vertices = frame.overlayVertices->deviceAddress() + VkDeviceSize{first} * sizeof(GpuOverlayVertex);
                constants.depthScale = depthScale;
                vkCmdPushConstants(commands, pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                   0, sizeof(constants), &constants);
                vkCmdDraw(commands, static_cast<std::uint32_t>(count), 1, 0, 0);
            };
            draw(*m_sceneLinePipeline, ranges.sceneLines, m_world.sceneLines.size(), sceneLineDepthScale);
            if (outlines)
            {
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlinePipeline->handle());
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_outlinePipeline->layout(), 0, 2,
                                        sets.data(), 0, nullptr);
                vkCmdPushConstants(commands, m_outlinePipeline->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                                   &constants);
                vkCmdDraw(commands, 3, 1, 0, 0);
            }
            draw(*m_overlayLinePipeline, ranges.overlayLines, m_world.overlayLines.size(), 1.0f);
            draw(*m_overlayTrianglePipeline, ranges.overlayTriangles, m_world.overlayTriangles.size(), 1.0f);
            vkCmdEndRendering(commands);
        });
    }

    if (drawUi)
    {
        graph.addPass("Interface", {{target, ImageAccess::ColorAttachment}},
                      [&](VkCommandBuffer commands) {
            const VkRenderingAttachmentInfo colorAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = graph.view(target),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            };
            const VkRenderingInfo renderingInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.extent = {extent.width, extent.height}},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
            };
            vkCmdBeginRendering(commands, &renderingInfo);
            setViewport(commands, extent);
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_uiPipeline->handle());
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    m_uiPipeline->layout(), 0, 2, sets.data(), 0, nullptr);

            for (const UiDraw& uiDraw : m_world.uiDraws)
            {
                const GpuTexture* const texture = m_textures.find(uiDraw.texture);
                // Slang's SV_VertexID does not include the first vertex of a draw: the batch gets
                // the address of its own first index instead.
                const UiPushConstants constants{
                    .vertices = frame.uiVertices->deviceAddress(),
                    .indices = frame.uiIndices->deviceAddress() +
                               VkDeviceSize{uiDraw.firstIndex} * sizeof(std::uint32_t),
                    .inverseViewport = math::Vec2{2.0f / static_cast<float>(extent.width),
                                                  2.0f / static_cast<float>(extent.height)},
                    .kind = static_cast<std::uint32_t>(uiDraw.kind),
                    .texture = texture != nullptr ? texture->slot : whiteTextureSlot,
                    .rect = uiDraw.rect,
                    .radius = uiDraw.radius,
                    .sharpness = uiDraw.sharpness,
                };
                vkCmdPushConstants(commands, m_uiPipeline->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(constants), &constants);
                vkCmdDraw(commands, uiDraw.indexCount, 1, 0, 0);
                ++drawCalls;
            }
            vkCmdEndRendering(commands);
        });
    }

    if (toViewport || drawImGui)
    {
        std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> toolAccesses{
            {backbuffer, ImageAccess::ColorAttachment},
        };
        if (toViewport)
        {
            toolAccesses.push_back({target, ImageAccess::FragmentRead});
            if (drawImGui)
            {
                const VkImageView alternate = graph.image(target)->alternateView();
                bindViewportTexture(frame, alternate != VK_NULL_HANDLE ? alternate : graph.view(target));
            }
        }
        graph.addPass("Tools", std::move(toolAccesses), [&](VkCommandBuffer commands) {
            // Without a viewport, the tools are drawn over the tonemapped scene.
            const VkRenderingAttachmentInfo colorAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = m_swapchain->toolsImageView(imageIndex),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = toViewport ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
            };
            const VkRenderingInfo renderingInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.extent = {windowExtent.width, windowExtent.height}},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
            };
            vkCmdBeginRendering(commands, &renderingInfo);
            setViewport(commands, windowExtent);
            // Tools are drawn in display colors.
            if (ImDrawData* const drawData = drawImGui ? ImGui::GetDrawData() : nullptr;
                drawData != nullptr && drawData->Valid)
            {
                ImGui_ImplVulkan_RenderDrawData(drawData, commands);
            }
            vkCmdEndRendering(commands);
        });
    }

    graph.addPass("Present", {{backbuffer, ImageAccess::Present}}, {});
    graph.execute(commandBuffer, m_instance.isValidationEnabled());
    frame.images.endFrame();

    DEVEX_VK_TRY(vkEndCommandBuffer, commandBuffer);
    return drawCalls;
}

std::vector<PickResult> VulkanRenderer::takePickResults()
{
    return std::exchange(m_pickResults, {});
}

void VulkanRenderer::readPickResult(FrameContext& frame)
{
    if (!frame.pickRequest)
    {
        return;
    }
    std::uint32_t objectId = 0;
    std::memcpy(&objectId, frame.pickReadback->mappedBytes().data(), sizeof(objectId));
    m_pickResults.push_back({.request = *frame.pickRequest, .objectId = objectId});
    frame.pickRequest.reset();
}

core::Result<void> VulkanRenderer::uploadUi(FrameContext& frame)
{
    const std::size_t vertexBytes = std::max<std::size_t>(m_world.uiVertices.size(), 1) * sizeof(GpuUiVertex);
    const std::size_t indexBytes = std::max<std::size_t>(m_world.uiIndices.size(), 1) * sizeof(std::uint32_t);
    if (core::Result<void> ensured = ensureHostBuffer(frame.uiVertices, vertexBytes); !ensured)
    {
        return ensured;
    }
    if (core::Result<void> ensured = ensureHostBuffer(frame.uiIndices, indexBytes); !ensured)
    {
        return ensured;
    }

    const std::span<std::byte> vertices = frame.uiVertices->mappedBytes();
    for (std::size_t index = 0; index < m_world.uiVertices.size(); ++index)
    {
        const UiVertex& vertex = m_world.uiVertices[index];
        const GpuUiVertex gpuVertex{.position = vertex.position, .uv = vertex.uv, .color = vertex.color};
        std::memcpy(vertices.data() + index * sizeof(GpuUiVertex), &gpuVertex, sizeof(gpuVertex));
    }
    if (!m_world.uiIndices.empty())
    {
        std::memcpy(frame.uiIndices->mappedBytes().data(), m_world.uiIndices.data(),
                    m_world.uiIndices.size() * sizeof(std::uint32_t));
    }
    return {};
}

core::Result<VulkanRenderer::OverlayRanges> VulkanRenderer::uploadOverlay(FrameContext& frame)
{
    const std::size_t count = m_world.sceneLines.size() + m_world.overlayLines.size() + m_world.overlayTriangles.size();
    if (core::Result<void> ensured = ensureHostBuffer(frame.overlayVertices, std::max<std::size_t>(count, 1) * sizeof(GpuOverlayVertex));
        !ensured)
    {
        return std::unexpected(ensured.error());
    }
    const std::span<std::byte> bytes = frame.overlayVertices->mappedBytes();
    std::uint32_t next = 0;
    const auto append = [&](const std::vector<OverlayVertex>& vertices) {
        const std::uint32_t first = next;
        for (const OverlayVertex& vertex : vertices)
        {
            const GpuOverlayVertex gpuVertex{.position = vertex.position, .color = vertex.color};
            std::memcpy(bytes.data() + std::size_t{next} * sizeof(GpuOverlayVertex), &gpuVertex, sizeof(gpuVertex));
            ++next;
        }
        return first;
    };
    OverlayRanges ranges;
    ranges.sceneLines = append(m_world.sceneLines);
    ranges.overlayLines = append(m_world.overlayLines);
    ranges.overlayTriangles = append(m_world.overlayTriangles);
    return ranges;
}

void VulkanRenderer::bindViewportTexture(FrameContext& frame, VkImageView viewport)
{
    if (!m_imguiInitialized)
    {
        return;
    }
    if (frame.imguiViewportView != viewport)
    {
        // The previous set was last used by this context's previous frame, which has completed.
        if (frame.imguiViewport != VK_NULL_HANDLE)
        {
            ImGui_ImplVulkan_RemoveTexture(frame.imguiViewport);
        }
        frame.imguiViewport = ImGui_ImplVulkan_AddTexture(viewport, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        frame.imguiViewportView = viewport;
    }
    ImDrawData* const drawData = ImGui::GetDrawData();
    if (drawData == nullptr)
    {
        return;
    }
    const auto descriptorSet = static_cast<ImTextureID>(std::bit_cast<std::uintptr_t>(frame.imguiViewport));
    for (ImDrawList* const list : drawData->CmdLists)
    {
        for (ImDrawCmd& command : list->CmdBuffer)
        {
            if (command.TexRef._TexData == nullptr && command.TexRef._TexID == viewportTextureId)
            {
                command.TexRef._TexID = descriptorSet;
            }
        }
    }
}

RendererStats VulkanRenderer::stats() const noexcept
{
    RendererStats stats{
        .drawCalls = m_lastDrawCalls,
        .meshCount = m_meshes.size(),
        .textureCount = m_textures.size(),
        // The default material is not counted.
        .materialCount = m_materials.size() - 1,
        .lightCount = m_lastLightCount,
        .ev100 = m_ev100,
        .msaaSamples = static_cast<std::uint32_t>(m_samples),
        .swapchainExtent = m_swapchain ? m_swapchain->extent() : math::Extent2D{},
        .sceneExtent = m_sceneExtent,
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

    m_imguiColorFormat = m_swapchain->toolsFormat();
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
        // The backend's descriptor pool holds the viewport sets.
        for (FrameContext& frame : m_frames)
        {
            frame.imguiViewport = VK_NULL_HANDLE;
            frame.imguiViewportView = VK_NULL_HANDLE;
        }
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

bool VulkanRenderer::imGuiNeedsLinearColors() const noexcept
{
    const VkFormat format = m_swapchain ? m_swapchain->toolsFormat() : m_imguiColorFormat;
    return format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_R8G8B8A8_SRGB;
}

void VulkanRenderer::queueImGuiDrawData() noexcept
{
    m_imguiDrawQueued = m_imguiInitialized;
}

void VulkanRenderer::releaseRetiredResources() noexcept
{
    // Frames before the retirement may still run on the GPU; they have all completed once as
    // many frames as can be in flight have started since.
    const auto expired = [this](std::uint64_t retiredAtFrame) {
        return m_frameIndex >= retiredAtFrame + framesInFlight;
    };
    std::erase_if(m_retiredMeshes, [&](const RetiredMesh& retired) { return expired(retired.retiredAtFrame); });
    std::erase_if(m_retiredEnvironments,
                  [&](const RetiredEnvironment& retired) { return expired(retired.retiredAtFrame); });
    std::erase_if(m_retiredTextures, [&](const RetiredTexture& retired) {
        if (!expired(retired.retiredAtFrame))
        {
            return false;
        }
        // The slot must not keep pointing at a destroyed view.
        m_descriptors->setTexture(retired.texture.slot, m_whiteTexture->image.view());
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
