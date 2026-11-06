#include "VulkanRenderer.hpp"

#include "Commands.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/render/PostProcess.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace devex::render::vulkan
{
namespace
{

// Coplanar grids need a margin near the horizon; ordinary lines keep their real depth.
constexpr float sceneLineDepthScale = 1.002f;
constexpr math::Vec4 outlineColor{1.0f, 0.42f, 0.05f, 1.0f};

void setViewport(VkCommandBuffer commands, math::Extent2D size)
{
    const VkViewport viewport{
        .width = static_cast<float>(size.width),
        .height = static_cast<float>(size.height),
        .maxDepth = 1.0f,
    };
    vkCmdSetViewport(commands, 0, 1, &viewport);
    const VkRect2D scissor{.offset = {0, 0}, .extent = {size.width, size.height}};
    vkCmdSetScissor(commands, 0, 1, &scissor);
}

} // namespace

// Data shared by the passes of one view. This object stays alive until graph.execute has
// recorded every callback; callbacks never borrow locals of a pass-building function.
struct VulkanRenderer::FrameGraph
{
    FrameContext& frame;
    std::uint32_t frameSlot;
    std::uint32_t imageIndex;
    ExtraView* extraView;
    RenderGraph graph;
    math::Extent2D extent{};
    math::Extent2D windowExtent{};
    VkFormat targetFormat = VK_FORMAT_UNDEFINED;
    VkFormat toolsFormat = VK_FORMAT_UNDEFINED;
    bool toViewport = false;
    bool outlines = false;
    bool drawOverlay = false;
    bool pick = false;
    bool keepShadows = false;
    bool localShadows = false;
    bool bloomed = false;
    bool focusing = false;
    bool blurring = false;
    bool lensed = false;
    bool occluded = false;
    bool drawTools = false;
    bool radianceCapture = false;
    bool capture = false;
    bool drawCascades = false;
    bool drawAtlas = false;
    bool drawUi = false;
    bool antialiased = false;
    std::vector<ExtraView*> shownViews;
    RenderGraph::ImageId backbuffer = 0;
    ImageState backbufferState = ImageState::AcquiredBackbuffer;
    std::vector<core::Result<RenderGraph::ImageId>> created;
    std::array<std::size_t, DescriptorSets::bloomLevels> bloomIndices{};
    std::array<math::Extent2D, DescriptorSets::bloomLevels> bloomExtents{};
    math::Extent2D halfExtent{};
    std::uint32_t tileSize = 0;
    math::Extent2D tileExtent{};
    std::array<std::optional<std::size_t>, DescriptorSets::postChainLength> lensIndices{};
    std::size_t lensOutputIndex = 0;
    std::size_t occlusionIndex = 0;
    std::size_t selectionMaskIndex = 0;
    std::size_t pickColorIndex = 0;
    std::size_t pickDepthIndex = 0;
    std::size_t captureIndex = 0;
    std::vector<std::size_t> surfaceIndices;
    RenderGraph::ImageId shadowMap = 0;
    const Image* shadowImage = nullptr;
    std::optional<RenderGraph::ImageId> shadowAtlas;
    RenderGraph::ImageId sceneColor = 0;
    RenderGraph::ImageId depth = 0;
    RenderGraph::ImageId velocity = 0;
    RenderGraph::ImageId normal = 0;
    RenderGraph::ImageId sceneDepth = 0;
    RenderGraph::ImageId target = 0;
    RenderGraph::ImageId historyRead = 0;
    RenderGraph::ImageId historyWrite = 0;
    RenderGraph::ImageId resolved = 0;
    RenderGraph::ImageId finished = 0;
    core::Result<OverlayRanges> overlayRanges{OverlayRanges{}};
    std::vector<std::uint32_t> surfaceIndexBases;
    std::uint32_t toolsIndexBase = 0;
    VkDeviceAddress sceneData = 0;
    VkDeviceAddress boneMatrices = 0;
    std::array<VkDescriptorSet, 2> sets{};
    std::uint32_t drawCalls = 0;
    std::uint32_t toolSlots = 0;

    FrameGraph(VulkanRenderer& renderer, FrameContext& context, std::uint32_t slot,
               std::uint32_t image, ExtraView* extra)
        : frame(context), frameSlot(slot), imageIndex(image), extraView(extra),
          graph(renderer.m_device, renderer.m_allocator, context.images)
    {
    }
};

core::Result<void> VulkanRenderer::createFrameImages(FrameGraph& view)
{
    view.backbuffer =
        view.extraView != nullptr
            ? view.graph.importImage(view.extraView->images[view.frameSlot]->handle(),
                                     view.extraView->images[view.frameSlot]->view(),
                                     view.targetFormat, view.extraView->imageStates[view.frameSlot])
            : view.graph.importImage(m_swapchain->image(view.imageIndex),
                                     m_swapchain->imageView(view.imageIndex), view.targetFormat,
                                     view.backbufferState);

    const auto create = [&](const ImageConfig& config) -> std::size_t
    {
        view.created.push_back(view.graph.createImage(config));
        return view.created.size() - 1;
    };
    // The main image keeps its shadows from a frame to the next, in images of its own; the views of
    // the tools draw theirs every time, from their own cameras.
    view.keepShadows = view.extraView == nullptr;
    if (view.keepShadows)
    {
        if (core::Result<void> ensured = ensureKeptShadows(); !ensured)
        {
            return std::unexpected(ensured.error());
        }
    }
    const std::size_t shadowMapIndex =
        view.keepShadows
            ? 0
            : create({
                  .format = depthFormat,
                  .extent = {shadowMapSize, shadowMapSize},
                  .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  .layers = cascadeCount,
              });
    // One image holds the shadows of every local light that was given a tile.
    view.localShadows = !m_shadowViews.empty();
    const std::size_t shadowAtlasIndex =
        view.localShadows && !view.keepShadows
            ? create({
                  .format = depthFormat,
                  .extent = {shadowAtlasSize, shadowAtlasSize},
                  .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              })
            : 0;
    const std::size_t sceneColorIndex = create({
        .format = sceneFormat,
        .extent = view.extent,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                 (view.frame.captureRequest && view.frame.captureRadiance
                      ? VkImageUsageFlags{VK_IMAGE_USAGE_TRANSFER_SRC_BIT}
                      : VkImageUsageFlags{0}),
    });
    const std::size_t depthIndex = create({
        .format = depthFormat,
        .extent = view.extent,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    });
    const std::size_t velocityIndex = create({
        .format = velocityFormat,
        .extent = view.extent,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    });
    const std::size_t normalIndex = create({
        .format = normalFormat,
        .extent = view.extent,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
    });
    // Every level halves the one before it, down to a few pixels.
    view.bloomed = m_world.camera.bloom > 0.0f;

    for (std::uint32_t level = 0; level < DescriptorSets::bloomLevels; ++level)
    {
        view.bloomExtents[level] = {std::max(view.extent.width >> (level + 1), 1u),
                                    std::max(view.extent.height >> (level + 1), 1u)};
        view.bloomIndices[level] =
            view.bloomed
                ? create({
                      .format = sceneFormat,
                      .extent = view.bloomExtents[level],
                      .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  })
                : 0;
    }

    // The lens, between the resolved image and the bloom: the depth of field, its circles of
    // confusion and its halved images, and the motion blur, its tiles; not for the captures of
    // light.
    view.focusing = !m_radianceView && m_world.camera.depthOfField > 0.0f &&
                    m_world.camera.aperture > 0.0f &&
                    m_world.camera.projection == Projection::Perspective;
    view.blurring = !m_radianceView && m_world.camera.motionBlur > 0.0f;
    view.lensed = view.focusing || view.blurring;
    view.halfExtent = {std::max((view.extent.width + 1) / 2, 1u),
                       std::max((view.extent.height + 1) / 2, 1u)};
    // A tile is as wide as the longest blur: a fifty-sixth of the height of the image.
    view.tileSize = static_cast<std::uint32_t>(
        std::max(8.0f, std::round(static_cast<float>(view.extent.height) / 56.0f)));
    view.tileExtent = {(view.extent.width + view.tileSize - 1) / view.tileSize,
                       (view.extent.height + view.tileSize - 1) / view.tileSize};
    constexpr VkImageUsageFlags drawnAndRead =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    view.lensOutputIndex =
        view.lensed ? create({.format = sceneFormat, .extent = view.extent, .usage = drawnAndRead})
                    : 0;

    if (view.focusing)
    {
        view.lensIndices[0] =
            create({.format = confusionFormat, .extent = view.extent, .usage = drawnAndRead});
        for (std::size_t index = 1; index <= 3; ++index)
        {
            view.lensIndices[index] =
                create({.format = sceneFormat, .extent = view.halfExtent, .usage = drawnAndRead});
        }
        if (view.blurring)
        {
            view.lensIndices[4] =
                create({.format = sceneFormat, .extent = view.extent, .usage = drawnAndRead});
        }
    }
    if (view.blurring)
    {
        view.lensIndices[5] =
            create({.format = tileMotionFormat, .extent = view.tileExtent, .usage = drawnAndRead});
        view.lensIndices[6] =
            create({.format = tileMotionFormat, .extent = view.tileExtent, .usage = drawnAndRead});
    }

    view.occluded = m_world.camera.ambientOcclusion > 0.0f;
    view.occlusionIndex =
        view.occluded
            ? create({
                  .format = occlusionFormat,
                  .extent = view.extent,
                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              })
            : 0;
    view.selectionMaskIndex =
        view.outlines
            ? create({
                  .format = selectionMaskFormat,
                  .extent = view.extent,
                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              })
            : 0;
    // The tools sample the viewport image in the format they draw in.
    view.toolsFormat = m_swapchain->toolsFormat();
    const std::size_t viewportIndex =
        view.toViewport && view.extraView == nullptr
            ? create({
                  .format = view.targetFormat,
                  .extent = view.extent,
                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  .alternateFormat = view.toolsFormat != view.targetFormat ? view.toolsFormat
                                                                           : VK_FORMAT_UNDEFINED,
              })
            : 0;
    // The interface surfaces, for the tools only, sampled by them as the viewport is.
    view.drawTools =
        view.extraView == nullptr && !m_world.toolsDraws.empty() && !m_world.toolsIndices.empty();

    if (view.drawTools)
    {
        for (const UiSurface& surface : m_world.uiSurfaces)
        {
            view.surfaceIndices.push_back(create({
                .format = view.targetFormat,
                .extent = {std::max(surface.size.width, 1u), std::max(surface.size.height, 1u)},
                .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                .alternateFormat =
                    view.toolsFormat != view.targetFormat ? view.toolsFormat : VK_FORMAT_UNDEFINED,
            }));
        }
    }
    view.pickColorIndex =
        view.pick
            ? create({
                  .format = pickFormat,
                  .extent = view.frame.pickExtent,
                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              })
            : 0;
    view.pickDepthIndex = view.pick ? create({
                                          .format = depthFormat,
                                          .extent = view.frame.pickExtent,
                                          .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                                      })
                                    : 0;
    // A capture tonemaps the scene a second time, into a small image copied to the CPU; that of a
    // radiance view copies the light of the scene as it was drawn.
    view.radianceCapture = view.frame.captureRequest.has_value() && view.frame.captureRadiance;
    view.capture = view.frame.captureRequest.has_value() && !view.frame.captureRadiance;
    view.frame.captureFormat = view.targetFormat;
    view.captureIndex =
        view.capture
            ? create({
                  .format = view.targetFormat,
                  .extent = view.frame.captureExtent,
                  .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              })
            : 0;
    for (const core::Result<RenderGraph::ImageId>& image : view.created)
    {
        if (!image)
        {
            return std::unexpected(image.error());
        }
    }
    view.shadowMap = view.keepShadows
                         ? view.graph.importImage(m_keptCascades->handle(), m_keptCascades->view(),
                                                  depthFormat, m_keptCascadesState)
                         : *view.created[shadowMapIndex];
    view.shadowImage = view.keepShadows ? &*m_keptCascades : view.graph.image(view.shadowMap);
    view.shadowAtlas =
        !view.localShadows ? std::nullopt
        : view.keepShadows
            ? std::optional(view.graph.importImage(m_keptAtlas->handle(), m_keptAtlas->view(),
                                                   depthFormat, m_keptAtlasState))
            : std::optional(*view.created[shadowAtlasIndex]);
    view.sceneColor = *view.created[sceneColorIndex];
    view.depth = *view.created[depthIndex];
    view.velocity = *view.created[velocityIndex];
    view.normal = *view.created[normalIndex];
    view.sceneDepth = view.depth;
    // A view is drawn straight into its image, imported as the backbuffer is.
    view.target = view.toViewport && view.extraView == nullptr ? *view.created[viewportIndex]
                                                               : view.backbuffer;

    return {};
}

core::Result<void> VulkanRenderer::prepareFrameGraph(FrameGraph& view, bool drawShadows)
{
    view.extent = m_sceneExtent;
    view.windowExtent = m_swapchain->extent();
    view.targetFormat = m_swapchain->format();
    // A view draws into its own image, without what the tools draw over the scene.
    view.toViewport =
        view.extraView != nullptr || (m_world.viewport.width > 0 && m_world.viewport.height > 0);
    view.outlines = view.extraView == nullptr &&
                    (std::ranges::any_of(m_world.meshes, &MeshInstance::outlined) ||
                     std::ranges::any_of(m_world.sprites, &RenderSprite::outlined) ||
                     std::ranges::any_of(m_world.tilemaps, &RenderTilemap::outlined));
    view.drawOverlay = view.extraView == nullptr &&
                       (view.outlines || !m_world.sceneLines.empty() ||
                        !m_world.overlayLines.empty() || !m_world.overlayTriangles.empty());
    // The views drawn this frame, which the main frame shows; the interface goes over the one that
    // asks for it.

    if (view.extraView == nullptr)
    {
        for (const std::unique_ptr<ExtraView>& drawn : m_extraViews)
        {
            if (drawn->drawnFrame == m_frameIndex)
            {
                view.shownViews.push_back(drawn.get());
            }
        }
    }
    const bool interfaceElsewhere = std::ranges::any_of(view.shownViews, &ExtraView::interface);
    view.pick = view.frame.pickRequest.has_value();
    m_culledInstances = 0;
    // The meshes and textures created since the previous frame, before anything draws them.
    if (!view.frame.uploads.empty())
    {
        view.graph.addPass("Uploads", {},
                           [&](VkCommandBuffer commands) { recordUploads(commands, view.frame); });
    }
    // The tables of the air and the light of its sky, before anything samples them.
    if (m_world.atmosphere.enabled)
    {
        view.graph.addPass("Atmosphere", {},
                           [&](VkCommandBuffer commands)
                           {
                               m_atmosphere->record(commands, m_descriptors->global(),
                                                    view.frame.atmosphere->deviceAddress(),
                                                    view.frame.sceneData->deviceAddress(), m_airKey,
                                                    m_skyKey, m_viewKey);
                           });
    }

    if (core::Result<void> made = createFrameImages(view); !made)
    {
        return made;
    }
    // Shadows drawn from the same instances, materials and views of the lights as the ones kept are
    // the ones kept: the editor at rest, or a scene whose camera alone moves for the local lights.
    view.drawCascades = drawShadows;
    view.drawAtlas = view.localShadows;
    if (view.keepShadows)
    {
        const std::optional<std::uint64_t> casters = shadowCastersKey();
        const auto keyed = [&casters](const void* data,
                                      std::size_t size) -> std::optional<std::uint64_t>
        {
            if (!casters)
            {
                return std::nullopt;
            }
            std::uint64_t hash = *casters;
            const auto* const bytes = static_cast<const std::byte*>(data);
            for (std::size_t index = 0; index < size; ++index)
            {
                hash = (hash ^ static_cast<std::uint64_t>(bytes[index])) * 1099511628211ull;
            }
            return hash;
        };
        m_keptShadowPasses = 0;
        if (view.drawCascades)
        {
            const std::optional<std::uint64_t> key =
                keyed(m_cascadeViewProjections.data(), sizeof(m_cascadeViewProjections));
            view.drawCascades = !key || key != m_keptCascadesKey;
            m_keptShadowPasses += view.drawCascades ? 0u : 1u;
            m_keptCascadesKey = key;
        }
        if (view.drawAtlas)
        {
            const std::optional<std::uint64_t> key =
                keyed(m_shadowViews.data(), m_shadowViews.size() * sizeof(GpuShadowView));
            view.drawAtlas = !key || key != m_keptAtlasKey;
            m_keptShadowPasses += view.drawAtlas ? 0u : 1u;
            m_keptAtlasKey = key;
        }
    }
    view.overlayRanges = OverlayRanges{};
    if (view.drawOverlay)
    {
        view.overlayRanges = uploadOverlay(view.frame);
        if (!view.overlayRanges)
        {
            return std::unexpected(view.overlayRanges.error());
        }
    }

    view.drawUi = !m_world.uiDraws.empty() && !m_world.uiIndices.empty() &&
                  (view.extraView != nullptr ? view.extraView->interface : !interfaceElsewhere);

    view.toolsIndexBase = 0;
    if (view.drawUi || view.drawTools)
    {
        if (core::Result<void> uploaded =
                uploadUi(view.frame, view.drawTools, view.surfaceIndexBases, view.toolsIndexBase);
            !uploaded)
        {
            return std::unexpected(uploaded.error());
        }
    }

    bindFrameImages(view);
    view.sceneData = view.frame.sceneData->deviceAddress();
    view.boneMatrices = view.frame.bones ? view.frame.bones->deviceAddress() : 0;
    view.sets = {m_descriptors->global(), frameDescriptors(view.frameSlot)};
    view.drawCalls = 0;
    if (core::Result<void> uploaded = uploadPreviousBones(view.frame); !uploaded)
    {
        return std::unexpected(uploaded.error());
    }
    {
        DEVEX_PROFILE_SCOPE("Culling");
        if (core::Result<void> prepared =
                prepareCulling(view.frame, view.drawCascades, view.drawAtlas, view.pick);
            !prepared)
        {
            return std::unexpected(prepared.error());
        }
    }
    return {};
}

void VulkanRenderer::bindFrameImages(FrameGraph& view)
{
    // The antialiasing reads what the previous frame resolved and writes what this one resolves;
    // the two swap every frame, which the frame slot already alternates with.
    view.antialiased = m_world.camera.antialiasing == Antialiasing::Temporal;
    const std::size_t writeSlot = view.frameSlot % m_history.size();
    const std::size_t readSlot = (writeSlot + 1) % m_history.size();
    view.historyRead =
        view.graph.importImage(m_history[readSlot]->handle(), m_history[readSlot]->view(),
                               sceneFormat, m_historyStates[readSlot]);
    view.historyWrite =
        view.graph.importImage(m_history[writeSlot]->handle(), m_history[writeSlot]->view(),
                               sceneFormat, m_historyStates[writeSlot]);

    // The frame set points at this frame's transient images.
    const VkImageView selectionMaskView =
        view.outlines ? view.graph.view(*view.created[view.selectionMaskIndex])
                      : m_emptySelectionMask->view();
    DescriptorSets::FrameImages images{
        .shadowMap = view.graph.view(view.shadowMap),
        .sceneColor = view.graph.view(view.sceneColor),
        .selectionMask = selectionMaskView,
        .velocity = view.graph.view(view.velocity),
        .normal = view.graph.view(view.normal),
        .ambientOcclusion = view.occluded ? view.graph.view(*view.created[view.occlusionIndex])
                                          : m_whiteImage->view(),
        .history = m_history[readSlot]->view(),
        .depth = view.graph.view(view.depth),
        .resolved = view.lensed        ? view.graph.view(*view.created[view.lensOutputIndex])
                    : view.antialiased ? m_history[writeSlot]->view()
                                       : view.graph.view(view.sceneColor),
        .localShadowMap =
            view.shadowAtlas ? view.graph.view(*view.shadowAtlas) : m_emptyShadowAtlas->view(),
        .postSource =
            view.antialiased ? m_history[writeSlot]->view() : view.graph.view(view.sceneColor),
        .screenReflection = m_screenReflections->view(),
    };
    for (std::uint32_t level = 0; level < DescriptorSets::bloomLevels; ++level)
    {
        images.bloom[level] = view.bloomed
                                  ? view.graph.view(*view.created[view.bloomIndices[level]])
                                  : m_whiteGeneralImage->view();
    }
    for (std::size_t index = 0; index < DescriptorSets::postChainLength; ++index)
    {
        images.post[index] = view.lensIndices[index]
                                 ? view.graph.view(*view.created[*view.lensIndices[index]])
                                 : m_whiteGeneralImage->view();
    }
    if (view.frame.boundImages != images)
    {
        view.frame.boundImages = images;
        m_descriptors->setFrameImages(m_setBase + view.frameSlot, images);
    }
}

void VulkanRenderer::addShadowPasses(FrameGraph& view)
{
    if (!m_skinJobs.empty())
    {
        view.graph.addPass("Skinning", {},
                           [&](VkCommandBuffer commands) { recordSkinning(commands); });
    }
    if (m_culledCount > 0)
    {
        view.graph.addPass("Culling", {},
                           [&](VkCommandBuffer commands) { recordCulling(commands, view.frame); });
    }

    if (view.drawCascades)
    {
        view.graph.addPass("Shadow cascades", {{view.shadowMap, ImageAccess::DepthAttachment}},
                           [&](VkCommandBuffer commands)
                           {
                               for (std::uint32_t cascade = 0; cascade < cascadeCount; ++cascade)
                               {
                                   const VkRenderingAttachmentInfo depthAttachment{
                                       .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                                       .imageView = view.shadowImage->subview(VK_IMAGE_VIEW_TYPE_2D,
                                                                              0, 1, cascade, 1),
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
                                   drawMeshes(commands, view.sceneData, view.boneMatrices,
                                              MeshPass::Shadow, cascade, view.frameSlot);
                                   drawCulled(commands, view.frame, view.sceneData,
                                              MeshPass::Shadow, cascade, view.frameSlot);
                                   drawTerrains(commands, view.sceneData, MeshPass::Shadow, cascade,
                                                view.frameSlot);
                                   vkCmdEndRendering(commands);
                               }
                           });
    }

    if (view.drawAtlas)
    {
        const RenderGraph::ImageId atlas = *view.shadowAtlas;
        view.graph.addPass(
            "Local shadows", {{atlas, ImageAccess::DepthAttachment}},
            [&, atlas](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo depthAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(atlas),
                    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = {.depthStencil = {.depth = 1.0f}},
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {shadowAtlasSize, shadowAtlasSize}},
                    .layerCount = 1,
                    .pDepthAttachment = &depthAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                for (std::uint32_t lightView = 0; lightView < m_shadowViews.size(); ++lightView)
                {
                    // Each lightView draws into its own tile, and nowhere else.
                    const math::Vec4& tile = m_shadowViews[lightView].tile;
                    const auto size = static_cast<float>(shadowAtlasSize);
                    const VkViewport viewport{
                        .x = tile.x * size,
                        .y = tile.y * size,
                        .width = tile.z * size,
                        .height = tile.w * size,
                        .maxDepth = 1.0f,
                    };
                    const VkRect2D scissor{
                        .offset = {static_cast<std::int32_t>(tile.x * size),
                                   static_cast<std::int32_t>(tile.y * size)},
                        .extent = {static_cast<std::uint32_t>(tile.z * size),
                                   static_cast<std::uint32_t>(tile.w * size)},
                    };
                    vkCmdSetViewport(commands, 0, 1, &viewport);
                    vkCmdSetScissor(commands, 0, 1, &scissor);
                    vkCmdSetDepthBias(commands, 0.0f, 0.0f, 2.0f);
                    view.drawCalls += drawMeshes(commands, view.sceneData, view.boneMatrices,
                                                 MeshPass::LocalShadow, lightView, view.frameSlot);
                    view.drawCalls += drawCulled(commands, view.frame, view.sceneData,
                                                 MeshPass::LocalShadow, lightView, view.frameSlot);
                    view.drawCalls += drawTerrains(commands, view.sceneData, MeshPass::LocalShadow,
                                                   lightView, view.frameSlot);
                }
                vkCmdEndRendering(commands);
            });
    }
}

void VulkanRenderer::drawDepthPrepass(FrameGraph& view, VkCommandBuffer commands, bool recover)
{
    const VkDeviceAddress previousBones =
        view.frame.previousBones ? view.frame.previousBones->deviceAddress() : 0;

    const VkAttachmentLoadOp load =
        recover ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
    const std::array<VkRenderingAttachmentInfo, 2> colorAttachments{
        VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view.graph.view(view.velocity),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = load,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}},
        },
        VkRenderingAttachmentInfo{
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = view.graph.view(view.normal),
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = load,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}},
        },
    };
    const VkRenderingAttachmentInfo depthAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view.graph.view(view.depth),
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .loadOp = load,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.depthStencil = {.depth = 0.0f}},
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = {view.extent.width, view.extent.height}},
        .layerCount = 1,
        .colorAttachmentCount = static_cast<std::uint32_t>(colorAttachments.size()),
        .pColorAttachments = colorAttachments.data(),
        .pDepthAttachment = &depthAttachment,
    };
    vkCmdBeginRendering(commands, &renderingInfo);
    setViewport(commands, view.extent);
    if (!recover)
    {
        view.drawCalls += drawMeshes(commands, view.sceneData, view.boneMatrices, MeshPass::Prepass,
                                     0, view.frameSlot, previousBones);
        view.drawCalls +=
            drawTerrains(commands, view.sceneData, MeshPass::Prepass, 0, view.frameSlot);
    }
    view.drawCalls += drawCulled(commands, view.frame, view.sceneData, MeshPass::Prepass,
                                 recover ? 1u : 0u, view.frameSlot);
    vkCmdEndRendering(commands);
}

