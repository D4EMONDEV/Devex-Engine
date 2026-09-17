#pragma once

#include "Commands.hpp"
#include "Device.hpp"
#include "Memory.hpp"
#include "Vulkan.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace devex::render::vulkan {

// Images that passes create each frame, kept from one frame to the next as long as they are used.
// Each frame context owns its pool, so frames in flight never share an image.
class TransientImagePool
{
public:
    struct Lease
    {
        const Image* image = nullptr;
        // The state the image was left in, updated by the render graph.
        ImageState* state = nullptr;
    };

    // An image with exactly this configuration not yet taken this frame, created when needed.
    [[nodiscard]] core::Result<Lease> acquire(const Device& device, const Allocator& allocator,
                                              const ImageConfig& config);

    // Destroys the images no pass took during the frame, then makes the others available again.
    void endFrame();

    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Entry
    {
        Image image;
        ImageState state = ImageState::Undefined;
        bool taken = false;
    };

    std::vector<std::unique_ptr<Entry>> m_entries;
};

enum class ImageAccess : std::uint8_t
{
    ColorAttachment,
    DepthAttachment,
    FragmentRead,
    ComputeRead,
    ComputeWrite,
    Present,
};

// Records a frame as passes that declare how they use images. Before each pass, the graph moves
// every image it uses into the required layout, with synchronization2 barriers computed from the
// image's previous use; passes only record their drawing or dispatching commands.
class RenderGraph
{
public:
    using ImageId = std::uint32_t;
    using Record = std::function<void(VkCommandBuffer commandBuffer)>;

    RenderGraph(const Device& device, const Allocator& allocator, TransientImagePool& pool) noexcept;

    // A transient image from the pool.
    [[nodiscard]] core::Result<ImageId> createImage(const ImageConfig& config);
    // An image owned elsewhere, such as a swapchain image, whose state the graph updates.
    [[nodiscard]] ImageId importImage(VkImage image, VkImageView view, VkFormat format,
                                      ImageState& state);

    [[nodiscard]] VkImage handle(ImageId id) const noexcept;
    [[nodiscard]] VkImageView view(ImageId id) const noexcept;
    // Null for imported images.
    [[nodiscard]] const Image* image(ImageId id) const noexcept;

    void addPass(std::string name, std::vector<std::pair<ImageId, ImageAccess>> accesses,
                 Record record);

    // Records the passes in the order they were added. Labels name the passes in debuggers and
    // require the debug utils extension.
    void execute(VkCommandBuffer commandBuffer, bool labels);

private:
    struct Resource
    {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        ImageState* state = nullptr;
        const Image* owned = nullptr;
    };

    struct Pass
    {
        std::string name;
        std::vector<std::pair<ImageId, ImageAccess>> accesses;
        Record record;
    };

    const Device& m_device;
    const Allocator& m_allocator;
    TransientImagePool& m_pool;
    std::vector<Resource> m_resources;
    std::vector<Pass> m_passes;
};

} // namespace devex::render::vulkan
