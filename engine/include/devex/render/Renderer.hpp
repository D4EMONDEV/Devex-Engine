#pragma once

#include <devex/core/Export.hpp>

#include <devex/asset/MaterialData.hpp>
#include <devex/asset/MeshData.hpp>
#include <devex/asset/ShaderData.hpp>
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

[[nodiscard]] DEVEX_API std::string_view toString(PresentMode mode) noexcept;

struct DEVEX_API RendererConfig
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
    // What a frame copies to the GPU at most for the meshes and textures created since the
    // previous one. The first copy of a frame always goes, so that a larger texture never waits.
    std::uint64_t uploadBytesPerFrame = std::uint64_t{64} << 20;
};

// What a sampler2D uniform samples while its texture is missing or still loading.
enum class TextureFallback : std::uint8_t
{
    White,
    Black,
    // A flat normal.
    Normal,
};

// A texture a uniform of a shader samples: the slot of the uniform in MaterialDesc::parameters.
struct DEVEX_API MaterialTexture
{
    std::uint32_t slot = 0;
    TextureHandle texture;
    TextureFallback fallback = TextureFallback::White;
};

// Parameters of a material, with textures already uploaded. Invalid or destroyed textures sample as
// white, or as a flat normal for the normal texture.
struct DEVEX_API MaterialDesc
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
    // A shader of the project that draws the material in place of the standard one, for what its
    // kind draws: meshes, sprites, particles or the sky. An invalid or destroyed shader, or one of
    // another kind than what the material draws, leaves the standard one.
    ShaderHandle shader;
    // The values of its uniforms, one slot each in the order of asset::ShaderData::parameters; the
    // slots it leaves out read zero. Textures take theirs from `textures`.
    std::vector<math::Vec4> parameters;
    std::vector<MaterialTexture> textures;
};

struct DEVEX_API RendererStats
{
    // Draw calls of the last presented frame.
    std::uint32_t drawCalls = 0;
    // Instances the frustums dropped, over every pass of the frame.
    std::uint32_t culledInstances = 0;
    std::size_t meshCount = 0;
    std::size_t textureCount = 0;
    std::size_t materialCount = 0;
    std::size_t shaderCount = 0;
    std::uint32_t lightCount = 0;
    // Exposure of the last frame, including automatic exposure.
    float ev100 = 0.0f;
    math::Extent2D swapchainExtent;
    // The size the scene was drawn at: the viewport, or the swapchain.
    math::Extent2D sceneExtent;
    // Bytes of device-local memory used by the process, and how much it may use before the
    // operating system starts evicting memory.
    std::uint64_t gpuMemoryUsage = 0;
    std::uint64_t gpuMemoryBudget = 0;
    // Meshes and textures created and not yet copied to the GPU, and what the last frame copied.
    std::size_t pendingUploads = 0;
    std::uint64_t pendingUploadBytes = 0;
    std::uint64_t uploadedBytes = 0;
};

namespace vulkan {
class VulkanRenderer;
} // namespace vulkan

// Draws RenderWorld snapshots into a window. Only one renderer may exist at a time, and the
// window must outlive it.
class DEVEX_API Renderer
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

    // Creates the mesh at once; its data reaches the GPU with the next frames, within the upload
    // budget of each (RendererConfig::uploadBytesPerFrame). Until then it is not drawn.
    [[nodiscard]] core::Result<MeshHandle> createMesh(const asset::MeshData& mesh);
    // Whether the data of the mesh is on the GPU, so that it is drawn. False for a stale handle.
    [[nodiscard]] bool isReady(MeshHandle mesh) const noexcept;
    // Releases the mesh once no frame in flight uses it. Stale handles are ignored.
    void destroyMesh(MeshHandle mesh);
    // Zero for a stale handle.
    [[nodiscard]] std::uint32_t submeshCount(MeshHandle mesh) const noexcept;

    // Creates the texture with its mip levels at once; they reach the GPU as a mesh's data does.
    // Until then materials use the default texture, and interfaces leave out what it draws.
    [[nodiscard]] core::Result<TextureHandle> createTexture(const asset::TextureData& texture);
    [[nodiscard]] bool isReady(TextureHandle texture) const noexcept;
    // Releases the texture once no frame in flight uses it. Materials using it sample the
    // default texture from the next frame on. Stale handles are ignored.
    void destroyTexture(TextureHandle texture);

    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc& material);
    // Changes a material from the next frame on. Stale handles are ignored.
    void updateMaterial(MaterialHandle handle, const MaterialDesc& material);
    void destroyMaterial(MaterialHandle material);

    // Builds the pipelines of a compiled shader of a project, for every pass its kind draws in.
    [[nodiscard]] core::Result<ShaderHandle> createShader(const asset::ShaderData& shader);
    // Replaces them, as a new import of the shader does: the materials using it draw with the new
    // ones from the next frame on. On failure the shader keeps its pipelines.
    [[nodiscard]] core::Result<void> updateShader(ShaderHandle handle, const asset::ShaderData& shader);
    // Releases the pipelines once no frame in flight uses them; materials using the shader then
    // draw with the standard one. Stale handles are ignored.
    void destroyShader(ShaderHandle shader);

    // Starts a frame with an empty snapshot to fill.
    [[nodiscard]] RenderWorld& beginFrame() noexcept;
    // Draws and presents the snapshot. Skips the frame while the window has no drawable area.
    [[nodiscard]] core::Result<void> endFrame();

    // The answers to RenderWorld::pick received since the last call, oldest first. A request is
    // answered once the GPU has completed its frame, usually two frames later; a frame skipped
    // while the window has no drawable area drops its request.
    [[nodiscard]] std::vector<PickResult> takePickResults();

    // Asks for a picture of the next frame drawn, scaled down to fit the size and keeping the
    // shape of the scene, with the scene alone: no interface, no tools. Returns the request the
    // answer carries.
    [[nodiscard]] std::uint64_t requestCapture(std::uint32_t maxWidth, std::uint32_t maxHeight);
    // The pictures received since the last call, once the GPU has completed their frames, usually
    // two frames later.
    [[nodiscard]] std::vector<CapturedImage> takeCaptures();

    [[nodiscard]] RendererStats stats() const noexcept;

private:
    explicit Renderer(std::unique_ptr<vulkan::VulkanRenderer> implementation) noexcept;

    std::unique_ptr<vulkan::VulkanRenderer> m_implementation;
};

} // namespace devex::render