void VulkanRenderer::addDepthPasses(FrameGraph& view)
{
    view.graph.addPass("Prepass",
                       {{view.velocity, ImageAccess::ColorAttachment},
                        {view.normal, ImageAccess::ColorAttachment},
                        {view.depth, ImageAccess::DepthAttachment}},
                       [&](VkCommandBuffer commands) { drawDepthPrepass(view, commands, false); });
    if (m_occlusionActive)
    {
        view.graph.addPass("Depth pyramid", {{view.depth, ImageAccess::ComputeRead}},
                           [&](VkCommandBuffer commands)
                           { recordDepthPyramid(commands, view.frame, view.frameSlot); });
        view.graph.addPass("Occlusion culling", {}, [&](VkCommandBuffer commands)
                           { recordOcclusion(commands, view.frame, view.frameSlot); });
        view.graph.addPass("Recover visibility",
                           {{view.velocity, ImageAccess::ColorAttachment},
                            {view.normal, ImageAccess::ColorAttachment},
                            {view.depth, ImageAccess::DepthAttachment}},
                           [&](VkCommandBuffer commands)
                           { drawDepthPrepass(view, commands, true); });
    }
    if (m_culledCount > 0)
    {
        view.graph.addPass("Visibility results", {}, [&](VkCommandBuffer commands)
                           { recordCullingReadback(commands, view.frame); });
    }
}

