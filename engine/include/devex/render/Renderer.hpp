#pragma once

#include <devex/core/Assert.hpp>
#include <devex/core/Error.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Gpu.hpp>
#include <devex/render/RenderWorld.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace devex::render {

enum class PresentMode : std::uint8_t
{
    // Waits for the display refresh: no tearing, lowest power. Always available.
    Fifo,
    // Replaces the queued image with the newest one: low latency without tearing.
    Mailbox,
    // Presents as soon as possible: lowest latency, with tearing.
    Immediate,
};

[[nodiscard]] std::string_view toString(PresentMode mode) noexcept;

struct RendererConfig
{
    std::string applicationName = "Devex";
    // Falls back to Fifo when the display does not support the requested mode.
    PresentMode presentMode = PresentMode::Fifo;
    // Case-insensitive part of the GPU name to use; empty selects the most capable GPU.
    std::string preferredGpu;
    // Enables the Khronos validation layer when the Vulkan SDK is installed.
    bool validation = core::assertsEnabled;
};

namespace vulkan {
class VulkanRenderer;
} // namespace vulkan

// Draws RenderWorld snapshots into a window. Only one renderer may exist at a time, and the
// window must outlive it.
class Renderer
{
public:
    [[nodiscard]] static core::Result<Renderer> create(const platform::Platform& platform,
                                                       platform::Window& window,
                                                       const RendererConfig& config);

    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    [[nodiscard]] const GpuInfo& gpu() const noexcept;
    // The present mode in use, which may differ from the requested one.
    [[nodiscard]] PresentMode presentMode() const noexcept;

    // Starts a frame with an empty snapshot to fill.
    [[nodiscard]] RenderWorld& beginFrame() noexcept;
    // Draws and presents the snapshot. Skips the frame while the window has no drawable area.
    [[nodiscard]] core::Result<void> endFrame();

private:
    explicit Renderer(std::unique_ptr<vulkan::VulkanRenderer> implementation) noexcept;

    std::unique_ptr<vulkan::VulkanRenderer> m_implementation;
};

} // namespace devex::render
