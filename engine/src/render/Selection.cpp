#include "Selection.hpp"

#include <devex/core/Log.hpp>

#include <algorithm>
#include <cctype>
#include <format>

namespace devex::render {
namespace {

[[nodiscard]] int capabilityRank(GpuType type) noexcept
{
    switch (type)
    {
    case GpuType::Discrete:
        return 4;
    case GpuType::Integrated:
        return 3;
    case GpuType::Virtual:
        return 2;
    case GpuType::Cpu:
        return 1;
    case GpuType::Other:
        return 0;
    }
    return 0;
}

[[nodiscard]] bool containsIgnoringCase(std::string_view text, std::string_view part) noexcept
{
    const auto equalIgnoringCase = [](char left, char right) {
        return std::tolower(static_cast<unsigned char>(left)) ==
               std::tolower(static_cast<unsigned char>(right));
    };
    return !std::ranges::search(text, part, equalIgnoringCase).empty();
}

[[nodiscard]] bool isCompatible(const GpuCandidate& candidate) noexcept
{
    return candidate.missingRequirement.empty();
}

} // namespace

std::string_view toString(GpuType type) noexcept
{
    switch (type)
    {
    case GpuType::Other:
        return "other";
    case GpuType::Integrated:
        return "integrated";
    case GpuType::Discrete:
        return "discrete";
    case GpuType::Virtual:
        return "virtual";
    case GpuType::Cpu:
        return "CPU";
    }
    return "unknown";
}

std::string_view toString(PresentMode mode) noexcept
{
    switch (mode)
    {
    case PresentMode::Fifo:
        return "FIFO";
    case PresentMode::Mailbox:
        return "mailbox";
    case PresentMode::Immediate:
        return "immediate";
    }
    return "unknown";
}

core::Result<std::size_t> selectGpu(std::span<const GpuCandidate> candidates,
                                    std::string_view preferredName)
{
    if (!preferredName.empty())
    {
        for (std::size_t index = 0; index < candidates.size(); ++index)
        {
            if (isCompatible(candidates[index]) &&
                containsIgnoringCase(candidates[index].info.name, preferredName))
            {
                return index;
            }
        }
        DEVEX_LOG_WARNING("No compatible GPU matches '{}', choosing one automatically",
                          preferredName);
    }

    std::optional<std::size_t> best;
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        if (isCompatible(candidates[index]) &&
            (!best || capabilityRank(candidates[index].info.type) >
                          capabilityRank(candidates[*best].info.type)))
        {
            best = index;
        }
    }
    if (best)
    {
        return *best;
    }

    if (candidates.empty())
    {
        return core::makeError(core::ErrorCode::Unsupported, "no GPU with Vulkan support found");
    }
    std::string reasons;
    for (const GpuCandidate& candidate : candidates)
    {
        reasons += std::format("{}{}: {}", reasons.empty() ? "" : "; ", candidate.info.name,
                               candidate.missingRequirement);
    }
    return core::makeError(core::ErrorCode::Unsupported, "no GPU can run Devex ({})", reasons);
}

PresentMode choosePresentMode(PresentMode requested,
                              std::span<const PresentMode> supported) noexcept
{
    return std::ranges::contains(supported, requested) ? requested : PresentMode::Fifo;
}

math::Extent2D chooseSwapchainExtent(std::optional<math::Extent2D> surfaceExtent,
                                     math::Extent2D minExtent, math::Extent2D maxExtent,
                                     math::Extent2D windowPixelSize) noexcept
{
    if (surfaceExtent)
    {
        return *surfaceExtent;
    }
    return {std::clamp(windowPixelSize.width, minExtent.width, maxExtent.width),
            std::clamp(windowPixelSize.height, minExtent.height, maxExtent.height)};
}

std::uint32_t chooseImageCount(std::uint32_t minCount, std::uint32_t maxCount) noexcept
{
    const std::uint32_t count = minCount + 1;
    return maxCount == 0 ? count : std::min(count, maxCount);
}

} // namespace devex::render