void VulkanRenderer::addLightingPasses(FrameGraph& view)
{
    if (view.occluded)
    {
        const RenderGraph::ImageId occlusion = *view.created[view.occlusionIndex];
        view.graph.addPass(
            "Ambient occlusion",
            {{view.depth, ImageAccess::FragmentRead},
             {view.normal, ImageAccess::FragmentRead},
             {occlusion, ImageAccess::ColorAttachment}},
            // The pass runs once the graph replays it, when this block is long gone: the
            // image it draws into is taken by value.
            [&, occlusion](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo colorAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(occlusion),
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {view.extent.width, view.extent.height}},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                setViewport(commands, view.extent);
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_aoPipeline->handle());
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_aoPipeline->layout(), 0, 2, view.sets.data(), 0, nullptr);
                const AoPushConstants constants{
                    .scene = view.sceneData,
                    .radius = std::max(m_world.camera.ambientOcclusionRadius, 0.01f),
                    .intensity = std::clamp(m_world.camera.ambientOcclusion, 0.0f, 1.0f),
                    .frame = static_cast<float>(m_frameIndex % taaSampleCount),
                };
                vkCmdPushConstants(commands, m_aoPipeline->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(constants), &constants);
                vkCmdDraw(commands, 3, 1, 0, 0);
                vkCmdEndRendering(commands);
            });
    }

    // The volumetric fog of the main view, lit through the shadows of the sun, before the surfaces
    // and the sky read it.
    if (m_volumetricFogThisFrame && view.extraView == nullptr)
    {
        view.graph.addPass("Volumetric fog", {{view.shadowMap, ImageAccess::ComputeRead}},
                           [&](VkCommandBuffer commands)
                           {
                               m_volumetricFog->record(commands, view.sets[0], view.sets[1],
                                                       view.sceneData,
                                                       view.frame.volumetricFog->deviceAddress());
                           });
    }
}

