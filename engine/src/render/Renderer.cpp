#include "vulkan/VulkanRenderer.hpp"

#include <devex/core/Assert.hpp>
#include <devex/render/Renderer.hpp>

#include <utility>

namespace devex::render {

core::Result<Renderer> Renderer::create(const platform::Platform& platform,
                                        platform::Window& window, const RendererConfig& config)
{
    core::Result<std::unique_ptr<vulkan::VulkanRenderer>> implementation =
        vulkan::VulkanRenderer::create(platform, window, config);
    if (!implementation)
    {
        return std::unexpected(implementation.error());
    }
    return Renderer(std::move(*implementation));
}

Renderer::Renderer(std::unique_ptr<vulkan::VulkanRenderer> implementation) noexcept
    : m_implementation(std::move(implementation))
{
}

Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer& Renderer::operator=(Renderer&& other) noexcept = default;
Renderer::~Renderer() = default;

const GpuInfo& Renderer::gpu() const noexcept
{
    DEVEX_ASSERT(m_implementation != nullptr);
    return m_implementation->gpu();
}

PresentMode Renderer::presentMode() const noexcept
{
    DEVEX_ASSERT(m_implementation != nullptr);
    return m_implementation->presentMode();
}

core::Result<MeshHandle> Renderer::createMesh(const asset::MeshData& mesh)
{
    DEVEX_ASSERT(m_implementation != nullptr);
    return m_implementation->createMesh(mesh);
}

void Renderer::destroyMesh(MeshHandle mesh)
{
    DEVEX_ASSERT(m_implementation != nullptr);
    m_implementation->destroyMesh(mesh);
}

RenderWorld& Renderer::beginFrame() noexcept
{
    DEVEX_ASSERT(m_implementation != nullptr);
    return m_implementation->beginFrame();
}

core::Result<void> Renderer::endFrame()
{
    DEVEX_ASSERT(m_implementation != nullptr);
    return m_implementation->endFrame();
}

} // namespace devex::render
