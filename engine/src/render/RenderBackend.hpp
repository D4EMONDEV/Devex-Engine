#pragma once

// What the renderer of an API does behind the facade Renderer: Vulkan's, and WebGPU's to come. The facade forwards to it,
// and Renderer::create chooses the one the platform has.
#include <devex/render/Renderer.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace devex::render {

class RenderBackend
{
public:
    RenderBackend() = default;
    virtual ~RenderBackend() = default;

    RenderBackend(const RenderBackend&) = delete;
    RenderBackend& operator=(const RenderBackend&) = delete;

    [[nodiscard]] virtual const GpuInfo& gpu() const noexcept = 0;
    [[nodiscard]] virtual PresentMode presentMode() const noexcept = 0;
    virtual void setPresentMode(PresentMode mode) = 0;
    [[nodiscard]] virtual core::Result<std::uint32_t> addWindowTarget(platform::Window& window) = 0;
    virtual void removeWindowTarget(std::uint32_t target) = 0;
    [[nodiscard]] virtual core::Result<std::string> describeGraphics() const = 0;

    [[nodiscard]] virtual core::Result<MeshHandle> createMesh(const asset::MeshData& mesh) = 0;
    [[nodiscard]] virtual bool isReady(MeshHandle mesh) const noexcept = 0;
    [[nodiscard]] virtual bool isReady(TextureHandle texture) const noexcept = 0;
    virtual void destroyMesh(MeshHandle mesh) = 0;
    [[nodiscard]] virtual std::uint32_t submeshCount(MeshHandle mesh) const noexcept = 0;

    [[nodiscard]] virtual core::Result<TextureHandle> createTexture(const asset::TextureData& texture) = 0;
    virtual void destroyTexture(TextureHandle texture) = 0;
    [[nodiscard]] virtual core::Result<void> updateTexture(TextureHandle texture, std::uint32_t top, std::uint32_t height,
                                                           std::span<const std::byte> rows) = 0;

    [[nodiscard]] virtual core::Result<ReflectionProbeHandle> createReflectionProbe(const asset::ReflectionProbeData& probe) = 0;
    virtual void destroyReflectionProbe(ReflectionProbeHandle probe) = 0;
    [[nodiscard]] virtual core::Result<IrradianceVolumeHandle> createIrradianceVolume(const asset::IrradianceVolumeData& volume) = 0;
    virtual void destroyIrradianceVolume(IrradianceVolumeHandle volume) = 0;

    [[nodiscard]] virtual MaterialHandle createMaterial(const MaterialDesc& material) = 0;
    virtual void updateMaterial(MaterialHandle handle, const MaterialDesc& material) = 0;
    virtual void destroyMaterial(MaterialHandle material) = 0;

    [[nodiscard]] virtual core::Result<ShaderHandle> createShader(const asset::ShaderData& shader) = 0;
    [[nodiscard]] virtual core::Result<void> updateShader(ShaderHandle handle, const asset::ShaderData& shader) = 0;
    virtual void destroyShader(ShaderHandle shader) = 0;

    [[nodiscard]] virtual RenderWorld& beginFrame() noexcept = 0;
    virtual void resetSceneHistory() noexcept = 0;
    [[nodiscard]] virtual core::Result<void> endFrame() = 0;
    [[nodiscard]] virtual std::vector<PickResult> takePickResults() = 0;
    // Of the main image, or of the view of that id.
    [[nodiscard]] virtual std::uint64_t requestCapture(std::uint32_t maxWidth, std::uint32_t maxHeight, std::optional<std::uint32_t> view) = 0;
    [[nodiscard]] virtual std::uint64_t requestWindowCapture() = 0;
    [[nodiscard]] virtual std::vector<CapturedImage> takeCaptures() = 0;

    [[nodiscard]] virtual RendererStats stats() const noexcept = 0;
};

} // namespace devex::render