void VulkanRenderer::addScenePasses(FrameGraph& view)
{
    // Surfaces can reflect the screen, which reads the depth while they test it, and the normals.
    // Among them only the grass writes the depth, in a pass of its own.
    std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> sceneAccesses{
        {view.sceneColor, ImageAccess::ColorAttachment},
        {view.depth, ImageAccess::DepthReadOnly},
        {view.normal, ImageAccess::FragmentRead},
        {view.shadowMap, ImageAccess::FragmentRead},
    };
    if (view.localShadows)
    {
        sceneAccesses.push_back({*view.shadowAtlas, ImageAccess::FragmentRead});
    }
    if (view.occluded)
    {
        sceneAccesses.push_back({*view.created[view.occlusionIndex], ImageAccess::FragmentRead});
    }
    view.graph.addPass(
        "Scene", std::move(sceneAccesses),
        [&](VkCommandBuffer commands)
        {
            const VkRenderingAttachmentInfo colorAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = view.graph.view(view.sceneColor),
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 1.0f}}},
            };
            // With reversed depth, 0 is infinitely far away.
            const VkRenderingAttachmentInfo depthAttachment{
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = view.graph.view(view.depth),
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_NONE,
            };
            const VkRenderingInfo renderingInfo{
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {.extent = {view.extent.width, view.extent.height}},
                .layerCount = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments = &colorAttachment,
                .pDepthAttachment = &depthAttachment,
            };
            vkCmdBeginRendering(commands, &renderingInfo);
            setViewport(commands, view.extent);
            view.drawCalls += drawMeshes(commands, view.sceneData, view.boneMatrices,
                                         MeshPass::Scene, 0, view.frameSlot);
            view.drawCalls += drawCulled(commands, view.frame, view.sceneData, MeshPass::Scene, 0,
                                         view.frameSlot);
            view.drawCalls +=
                drawTerrains(commands, view.sceneData, MeshPass::Scene, 0, view.frameSlot);

            // A sky shader of the project draws the sky in place of the texture; an atmosphere
            // draws its own.
            const GpuShader* const skyShader =
                m_world.atmosphere.enabled
                    ? nullptr
                    : shaderOf(m_world.environment.skyMaterial, asset::ShaderKind::Sky);
            const Pipeline& skyPipeline =
                skyShader != nullptr && skyShader->scene ? *skyShader->scene : *m_skyPipeline;
            vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline.handle());
            vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline.layout(),
                                    0, 1, view.sets.data(), 0, nullptr);
            const SkyPushConstants sky{.scene = view.sceneData,
                                       .material = materialIndex(m_world.environment.skyMaterial)};
            vkCmdPushConstants(commands, skyPipeline.layout(),
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                               sizeof(sky), &sky);
            vkCmdDraw(commands, 3, 1, 0, 0);

            vkCmdEndRendering(commands);
        });

    if (!m_grassTiles.empty())
    {
        view.graph.addPass(
            "Grass",
            {{view.sceneColor, ImageAccess::ColorAttachment},
             {view.depth, ImageAccess::DepthAttachment}},
            [&](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo colorAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(view.sceneColor),
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                };
                const VkRenderingAttachmentInfo depthAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(view.depth),
                    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {view.extent.width, view.extent.height}},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                    .pDepthAttachment = &depthAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                setViewport(commands, view.extent);
                view.drawCalls += drawGrass(commands, view.sceneData, view.frameSlot);
                vkCmdEndRendering(commands);
            });
    }

    // The blended surfaces and the particles come last, over the sky as over everything else. They
    // test the depth without writing it, which lets particles read it to fade into surfaces.
    std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> transparentAccesses{
        {view.sceneColor, ImageAccess::ColorAttachment},
        {view.depth, ImageAccess::DepthReadOnly},
        {view.normal, ImageAccess::FragmentRead},
        {view.shadowMap, ImageAccess::FragmentRead},
    };
    if (view.localShadows)
    {
        transparentAccesses.push_back({*view.shadowAtlas, ImageAccess::FragmentRead});
    }
    if (view.occluded)
    {
        transparentAccesses.push_back(
            {*view.created[view.occlusionIndex], ImageAccess::FragmentRead});
    }
    view.graph.addPass("Transparent", std::move(transparentAccesses),
                       [&](VkCommandBuffer commands)
                       {
                           const VkRenderingAttachmentInfo colorAttachment{
                               .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                               .imageView = view.graph.view(view.sceneColor),
                               .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                               .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                           };
                           const VkRenderingAttachmentInfo depthAttachment{
                               .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                               .imageView = view.graph.view(view.depth),
                               .imageLayout = VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL,
                               .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                               .storeOp = VK_ATTACHMENT_STORE_OP_NONE,
                           };
                           const VkRenderingInfo renderingInfo{
                               .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                               .renderArea = {.extent = {view.extent.width, view.extent.height}},
                               .layerCount = 1,
                               .colorAttachmentCount = 1,
                               .pColorAttachments = &colorAttachment,
                               .pDepthAttachment = &depthAttachment,
                           };
                           vkCmdBeginRendering(commands, &renderingInfo);
                           setViewport(commands, view.extent);
                           view.drawCalls +=
                               drawTransparent(commands, view.sceneData, view.boneMatrices,
                                               view.frame, view.frameSlot);
                           vkCmdEndRendering(commands);
                       });

    if (view.radianceCapture)
    {
        view.graph.addPass(
            "Radiance readback", {{view.sceneColor, ImageAccess::TransferRead}},
            [&](VkCommandBuffer commands)
            {
                const VkBufferImageCopy copy{
                    .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                    .imageExtent = {view.extent.width, view.extent.height, 1},
                };
                vkCmdCopyImageToBuffer(commands, view.graph.handle(view.sceneColor),
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       view.frame.radianceReadback->handle(), 1, &copy);
            });
    }

    if (m_screenReflectionsThisFrame && view.extraView == nullptr)
    {
        view.graph.addPass("Screen reflections", {{view.sceneColor, ImageAccess::ComputeRead}},
                           [&](VkCommandBuffer commands)
                           {
                               m_screenReflections->record(commands, view.sets[0], view.sets[1],
                                                           view.frame.exposure);
                           });
    }

    if (m_world.camera.autoExposure)
    {
        view.graph.addPass("Luminance", {{view.sceneColor, ImageAccess::ComputeRead}},
                           [&](VkCommandBuffer commands)
                           {
                               vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                 m_luminancePipeline->handle());
                               vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE,
                                                       m_luminancePipeline->layout(), 0, 2,
                                                       view.sets.data(), 0, nullptr);
                               const LuminancePushConstants constants{
                                   .output = view.frame.luminance->deviceAddress(),
                                   .width = luminanceGridWidth,
                                   .height = luminanceGridHeight,
                               };
                               vkCmdPushConstants(commands, m_luminancePipeline->layout(),
                                                  VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                                                  &constants);
                               vkCmdDispatch(commands, (luminanceGridWidth + 7) / 8,
                                             (luminanceGridHeight + 7) / 8, 1);
                           });
        view.frame.luminanceMeasured = true;
    }
}

void VulkanRenderer::addPickPasses(FrameGraph& view)
{
    if (view.pick)
    {
        const RenderGraph::ImageId pickColor = *view.created[view.pickColorIndex];
        const RenderGraph::ImageId pickDepth = *view.created[view.pickDepthIndex];
        view.graph.addPass(
            "Pick",
            {{pickColor, ImageAccess::ColorAttachment}, {pickDepth, ImageAccess::DepthAttachment}},
            [&, pickColor, pickDepth](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo colorAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(pickColor),
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = {.color = {.uint32 = {0, 0, 0, 0}}},
                };
                const VkRenderingAttachmentInfo depthAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(pickDepth),
                    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                    .clearValue = {.depthStencil = {.depth = 0.0f}},
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {view.frame.pickExtent.width,
                                              view.frame.pickExtent.height}},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                    .pDepthAttachment = &depthAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                setViewport(commands, view.frame.pickExtent);
                drawMeshes(commands, view.sceneData, view.boneMatrices, MeshPass::Pick, 0,
                           view.frameSlot);
                drawCulled(commands, view.frame, view.sceneData, MeshPass::Pick, 0, view.frameSlot);
                drawTerrains(commands, view.sceneData, MeshPass::Pick, 0, view.frameSlot);
                static_cast<void>(drawSprites(commands, view.sceneData, view.frame, view.frameSlot,
                                              SpritePass::Pick, 0,
                                              static_cast<std::uint32_t>(m_gpuSprites.size())));
                vkCmdEndRendering(commands);
            });
        view.graph.addPass(
            "Pick readback", {{pickColor, ImageAccess::TransferRead}},
            [&, pickColor](VkCommandBuffer commands)
            {
                const VkBufferImageCopy copy{
                    .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                    .imageExtent = {view.frame.pickExtent.width, view.frame.pickExtent.height, 1},
                };
                vkCmdCopyImageToBuffer(commands, view.graph.handle(pickColor),
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       view.frame.pickReadback->handle(), 1, &copy);
            });
    }

    if (view.outlines)
    {
        const RenderGraph::ImageId selectionMask = *view.created[view.selectionMaskIndex];
        view.graph.addPass(
            "Selection mask", {{selectionMask, ImageAccess::ColorAttachment}},
            [&, selectionMask](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo colorAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(selectionMask),
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue = {.color = {.float32 = {0.0f, 0.0f, 0.0f, 0.0f}}},
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {view.extent.width, view.extent.height}},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                setViewport(commands, view.extent);
                drawMeshes(commands, view.sceneData, view.boneMatrices, MeshPass::SelectionMask, 0,
                           view.frameSlot);
                // Runs of outlined sprites, in their order.
                const auto outlinedCount = static_cast<std::uint32_t>(m_spriteOutlined.size());
                for (std::uint32_t position = 0; position < outlinedCount;)
                {
                    if (m_spriteOutlined[position] == 0)
                    {
                        ++position;
                        continue;
                    }
                    std::uint32_t end = position + 1;
                    while (end < outlinedCount && m_spriteOutlined[end] != 0)
                    {
                        ++end;
                    }
                    static_cast<void>(drawSprites(commands, view.sceneData, view.frame,
                                                  view.frameSlot, SpritePass::Mask, position,
                                                  end - position));
                    position = end;
                }
                vkCmdEndRendering(commands);
            });
    }
}

