#pragma once

#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>
#include <devex/render/Gpu.hpp>
#include <devex/render/Renderer.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

// Choices made while creating Vulkan objects, expressed without Vulkan types so that they can be
// tested without a GPU.
namespace devex::render {

struct GpuCandidate
{
    GpuInfo info;
    // Empty when the GPU can run Devex, otherwise the first requirement it misses.
    std::string missingRequirement;
};

// Picks the GPU whose name contains preferredName if it is compatible, otherwise the most
// capable compatible GPU: discrete, then integrated, virtual and CPU implementations.
[[nodiscard]] core::Result<std::size_t> selectGpu(std::span<const GpuCandidate> candidates,
                                                  std::string_view preferredName);

[[nodiscard]] PresentMode choosePresentMode(PresentMode requested,
                                            std::span<const PresentMode> supported) noexcept;

// Uses the surface extent when the surface imposes one, otherwise the window size clamped to the
// limits of the surface.
[[nodiscard]] math::Extent2D chooseSwapchainExtent(std::optional<math::Extent2D> surfaceExtent,
                                                   math::Extent2D minExtent,
                                                   math::Extent2D maxExtent,
                                                   math::Extent2D windowPixelSize) noexcept;

// One image more than the minimum avoids waiting on the driver; a maximum of 0 means unbounded.
[[nodiscard]] std::uint32_t chooseImageCount(std::uint32_t minCount,
                                             std::uint32_t maxCount) noexcept;

} // namespace devex::render
