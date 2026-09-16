#include <render/Selection.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>

using devex::math::Extent2D;
using devex::render::GpuCandidate;
using devex::render::GpuType;
using devex::render::PresentMode;

namespace {

GpuCandidate gpu(const char* name, GpuType type, const char* missingRequirement = "")
{
    GpuCandidate candidate;
    candidate.info.name = name;
    candidate.info.type = type;
    candidate.missingRequirement = missingRequirement;
    return candidate;
}

} // namespace

TEST_CASE("The most capable compatible GPU is selected", "[render][selection]")
{
    const std::array candidates{
        gpu("Software Renderer", GpuType::Cpu),
        gpu("Intel Graphics", GpuType::Integrated),
        gpu("Old Discrete", GpuType::Discrete, "supports Vulkan 1.2.0 instead of 1.4"),
        gpu("NVIDIA GeForce RTX 4060", GpuType::Discrete),
    };

    const auto selected = devex::render::selectGpu(candidates, "");

    REQUIRE(selected.has_value());
    CHECK(*selected == 3);
}

TEST_CASE("A preferred GPU name wins when that GPU is compatible", "[render][selection]")
{
    const std::array candidates{
        gpu("NVIDIA GeForce RTX 4060", GpuType::Discrete),
        gpu("Intel Graphics", GpuType::Integrated),
        gpu("Old Discrete", GpuType::Discrete, "no dynamic rendering or synchronization2"),
    };

    CHECK(devex::render::selectGpu(candidates, "intel").value() == 1);
    // An incompatible or unknown preference falls back to automatic selection.
    CHECK(devex::render::selectGpu(candidates, "old discrete").value() == 0);
    CHECK(devex::render::selectGpu(candidates, "AMD").value() == 0);
}

TEST_CASE("Selection fails with every reason when no GPU is compatible", "[render][selection]")
{
    const std::array candidates{
        gpu("First", GpuType::Discrete, "no VK_KHR_swapchain extension"),
        gpu("Second", GpuType::Integrated, "supports Vulkan 1.3.0 instead of 1.4"),
    };

    const auto selected = devex::render::selectGpu(candidates, "");

    REQUIRE_FALSE(selected.has_value());
    CHECK(selected.error().code == devex::core::ErrorCode::Unsupported);
    CHECK(selected.error().message ==
          "no GPU can run Devex (First: no VK_KHR_swapchain extension; "
          "Second: supports Vulkan 1.3.0 instead of 1.4)");
    CHECK_FALSE(devex::render::selectGpu({}, "").has_value());
}

TEST_CASE("Unsupported present modes fall back to FIFO", "[render][selection]")
{
    const std::array supported{PresentMode::Fifo, PresentMode::Immediate};

    CHECK(devex::render::choosePresentMode(PresentMode::Immediate, supported) ==
          PresentMode::Immediate);
    CHECK(devex::render::choosePresentMode(PresentMode::Mailbox, supported) == PresentMode::Fifo);
}

TEST_CASE("Swapchain extent follows the surface or the clamped window", "[render][selection]")
{
    const Extent2D minExtent{1, 1};
    const Extent2D maxExtent{4096, 2160};

    CHECK(devex::render::chooseSwapchainExtent(Extent2D{800, 600}, minExtent, maxExtent,
                                               Extent2D{1280, 720}) == Extent2D{800, 600});
    CHECK(devex::render::chooseSwapchainExtent(std::nullopt, minExtent, maxExtent,
                                               Extent2D{1280, 720}) == Extent2D{1280, 720});
    CHECK(devex::render::chooseSwapchainExtent(std::nullopt, minExtent, maxExtent,
                                               Extent2D{8000, 0}) == Extent2D{4096, 1});
}

TEST_CASE("Swapchain image count asks for one more than the minimum", "[render][selection]")
{
    CHECK(devex::render::chooseImageCount(2, 0) == 3);
    CHECK(devex::render::chooseImageCount(2, 8) == 3);
    CHECK(devex::render::chooseImageCount(3, 3) == 3);
}