void VulkanRenderer::addAntialiasingPass(FrameGraph& view)
{
    if (view.antialiased)
    {
        view.graph.addPass(
            "Antialiasing",
            {{view.sceneColor, ImageAccess::FragmentRead},
             {view.depth, ImageAccess::FragmentRead},
                            {view.velocity, ImageAccess::FragmentRead},
             {view.historyRead, ImageAccess::FragmentRead},
             {view.historyWrite, ImageAccess::ColorAttachment}},
            [&](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo colorAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(view.historyWrite),
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {view.extent.width, view.extent.height}},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                setViewport(commands, view.extent);
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_taaPipeline->handle());
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_taaPipeline->layout(), 0, 2, view.sets.data(), 0,
                                        nullptr);
                // The scene combines physical light and exposure-relative sprites/emission: scaling
                // old colour by the exposure ratio would change those relative colours. Start fresh
                // after an abrupt EV change (over a quarter stop), while retaining normal
                // adaptation.
                constexpr float exposureLimit = 1.1892071f; // 2^(1/4)
                const float exposureRatio = view.frame.exposure / m_historyExposure;
                const bool historyValid = m_historyValid && exposureRatio >= 1.0f / exposureLimit &&
                                          exposureRatio <= exposureLimit;
                const TaaPushConstants constants{
                    .blend = taaBlend,
                    .historyValid = historyValid ? 1.0f : 0.0f,
                    .texelWidth = 1.0f / static_cast<float>(view.extent.width),
                    .texelHeight = 1.0f / static_cast<float>(view.extent.height),
                    .scene = view.frame.sceneData->deviceAddress(),
                    .previousSkyViewProjection = m_previousSkyViewProjection,
                };
                vkCmdPushConstants(commands, m_taaPipeline->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(constants), &constants);
                vkCmdDraw(commands, 3, 1, 0, 0);
                vkCmdEndRendering(commands);
            });
    }
}

void VulkanRenderer::addLensPasses(FrameGraph& view)
{
    // What the antialiasing resolved, then what the lens made of it: the image the bloom and the
    // tonemapping read.
    view.resolved = view.antialiased ? view.historyWrite : view.sceneColor;
    view.finished = view.lensed ? *view.created[view.lensOutputIndex] : view.resolved;
    if (view.lensed)
    {
        // As the bloom's, the images of the chain are named by one descriptor: all in the general
        // layout before any pass binds it.
        std::array<std::optional<RenderGraph::ImageId>, DescriptorSets::postChainLength> chain{};
        std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> chainAccesses;
        for (std::size_t index = 0; index < chain.size(); ++index)
        {
            if (view.lensIndices[index])
            {
                chain[index] = *view.created[*view.lensIndices[index]];
                chainAccesses.push_back({*chain[index], ImageAccess::GeneralRead});
            }
        }
        view.graph.addPass("Lens chain", std::move(chainAccesses), {});
        // A full-frame sensor, 24 mm high, behind a lens whose focal length gives the field of
        // view.
        const float focalLength =
            0.012f / std::tan(std::max(m_world.camera.verticalFov, 1e-3f) * 0.5f);
        const LensPushConstants base{
            .scene = view.sceneData,
            .imageWidth = static_cast<float>(view.extent.width),
            .imageHeight = static_cast<float>(view.extent.height),
            .aperture = focalLength / std::max(m_world.camera.aperture, 0.5f),
            .focalLength = focalLength,
            .focusDistance = std::max(m_world.camera.focusDistance, 0.01f),
            .amount = std::clamp(m_world.camera.depthOfField, 0.0f, 1.0f),
            .maxRadius = static_cast<float>(view.extent.height) / 60.0f,
            .autoFocus = m_world.camera.autoFocus ? 1u : 0u,
            .shutter = std::clamp(m_world.camera.motionBlur, 0.0f, 1.0f),
            .tileSize = static_cast<float>(view.tileSize),
            .fromDepthOfField = view.focusing ? 1u : 0u,
            .frame = static_cast<std::uint32_t>(m_frameIndex),
        };
        // A full-screen pass into one image: of the chain, in the general layout, or the output.
        const auto lensPass =
            [&](const char* name, const Pipeline& pipeline, RenderGraph::ImageId into,
                math::Extent2D size, bool general,
                std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> accesses)
        {
            accesses.push_back(
                {into, general ? ImageAccess::GeneralAttachment : ImageAccess::ColorAttachment});
            LensPushConstants constants = base;
            constants.targetWidth = static_cast<float>(size.width);
            constants.targetHeight = static_cast<float>(size.height);
            view.graph.addPass(
                name, std::move(accesses),
                [&view, used = &pipeline, into, size, general, constants](VkCommandBuffer commands)
                {
                    const VkRenderingAttachmentInfo colorAttachment{
                        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = view.graph.view(into),
                        .imageLayout = general ? VK_IMAGE_LAYOUT_GENERAL
                                               : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    };
                    const VkRenderingInfo renderingInfo{
                        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                        .renderArea = {.extent = {size.width, size.height}},
                        .layerCount = 1,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &colorAttachment,
                    };
                    vkCmdBeginRendering(commands, &renderingInfo);
                    setViewport(commands, size);
                    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, used->handle());
                    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            used->layout(), 0, 2, view.sets.data(), 0, nullptr);
                    vkCmdPushConstants(commands, used->layout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(constants), &constants);
                    vkCmdDraw(commands, 3, 1, 0, 0);
                    vkCmdEndRendering(commands);
                });
        };
        const RenderGraph::ImageId lensOutput = *view.created[view.lensOutputIndex];
        if (view.focusing)
        {
            lensPass("Depth of field", *m_circleOfConfusionPipeline, *chain[0], view.extent, true,
                     {{view.depth, ImageAccess::FragmentRead}});
            lensPass("Depth of field", *m_dofPrefilterPipeline, *chain[1], view.halfExtent, true,
                     {{view.resolved, ImageAccess::FragmentRead},
                      {*chain[0], ImageAccess::GeneralRead}});
            lensPass("Depth of field", *m_bokehPipeline, *chain[2], view.halfExtent, true,
                     {{*chain[1], ImageAccess::GeneralRead}});
            lensPass("Depth of field", *m_dofFilterPipeline, *chain[3], view.halfExtent, true,
                     {{*chain[2], ImageAccess::GeneralRead}});
            lensPass("Depth of field", *m_dofCombinePipeline,
                     view.blurring ? *chain[4] : lensOutput, view.extent, view.blurring,
                     {{view.resolved, ImageAccess::FragmentRead},
                      {*chain[0], ImageAccess::GeneralRead},
                      {*chain[3], ImageAccess::GeneralRead}});
        }
        if (view.blurring)
        {
            lensPass("Motion blur", *m_motionTilesPipeline, *chain[5], view.tileExtent, true,
                     {{view.depth, ImageAccess::FragmentRead},
                      {view.velocity, ImageAccess::FragmentRead}});
            lensPass("Motion blur", *m_motionNeighboursPipeline, *chain[6], view.tileExtent, true,
                     {{*chain[5], ImageAccess::GeneralRead}});
            std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> reads{
                {view.resolved, ImageAccess::FragmentRead},
                {view.depth, ImageAccess::FragmentRead},
                {view.velocity, ImageAccess::FragmentRead},
                {*chain[6], ImageAccess::GeneralRead},
            };
            if (view.focusing)
            {
                reads.push_back({*chain[4], ImageAccess::GeneralRead});
            }
            lensPass("Motion blur", *m_motionBlurPipeline, lensOutput, view.extent, false,
                     std::move(reads));
        }
    }
}

