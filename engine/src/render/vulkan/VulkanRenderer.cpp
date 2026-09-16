#include "VulkanRenderer.hpp"

#include "Commands.hpp"
#include "GpuData.hpp"

#include <devex/core/Assert.hpp>
#include <devex/core/Log.hpp>

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

    return m_meshes.insert(GpuMesh{
        .vertices = std::move(*vertexBuffer),
        .indices = std::move(*indexBuffer),
        .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
    });
}

void VulkanRenderer::destroyMesh(MeshHandle mesh)
{
    if (std::optional<GpuMesh> removed = m_meshes.remove(mesh))
    {
        m_retiredMeshes.push_back({.mesh = std::move(*removed), .retiredAtFrame = m_frameIndex});
    }
}

RenderWorld& VulkanRenderer::beginFrame() noexcept
{
    m_world.reset();
    return m_world;
}

core::Result<void> VulkanRenderer::endFrame()
{
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
    const FrameContext& frame = m_frames[m_frameIndex % framesInFlight];
    DEVEX_VK_TRY(vkWaitForFences, device, 1, &frame.completed, VK_TRUE, noTimeout);
    releaseRetiredMeshes();

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
    writeSceneData(frame);
    if (core::Result<void> recorded = recordFrame(frame, imageIndex); !recorded)
    {
        return recorded;
    }

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

    if (!m_meshPipeline || previousFormat != m_swapchain->format())
    {
        m_meshPipeline.reset();
        core::Result<GraphicsPipeline> pipeline =
            createMeshPipeline(device, {
                                           .shaderPath = m_shaderDirectory / "mesh.spv",
                                           .colorFormat = m_swapchain->format(),
                                           .depthFormat = depthFormat,
                                       });
        if (!pipeline)
        {
            return std::unexpected(pipeline.error());
        }
        m_meshPipeline = std::move(*pipeline);
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

    // The fence of this frame context has been waited for, so no GPU work reads the buffer.
    std::memcpy(frame.sceneData->mappedBytes().data(), &scene, sizeof(scene));
}

core::Result<void> VulkanRenderer::recordFrame(const FrameContext& frame,
                                               std::uint32_t imageIndex) const
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

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_meshPipeline->handle());
    const VkDeviceAddress sceneData = frame.sceneData->deviceAddress();
    for (const MeshInstance& instance : m_world.meshes)
    {
        const GpuMesh* const mesh = m_meshes.find(instance.mesh);
        DEVEX_ASSERT_MSG(mesh != nullptr, "the render world references a destroyed mesh");
        if (mesh == nullptr)
        {
            continue;
        }

        const DrawPushConstants constants{
            .scene = sceneData,
            .vertices = mesh->vertices.deviceAddress(),
            .world = instance.transform,
        };
        vkCmdPushConstants(commandBuffer, m_meshPipeline->layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        vkCmdBindIndexBuffer(commandBuffer, mesh->indices.handle(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh->indexCount, 1, 0, 0, 0);
    }

    vkCmdEndRendering(commandBuffer);
    transitionImage(commandBuffer, backbuffer, ImageState::ColorAttachment, ImageState::Present);
    DEVEX_VK_TRY(vkEndCommandBuffer, commandBuffer);
    return {};
}

void VulkanRenderer::releaseRetiredMeshes() noexcept
{
    // Frames before the retirement may still run on the GPU; they have all completed once as
    // many frames as can be in flight have started since.
    std::erase_if(m_retiredMeshes, [this](const RetiredMesh& retired) {
        return m_frameIndex >= retired.retiredAtFrame + framesInFlight;
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
