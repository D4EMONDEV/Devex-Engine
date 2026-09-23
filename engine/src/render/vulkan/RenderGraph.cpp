#include "RenderGraph.hpp"

#include <algorithm>

namespace devex::render::vulkan {
namespace {

[[nodiscard]] ImageState stateFor(ImageAccess access) noexcept
{
    switch (access)
    {
    case ImageAccess::ColorAttachment:
        return ImageState::ColorAttachment;
    case ImageAccess::DepthAttachment:
        return ImageState::DepthAttachment;
    case ImageAccess::DepthResolveAttachment:
        return ImageState::DepthResolveAttachment;
    case ImageAccess::FragmentRead:
        return ImageState::ShaderReadOnly;
    case ImageAccess::ComputeRead:
        return ImageState::ComputeReadOnly;
    case ImageAccess::ComputeWrite:
        return ImageState::ComputeStorage;
    case ImageAccess::GeneralRead:
        return ImageState::GeneralRead;
    case ImageAccess::GeneralAttachment:
        return ImageState::GeneralAttachment;
    case ImageAccess::TransferRead:
        return ImageState::TransferSource;
    case ImageAccess::Present:
        return ImageState::Present;
    }
    return ImageState::Undefined;
}

} // namespace

core::Result<TransientImagePool::Lease> TransientImagePool::acquire(const Device& device,
                                                                     const Allocator& allocator,
                                                                     const ImageConfig& config)
{
    for (const std::unique_ptr<Entry>& entry : m_entries)
    {
        if (!entry->taken && entry->image.config() == config)
        {
            entry->taken = true;
            return Lease{&entry->image, &entry->state};
        }
    }

    core::Result<Image> image = Image::create(device, allocator, config);
    if (!image)
    {
        return std::unexpected(image.error());
    }
    m_entries.push_back(std::make_unique<Entry>(Entry{std::move(*image), ImageState::Undefined, true}));
    return Lease{&m_entries.back()->image, &m_entries.back()->state};
}

void TransientImagePool::endFrame()
{
    std::erase_if(m_entries, [](const std::unique_ptr<Entry>& entry) { return !entry->taken; });
    for (const std::unique_ptr<Entry>& entry : m_entries)
    {
        entry->taken = false;
    }
}

std::size_t TransientImagePool::size() const noexcept
{
    return m_entries.size();
}

RenderGraph::RenderGraph(const Device& device, const Allocator& allocator,
                         TransientImagePool& pool) noexcept
    : m_device(device)
    , m_allocator(allocator)
    , m_pool(pool)
{
}

core::Result<RenderGraph::ImageId> RenderGraph::createImage(const ImageConfig& config)
{
    core::Result<TransientImagePool::Lease> lease = m_pool.acquire(m_device, m_allocator, config);
    if (!lease)
    {
        return std::unexpected(lease.error());
    }
    m_resources.push_back({
        .image = lease->image->handle(),
        .view = lease->image->view(),
        .format = config.format,
        .state = lease->state,
        .owned = lease->image,
    });
    return static_cast<ImageId>(m_resources.size() - 1);
}

RenderGraph::ImageId RenderGraph::importImage(VkImage image, VkImageView view, VkFormat format,
                                              ImageState& state)
{
    m_resources.push_back({.image = image, .view = view, .format = format, .state = &state});
    return static_cast<ImageId>(m_resources.size() - 1);
}

VkImage RenderGraph::handle(ImageId id) const noexcept
{
    return m_resources[id].image;
}

VkImageView RenderGraph::view(ImageId id) const noexcept
{
    return m_resources[id].view;
}

const Image* RenderGraph::image(ImageId id) const noexcept
{
    return m_resources[id].owned;
}

void RenderGraph::addPass(std::string name, std::vector<std::pair<ImageId, ImageAccess>> accesses,
                          Record record)
{
    m_passes.push_back({std::move(name), std::move(accesses), std::move(record)});
}

void RenderGraph::execute(VkCommandBuffer commandBuffer, bool labels, const PassMarker& marker)
{
    for (Pass& pass : m_passes)
    {
        // Before the barriers of the pass, which are part of what it costs.
        if (marker)
        {
            marker(commandBuffer, pass.name);
        }
        if (labels)
        {
            const VkDebugUtilsLabelEXT label{
                .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
                .pLabelName = pass.name.c_str(),
            };
            vkCmdBeginDebugUtilsLabelEXT(commandBuffer, &label);
        }

        for (const auto& [id, access] : pass.accesses)
        {
            Resource& resource = m_resources[id];
            const ImageState target = stateFor(access);
            const ImageState current = *resource.state;
            // Reading an image already readable in the same layout needs no barrier.
            const bool compatible = current == target ||
                                    (isReadOnly(current) && isReadOnly(target) &&
                                     layoutOf(current) == layoutOf(target));
            if (!compatible)
            {
                transitionImage(commandBuffer, resource.image, current, target,
                                aspectOf(resource.format));
            }
            *resource.state = target;
        }

        if (pass.record)
        {
            pass.record(commandBuffer);
        }
        if (labels)
        {
            vkCmdEndDebugUtilsLabelEXT(commandBuffer);
        }
    }
    if (marker)
    {
        marker(commandBuffer, {});
    }
    m_passes.clear();
}

} // namespace devex::render::vulkan