void VulkanRenderer::addBloomPasses(FrameGraph& view)
{
    if (view.bloomed)
    {
        // Every level of the chain is named by one descriptor, so they all have to be in the
        // general layout before any pass that binds it runs, even the ones written later.
        std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> chainAccesses;
        for (const std::size_t index : view.bloomIndices)
        {
            chainAccesses.push_back({*view.created[index], ImageAccess::GeneralRead});
        }
        view.graph.addPass("Bloom chain", std::move(chainAccesses), {});

        // Each step reads the level above it, and the last ones add themselves back into it.
        const auto blur = [&](const Pipeline& pipeline, RenderGraph::ImageId source,
                              RenderGraph::ImageId destination, math::Extent2D sourceExtent,
                              math::Extent2D destinationExtent, std::uint32_t sourceLevel,
                              float strength, bool clear)
        {
            view.graph.addPass(
                "Bloom",
                {{source, ImageAccess::GeneralRead}, {destination, ImageAccess::GeneralAttachment}},
                // The pass runs once the graph replays it, when the arguments of this
                // call are long gone: everything it needs is taken by value.
                [&view, used = &pipeline, sourceLevel, strength, clear, destination,
                 destinationExtent, sourceExtent](VkCommandBuffer commands)
                {
                    const VkRenderingAttachmentInfo colorAttachment{
                        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                        .imageView = view.graph.view(destination),
                        .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                        .loadOp =
                            clear ? VK_ATTACHMENT_LOAD_OP_DONT_CARE : VK_ATTACHMENT_LOAD_OP_LOAD,
                        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                    };
                    const VkRenderingInfo renderingInfo{
                        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                        .renderArea = {.extent = {destinationExtent.width,
                                                  destinationExtent.height}},
                        .layerCount = 1,
                        .colorAttachmentCount = 1,
                        .pColorAttachments = &colorAttachment,
                    };
                    vkCmdBeginRendering(commands, &renderingInfo);
                    setViewport(commands, destinationExtent);
                    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, used->handle());
                    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            used->layout(), 0, 2, view.sets.data(), 0, nullptr);
                    const BloomPushConstants constants{
                        .source = sourceLevel,
                        .texelWidth = 1.0f / static_cast<float>(sourceExtent.width),
                        .texelHeight = 1.0f / static_cast<float>(sourceExtent.height),
                        .strength = strength,
                    };
                    vkCmdPushConstants(commands, used->layout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(constants), &constants);
                    vkCmdDraw(commands, 3, 1, 0, 0);
                    vkCmdEndRendering(commands);
                });
        };

        // The first step reads the finished image, which the chain does not hold.
        view.graph.addPass(
            "Bloom",
            {{view.finished, ImageAccess::FragmentRead},
             {*view.created[view.bloomIndices[0]], ImageAccess::GeneralAttachment}},
            [&](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo colorAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(*view.created[view.bloomIndices[0]]),
                    .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {view.bloomExtents[0].width,
                                              view.bloomExtents[0].height}},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                setViewport(commands, view.bloomExtents[0]);
                vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  m_bloomThresholdPipeline->handle());
                vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        m_bloomThresholdPipeline->layout(), 0, 2, view.sets.data(),
                                        0, nullptr);
                const BloomPushConstants constants{
                    .texelWidth = 1.0f / static_cast<float>(view.extent.width),
                    .texelHeight = 1.0f / static_cast<float>(view.extent.height),
                    .strength = m_world.camera.bloomThreshold,
                };
                vkCmdPushConstants(commands, m_bloomThresholdPipeline->layout(),
                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                   sizeof(constants), &constants);
                vkCmdDraw(commands, 3, 1, 0, 0);
                vkCmdEndRendering(commands);
            });

        for (std::uint32_t level = 1; level < DescriptorSets::bloomLevels; ++level)
        {
            blur(*m_bloomDownsamplePipeline, *view.created[view.bloomIndices[level - 1]],
                 *view.created[view.bloomIndices[level]], view.bloomExtents[level - 1],
                 view.bloomExtents[level], level - 1, 1.0f, true);
        }
        for (std::uint32_t level = DescriptorSets::bloomLevels - 1; level > 0; --level)
        {
            blur(*m_bloomUpsamplePipeline, *view.created[view.bloomIndices[level]],
                 *view.created[view.bloomIndices[level - 1]], view.bloomExtents[level],
                 view.bloomExtents[level - 1], level, 1.0f, false);
        }
    }
}

void VulkanRenderer::drawTonemap(FrameGraph& view, VkCommandBuffer commands,
                                 RenderGraph::ImageId image, math::Extent2D size)
{
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view.graph.view(image),
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = {size.width, size.height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };
    vkCmdBeginRendering(commands, &renderingInfo);
    setViewport(commands, size);
    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline->handle());
    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, m_tonemapPipeline->layout(),
                            0, 2, view.sets.data(), 0, nullptr);
    const GpuTexture* const colorTable =
        isReady(m_world.camera.colorTable) ? m_textures.find(m_world.camera.colorTable) : nullptr;
    const math::Vec3 balance = whiteBalanceFactors(m_world.camera.temperature, m_world.camera.tint);
    const TonemapPushConstants tonemap{
        .tonemapper = static_cast<std::uint32_t>(m_world.camera.tonemapper),
        .bloom = view.bloomed ? m_world.camera.bloom : 0.0f,
        .vignette = std::clamp(m_world.camera.vignette, 0.0f, 1.0f),
        .grain = std::max(m_world.camera.grain, 0.0f),
        .chromatic = std::max(m_world.camera.chromaticAberration, 0.0f),
        .time = static_cast<float>(m_frameIndex % 1024),
        .colorTable = colorTable != nullptr ? colorTable->slot : whiteTextureSlot,
        // A strip of squares is as tall as one step of the table.
        .colorTableSize =
            colorTable != nullptr ? static_cast<float>(colorTable->image.extent().height) : 0.0f,
        .targetWidth = static_cast<float>(size.width),
        .targetHeight = static_cast<float>(size.height),
        .encode = view.targetFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                          view.targetFormat == VK_FORMAT_R8G8B8A8_SRGB
                      ? 0u
                      : 1u,
        .saturation = std::max(m_world.camera.saturation, 0.0f),
        .contrast = std::max(m_world.camera.contrast, 0.01f),
        .filterRed = std::max(m_world.camera.colorFilter.r, 0.0f),
        .filterGreen = std::max(m_world.camera.colorFilter.g, 0.0f),
        .filterBlue = std::max(m_world.camera.colorFilter.b, 0.0f),
        .balanceLong = balance.x,
        .balanceMedium = balance.y,
        .balanceShort = balance.z,
    };
    vkCmdPushConstants(commands, m_tonemapPipeline->layout(),
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(tonemap), &tonemap);
    vkCmdDraw(commands, 3, 1, 0, 0);
    vkCmdEndRendering(commands);
}

void VulkanRenderer::addTonemapPasses(FrameGraph& view)
{
    std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> tonemapAccesses{
        {view.finished, ImageAccess::FragmentRead},
        {view.target, ImageAccess::ColorAttachment},
    };
    if (view.bloomed)
    {
        tonemapAccesses.push_back({*view.created[view.bloomIndices[0]], ImageAccess::GeneralRead});
    }
    // Draws the scene into the target, or into the smaller image of a capture.
    std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> captureAccesses = tonemapAccesses;
    view.graph.addPass("Tonemap", std::move(tonemapAccesses), [&](VkCommandBuffer commands)
                       { drawTonemap(view, commands, view.target, view.extent); });
    if (view.capture)
    {
        const RenderGraph::ImageId captured = *view.created[view.captureIndex];
        captureAccesses[1] = {captured, ImageAccess::ColorAttachment};
        view.graph.addPass("Capture", std::move(captureAccesses),
                           [&, captured](VkCommandBuffer commands)
                           { drawTonemap(view, commands, captured, view.frame.captureExtent); });
        view.graph.addPass(
            "Capture readback", {{captured, ImageAccess::TransferRead}},
            [&, captured](VkCommandBuffer commands)
            {
                const VkBufferImageCopy copy{
                    .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                    .imageExtent = {view.frame.captureExtent.width, view.frame.captureExtent.height,
                                    1},
                };
                vkCmdCopyImageToBuffer(commands, view.graph.handle(captured),
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       view.frame.captureReadback->handle(), 1, &copy);
            });
    }
}

void VulkanRenderer::addOverlayPass(FrameGraph& view)
{
    if (view.drawOverlay)
    {
        std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> overlayAccesses{
            {view.target, ImageAccess::ColorAttachment},
            {view.sceneDepth, ImageAccess::DepthAttachment},
        };
        if (view.outlines)
        {
            overlayAccesses.push_back(
                {*view.created[view.selectionMaskIndex], ImageAccess::FragmentRead});
        }
        const OverlayRanges ranges = *view.overlayRanges;
        view.graph.addPass(
            "Overlay", std::move(overlayAccesses),
            [&, ranges](VkCommandBuffer commands)
            {
                const VkRenderingAttachmentInfo colorAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(view.target),
                    .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                };
                const VkRenderingAttachmentInfo depthAttachment{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView = view.graph.view(view.sceneDepth),
                    .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                    .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                };
                const VkRenderingInfo renderingInfo{
                    .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea = {.extent = {view.extent.width, view.extent.height}},
                    .layerCount = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments = &colorAttachment,
                    .pDepthAttachment = &depthAttachment,
                };
                vkCmdBeginRendering(commands, &renderingInfo);
                setViewport(commands, view.extent);

                OverlayPushConstants constants{
                    .scene = view.sceneData,
                    .vertices = view.frame.overlayVertices->deviceAddress(),
                    .outlineColor = outlineColor,
                    .lineWidth = std::isfinite(m_world.lineWidth)
                                     ? std::clamp(m_world.lineWidth, 0.5f, 6.0f)
                                     : 1.5f,
                    .viewportWidth = static_cast<float>(view.extent.width),
                    .viewportHeight = static_cast<float>(view.extent.height),
                    .jitter = m_jitter,
                };
                // Slang's SV_VertexID does not include the first vertex of a draw: each list gets
                // the address of its own first vertex instead.
                const auto draw = [&](const Pipeline& pipeline, std::uint32_t first,
                                      std::size_t count, float depthScale, bool lines)
                {
                    if (count == 0)
                    {
                        return;
                    }
                    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
                    constants.vertices = view.frame.overlayVertices->deviceAddress() +
                                         VkDeviceSize{first} * sizeof(GpuOverlayVertex);
                    constants.depthScale = depthScale;
                    vkCmdPushConstants(commands, pipeline.layout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(constants), &constants);
                    vkCmdDraw(commands, static_cast<std::uint32_t>(lines ? (count / 2) * 6 : count),
                              1, 0, 0);
                };
                constants.depthPlane = m_world.sceneLineDepthPlane;
                const math::Vec3 planeNormal(m_world.sceneLineDepthPlane);
                const float lineBias =
                    math::dot(planeNormal, planeNormal) > 0.5f ? sceneLineDepthScale : 1.0f;
                draw(*m_sceneLinePipeline, ranges.sceneLines, m_world.sceneLines.size(), lineBias,
                     true);
                constants.depthPlane = math::Vec4{0.0f};
                if (view.outlines)
                {
                    vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                      m_outlinePipeline->handle());
                    vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            m_outlinePipeline->layout(), 0, 2, view.sets.data(), 0,
                                            nullptr);
                    vkCmdPushConstants(commands, m_outlinePipeline->layout(),
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       sizeof(constants), &constants);
                    vkCmdDraw(commands, 3, 1, 0, 0);
                }
                draw(*m_overlayLinePipeline, ranges.overlayLines, m_world.overlayLines.size(), 1.0f,
                     true);
                draw(*m_overlayTrianglePipeline, ranges.overlayTriangles,
                     m_world.overlayTriangles.size(), 1.0f, false);
                vkCmdEndRendering(commands);
            });
    }
}

