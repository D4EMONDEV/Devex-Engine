#pragma once

#include <devex/asset/MaterialData.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/asset/TextureData.hpp>
#include <devex/core/Assert.hpp>
#include <devex/core/Error.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Gpu.hpp>
#include <devex/render/RenderWorld.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

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
    // Directory of the compiled .spv shaders; empty uses "shaders" next to the executable.
    std::filesystem::path shaderDirectory;
    // Multisampling of the scene, lowered to what the GPU supports; 1 disables it.
    std::uint32_t msaaSamples = 4;
};

// Parameters of a material, with textures already uploaded. Invalid or destroyed textures sample as
// white, or as a flat normal for the normal texture.
struct MaterialDesc
{
    // Linear RGBA.
    math::Vec4 baseColorFactor{1.0f};
    TextureHandle baseColorTexture;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    TextureHandle metallicRoughnessTexture;
    TextureHandle normalTexture;
    float normalScale = 1.0f;
    TextureHandle occlusionTexture;
    float occlusionStrength = 1.0f;
    math::Vec3 emissiveFactor{0.0f};
    TextureHandle emissiveTexture;
    asset::AlphaMode alphaMode = asset::AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    bool doubleSided = false;
};

struct RendererStats
{
    // Draw calls of the last presented frame.
    std::uint32_t drawCalls = 0;
    std::size_t meshCount = 0;
    std::size_t textureCount = 0;
    std::size_t materialCount = 0;
    std::uint32_t lightCount = 0;
    // Exposure of the last frame, including automatic exposure.
    float ev100 = 0.0f;
    std::uint32_t msaaSamples = 1;
    math::Extent2D swapchainExtent;
    // The size the scene was drawn at: the viewport, or the swapchain.
    math::Extent2D sceneExtent;
    // Bytes of device-local memory used by the process, and how much it may use before the
    // operating system starts evicting memory.
    std::uint64_t gpuMemoryUsage = 0;
    std::uint64_t gpuMemoryBudget = 0;
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

    // Uploads the mesh to GPU memory and waits for the transfer to complete.
    [[nodiscard]] core::Result<MeshHandle> createMesh(const asset::MeshData& mesh);
    // Releases the mesh once no frame in flight uses it. Stale handles are ignored.
    void destroyMesh(MeshHandle mesh);
    // Zero for a stale handle.
    [[nodiscard]] std::uint32_t submeshCount(MeshHandle mesh) const noexcept;

    // Uploads the texture with its mip levels and waits for the transfer to complete.
    [[nodiscard]] core::Result<TextureHandle> createTexture(const asset::TextureData& texture);
    // Releases the texture once no frame in flight uses it. Materials using it sample the
    // default texture from the next frame on. Stale handles are ignored.
    void destroyTexture(TextureHandle texture);

    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc& material);
    // Changes a material from the next frame on. Stale handles are ignored.
    void updateMaterial(MaterialHandle handle, const MaterialDesc& material);
    void destroyMaterial(MaterialHandle material);

    // Starts a frame with an empty snapshot to fill.
    [[nodiscard]] RenderWorld& beginFrame() noexcept;
    // Draws and presents the snapshot. Skips the frame while the window has no drawable area.
    [[nodiscard]] core::Result<void> endFrame();

    // The answers to RenderWorld::pick received since the last call, oldest first. A request is
    // answered once the GPU has completed its frame, usually two frames later; a frame skipped
    // while the window has no drawable area drops its request.
    [[nodiscard]] std::vector<PickResult> takePickResults();

    [[nodiscard]] RendererStats stats() const noexcept;

    // Connects Dear ImGui to the renderer. An ImGui context must be current and the window must
    // be visible; shutdownImGui must run before the context is destroyed.
    [[nodiscard]] core::Result<void> initializeImGui();
    void shutdownImGui() noexcept;
    void beginImGuiFrame();
    // Draws ImGui::GetDrawData() over the scene in the next endFrame. Call after ImGui::Render().
    void queueImGuiDrawData() noexcept;
    // An ImGui texture identifier (ImTextureID) that shows the scene image of the frame it is drawn
    // in, when RenderWorld::viewport is set.
    [[nodiscard]] static std::uint64_t viewportTexture() noexcept;

private:
    explicit Renderer(std::unique_ptr<vulkan::VulkanRenderer> implementation) noexcept;

    std::unique_ptr<vulkan::VulkanRenderer> m_implementation;
};

} // namespace devex::render