std::optional<std::uint32_t> VulkanRenderer::interfaceTexture(FrameGraph& view,
                                                              const UiDraw& uiDraw)
{
    switch (uiDraw.source)
    {
    case UiSource::Texture:
        break;
    case UiSource::SceneImage:
        return view.toViewport ? std::optional(view.toolSlots) : std::nullopt;
    case UiSource::Surface:
        for (std::size_t surface = 0;
             surface < view.surfaceIndices.size() && surface + 1 < toolImageSlots; ++surface)
        {
            if (m_world.uiSurfaces[surface].id == uiDraw.surface)
            {
                return view.toolSlots + 1 + static_cast<std::uint32_t>(surface);
            }
        }
        return std::nullopt;
    case UiSource::View:
        for (std::size_t shown = 0; shown < view.shownViews.size(); ++shown)
        {
            const std::size_t place = 1 + view.surfaceIndices.size() + shown;
            if (view.shownViews[shown]->id == uiDraw.surface && place < toolImageSlots)
            {
                return view.toolSlots + static_cast<std::uint32_t>(place);
            }
        }
        return std::nullopt;
    }
    const GpuTexture* const texture = m_textures.find(uiDraw.texture);
    return texture != nullptr ? texture->slot : whiteTextureSlot;
}

void VulkanRenderer::drawInterface(FrameGraph& view, VkCommandBuffer commands,
                                   VkImageView imageView, VkAttachmentLoadOp load,
                                   const math::Vec4& clearColor, math::Extent2D size,
                                   std::span<const UiDraw> draws, std::uint32_t indexBase,
                                   const Pipeline& pipeline, bool displaySpace)
{
    // A view of display colours takes its clear colour encoded as well.
    const auto encode = [displaySpace](float value)
    {
        if (!displaySpace)
        {
            return value;
        }
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        return clamped <= 0.0031308f ? clamped * 12.92f
                                     : 1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f;
    };
    const VkRenderingAttachmentInfo colorAttachment{
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = imageView,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .loadOp = load,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue = {.color = {.float32 = {encode(clearColor.x), encode(clearColor.y),
                                             encode(clearColor.z), clearColor.w}}},
    };
    const VkRenderingInfo renderingInfo{
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = {.extent = {size.width, size.height}},
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &colorAttachment,
    };
    vkCmdBeginRendering(commands, &renderingInfo);
    setViewport(commands, size);
    if (!draws.empty())
    {
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 2,
                                view.sets.data(), 0, nullptr);
    }
    for (const UiDraw& uiDraw : draws)
    {
        const std::optional<std::uint32_t> slot = interfaceTexture(view, uiDraw);
        if (!slot)
        {
            continue;
        }
        // A batch that names a rectangle is cut to it; the others cover the image.
        const bool clipped = uiDraw.clip.z > uiDraw.clip.x && uiDraw.clip.w > uiDraw.clip.y;
        const VkRect2D scissor =
                clipped ? VkRect2D{
                              .offset = {std::max(static_cast<std::int32_t>(uiDraw.clip.x), 0),
                                         std::max(static_cast<std::int32_t>(uiDraw.clip.y), 0)},
                              .extent = {static_cast<std::uint32_t>(std::clamp(
                                             uiDraw.clip.z - std::max(uiDraw.clip.x, 0.0f), 0.0f,
                                             static_cast<float>(size.width))),
                                         static_cast<std::uint32_t>(std::clamp(
                                             uiDraw.clip.w - std::max(uiDraw.clip.y, 0.0f), 0.0f,
                                             static_cast<float>(size.height)))},
                          }
                        : VkRect2D{.offset = {0, 0}, .extent = {size.width, size.height}};
        vkCmdSetScissor(commands, 0, 1, &scissor);

        // Slang's SV_VertexID does not include the first vertex of a draw: the batch gets
        // the address of its own first index instead.
        const UiPushConstants constants{
            .vertices = view.frame.uiVertices->deviceAddress(),
            .indices = view.frame.uiIndices->deviceAddress() +
                       VkDeviceSize{indexBase + uiDraw.firstIndex} * sizeof(std::uint32_t),
            .inverseViewport = math::Vec2{2.0f / static_cast<float>(size.width),
                                          2.0f / static_cast<float>(size.height)},
            .kind = static_cast<std::uint32_t>(uiDraw.kind),
            .texture = *slot,
            .rect = uiDraw.rect,
            .radius = uiDraw.radius,
            .sharpness = uiDraw.sharpness,
            .displaySpace = displaySpace && uiDraw.source == UiSource::Texture ? 1u : 0u,
        };
        vkCmdPushConstants(commands, pipeline.layout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(constants), &constants);
        vkCmdDraw(commands, uiDraw.indexCount, 1, 0, 0);
        ++view.drawCalls;
    }
    vkCmdEndRendering(commands);
}

void VulkanRenderer::addInterfacePasses(FrameGraph& view)
{
    // The slots the tools' images sit in, for this frame context: its previous frame, the last to
    // read them, has completed.
    view.toolSlots = m_toolImageSlotBase + view.frameSlot * toolImageSlots;
    // The slot of the texture a batch reads, or nothing for an image this frame did not draw.
    // An image of a display that does not encode its colours itself takes them encoded by the
    // interface: drawn as light, they would come out far too dark.
    const bool targetEncodes = view.targetFormat == VK_FORMAT_B8G8R8A8_SRGB ||
                               view.targetFormat == VK_FORMAT_R8G8B8A8_SRGB;
    if (view.drawUi)
    {
        view.graph.addPass("Interface", {{view.target, ImageAccess::ColorAttachment}},
                           [&, targetEncodes](VkCommandBuffer commands)
                           {
                               drawInterface(view, commands, view.graph.view(view.target),
                                             VK_ATTACHMENT_LOAD_OP_LOAD, math::Vec4{0.0f},
                                             view.extent, m_world.uiDraws, 0, *m_uiPipeline,
                                             !targetEncodes);
                           });
    }
    for (std::size_t surface = 0; surface < view.surfaceIndices.size(); ++surface)
    {
        const RenderGraph::ImageId image = *view.created[view.surfaceIndices[surface]];
        view.graph.addPass(
            "Interface surface", {{image, ImageAccess::ColorAttachment}},
            [&, surface, image, targetEncodes](VkCommandBuffer commands)
            {
                const UiSurface& drawn = m_world.uiSurfaces[surface];
                // Written in the colours of the display, through the view the tools read, or
                // straight into an image that does not encode them.
                const VkImageView alternate = view.graph.image(image)->alternateView();
                drawInterface(
                    view, commands,
                    alternate != VK_NULL_HANDLE ? alternate : view.graph.view(image),
                    VK_ATTACHMENT_LOAD_OP_CLEAR, drawn.clearColor,
                    math::Extent2D{std::max(drawn.size.width, 1u), std::max(drawn.size.height, 1u)},
                    drawn.draws, view.surfaceIndexBases[surface],
                    alternate != VK_NULL_HANDLE ? *m_uiDisplayPipeline : *m_uiPipeline,
                    alternate != VK_NULL_HANDLE || !targetEncodes);
            });
    }

    // The surfaces other windows of the system show, whose bytes are already the colours of the display.
    for (const std::unique_ptr<WindowTarget>& target : m_windowTargets)
    {
        for (std::size_t surface = 0; target->ready && surface < view.surfaceIndices.size(); ++surface)
        {
            if (m_world.uiSurfaces[surface].id != target->surfaceId)
            {
                continue;
            }
            const RenderGraph::ImageId image = *view.created[view.surfaceIndices[surface]];
            const math::Extent2D drawn = m_world.uiSurfaces[surface].size;
            view.graph.addPass("Window of its own", {{image, ImageAccess::TransferRead}},
                               [&view, image, drawn, shown = target.get()](VkCommandBuffer commands)
                               {
                                   const VkImage destination = shown->swapchain->image(shown->imageIndex);
                                   const math::Extent2D room = shown->swapchain->extent();
                                   transitionImage(commands, destination, ImageState::AcquiredBackbuffer, ImageState::TransferDestination);
                                   const VkImageBlit region{
                                       .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                                       .srcOffsets = {{0, 0, 0}, {static_cast<std::int32_t>(std::max(drawn.width, 1u)), static_cast<std::int32_t>(std::max(drawn.height, 1u)), 1}},
                                       .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .layerCount = 1},
                                       .dstOffsets = {{0, 0, 0}, {static_cast<std::int32_t>(room.width), static_cast<std::int32_t>(room.height), 1}},
                                   };
                                   if (shown->swapchain->format() == view.targetFormat && drawn.width == room.width && drawn.height == room.height)
                                   {
                                       // The bytes as they are: a blit would encode an sRGB image once more.
                                       const VkImageCopy copy{
                                           .srcSubresource = region.srcSubresource,
                                           .dstSubresource = region.dstSubresource,
                                           .extent = {room.width, room.height, 1},
                                       };
                                       vkCmdCopyImage(commands, view.graph.handle(image), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination,
                                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
                                   }
                                   else
                                   {
                                       vkCmdBlitImage(commands, view.graph.handle(image), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination,
                                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region, VK_FILTER_LINEAR);
                                   }
                                   transitionImage(commands, destination, ImageState::TransferDestination, ImageState::Present);
                               });
        }
    }

    if (view.extraView == nullptr && (view.toViewport || view.drawTools))
    {
        // The images the tools show are read in the colours of the display, through the view of
        // their image the tools read when the window has one.
        std::vector<std::pair<RenderGraph::ImageId, ImageAccess>> toolAccesses{
            {view.backbuffer, ImageAccess::ColorAttachment},
        };
        const auto toolView = [&](RenderGraph::ImageId image)
        {
            const VkImageView alternate = view.graph.image(image)->alternateView();
            return alternate != VK_NULL_HANDLE ? alternate : view.graph.view(image);
        };
        if (view.toViewport)
        {
            toolAccesses.push_back({view.target, ImageAccess::FragmentRead});
            m_descriptors->setTexture(view.toolSlots, toolView(view.target), true);
        }
        for (std::size_t surface = 0;
             surface < view.surfaceIndices.size() && surface + 1 < toolImageSlots; ++surface)
        {
            const RenderGraph::ImageId image = *view.created[view.surfaceIndices[surface]];
            toolAccesses.push_back({image, ImageAccess::FragmentRead});
            m_descriptors->setTexture(view.toolSlots + 1 + static_cast<std::uint32_t>(surface),
                                      toolView(image), true);
        }
        // The images of the views, drawn before this frame by their own submissions.
        for (std::size_t shown = 0; shown < view.shownViews.size(); ++shown)
        {
            const std::size_t place = 1 + view.surfaceIndices.size() + shown;
            if (place >= toolImageSlots)
            {
                break;
            }
            const Image& image = *view.shownViews[shown]->images[view.frameSlot];
            toolAccesses.push_back(
                {view.graph.importImage(image.handle(), image.view(), view.targetFormat,
                                        view.shownViews[shown]->imageStates[view.frameSlot]),
                 ImageAccess::FragmentRead});
            const VkImageView alternate = image.alternateView();
            m_descriptors->setTexture(view.toolSlots + static_cast<std::uint32_t>(place),
                                      alternate != VK_NULL_HANDLE ? alternate : image.view(), true);
        }
        // A window of display colours with an sRGB view encodes them itself.
        const bool encodeTools = view.toolsFormat != VK_FORMAT_B8G8R8A8_SRGB &&
                                 view.toolsFormat != VK_FORMAT_R8G8B8A8_SRGB;
        view.graph.addPass("Tools", std::move(toolAccesses),
                           [&, encodeTools](VkCommandBuffer commands)
                           {
                               // Without a viewport, the tools are drawn over the tonemapped scene.
                               drawInterface(view, commands,
                                             m_swapchain->toolsImageView(view.imageIndex),
                                             view.toViewport ? VK_ATTACHMENT_LOAD_OP_CLEAR
                                                             : VK_ATTACHMENT_LOAD_OP_LOAD,
                                             math::Vec4{0.0f, 0.0f, 0.0f, 1.0f}, view.windowExtent,
                                             m_world.toolsDraws, view.toolsIndexBase,
                                             *m_uiDisplayPipeline, encodeTools);
                           });
    }
}

void VulkanRenderer::addWindowCapturePass(FrameGraph& view)
{
    // The window as it will be shown, read back after everything is drawn into it.
    if (view.extraView == nullptr && m_windowCaptureRequest)
    {
        const VkDeviceSize bytes =
            VkDeviceSize{4} * view.windowExtent.width * view.windowExtent.height;
        if (m_swapchain->readable() &&
            (!view.frame.windowReadback || view.frame.windowReadback->size() < bytes))
        {
            core::Result<Buffer> readback =
                Buffer::create(m_allocator, {.size = bytes,
                                             .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                             .hostVisible = true,
                                             .hostReads = true});
            if (readback)
            {
                view.frame.windowReadback = std::move(*readback);
            }
            else
            {
                DEVEX_LOG_WARNING("The window cannot be read back: {}", readback.error());
                view.frame.windowReadback.reset();
            }
        }
        view.frame.windowRequest = std::exchange(m_windowCaptureRequest, std::nullopt);
        view.frame.windowExtent = view.windowExtent;
        view.frame.windowFormat = view.targetFormat;
        if (m_swapchain->readable() && view.frame.windowReadback)
        {
            view.graph.addPass(
                "Window readback", {{view.backbuffer, ImageAccess::TransferRead}},
                [&](VkCommandBuffer commands)
                {
                    const VkBufferImageCopy copy{
                        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                             .layerCount = 1},
                        .imageExtent = {view.windowExtent.width, view.windowExtent.height, 1},
                    };
                    vkCmdCopyImageToBuffer(commands, view.graph.handle(view.backbuffer),
                                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                           view.frame.windowReadback->handle(), 1, &copy);
                });
        }
        else
        {
            // Nothing to read: the request is answered with an empty picture.
            view.frame.windowReadback.reset();
        }
    }
}

core::Result<std::uint32_t> VulkanRenderer::recordFrame(FrameContext& frame,
                                                        std::uint32_t frameSlot,
                                                        std::uint32_t imageIndex, bool drawShadows,
                                                        ExtraView* extraView)
{
    // Every pass of this view uses the same level, including picking, shadows and outlines.
    // Restore the original handles before recording another camera or retaining frame history.
    m_originalMeshes.clear();
    m_nextLods.clear();
    if (m_meshLodError > 0.0f && std::isfinite(m_meshLodError))
        for (auto& instance : m_world.meshes)
        {
            m_originalMeshes.push_back(instance.mesh);
            const auto selected = chooseMeshLod(instance);
            const auto* originalMesh = m_meshes.find(instance.mesh);
            if (instance.objectId != 0 && originalMesh != nullptr && !originalMesh->lods.empty())
                m_nextLods.push_back({instanceKey(instance), instance.mesh, selected});
            if (selected != instance.mesh)
            {
                const auto& original = m_meshes.find(instance.mesh)->submeshes[instance.submesh];
                const auto& reduced = m_meshes.find(selected)->submeshes[instance.submesh];
                ++m_lodInstances;
                m_lodTrianglesSaved += (original.indexCount - reduced.indexCount) / 3;
                instance.mesh = selected;
            }
        }
    std::ranges::sort(m_nextLods, {}, &LodChoice::key);
    struct RestoreMeshes
    {
        VulkanRenderer& renderer;
        RenderWorld& world;
        const std::vector<MeshHandle>& handles;
        ~RestoreMeshes()
        {
            for (std::size_t i = 0; i < handles.size(); ++i)
                world.meshes[i].mesh = handles[i];
            renderer.m_previousLods.swap(renderer.m_nextLods);
        }
    } restoreMeshes{*this, m_world, m_originalMeshes};
    const VkCommandBuffer commandBuffer = frame.commandBuffer;
    DEVEX_VK_TRY(vkResetCommandPool, m_device.handle(), frame.commandPool, 0);
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    DEVEX_VK_TRY(vkBeginCommandBuffer, commandBuffer, &beginInfo);
    const bool timed = frame.timestamps != VK_NULL_HANDLE && core::profiler::isEnabled();
    frame.timestampCount = 0;
    frame.timedPasses.clear();
    if (timed)
    {
        vkCmdResetQueryPool(commandBuffer, frame.timestamps, 0, maxTimestamps);
        frame.profiledFrame = core::profiler::currentFrame();
    }

    FrameGraph view{*this, frame, frameSlot, imageIndex, extraView};
    if (core::Result<void> prepared = prepareFrameGraph(view, drawShadows); !prepared)
    {
        return std::unexpected(prepared.error());
    }
    addShadowPasses(view);
    addDepthPasses(view);
    addLightingPasses(view);
    addScenePasses(view);
    addPickPasses(view);
    addAntialiasingPass(view);
    addLensPasses(view);
    addBloomPasses(view);
    addTonemapPasses(view);
    addOverlayPass(view);
    addInterfacePasses(view);
    addWindowCapturePass(view);

    if (extraView == nullptr)
    {
        view.graph.addPass("Present", {{view.backbuffer, ImageAccess::Present}}, {});
    }
    RenderGraph::PassMarker marker;
    if (timed)
    {
        marker = [&frame](VkCommandBuffer commands, std::string_view pass)
        {
            if (frame.timestampCount >= maxTimestamps)
            {
                return;
            }
            // Written once everything recorded before has finished: the end of one pass is the
            // start of the next.
            vkCmdWriteTimestamp2(commands, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, frame.timestamps,
                                 frame.timestampCount++);
            frame.timedPasses.push_back(pass.empty() ? nullptr : core::profiler::intern(pass));
        };
    }
    view.graph.execute(commandBuffer, m_instance.isValidationEnabled(), marker);
    frame.images.endFrame();

    DEVEX_VK_TRY(vkEndCommandBuffer, commandBuffer);
    return view.drawCalls;
}

} // namespace devex::render::vulkan
