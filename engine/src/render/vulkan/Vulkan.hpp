#pragma once

#include <devex/core/Error.hpp>

#include <volk.h>

#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

namespace devex::render::vulkan {

// Every Devex device requires this version.
inline constexpr std::uint32_t requiredApiVersion = VK_API_VERSION_1_4;
// The Vulkan loader may be older than the drivers: Vulkan 1.4 added commands of devices only, which
// the engine takes from the driver (volkLoadDevice), and a loader of 1.1 or later accepts any version
// the application asks for. Ubuntu 24.04 comes with a loader of Vulkan 1.3 and drivers of 1.4.
inline constexpr std::uint32_t requiredLoaderVersion = VK_API_VERSION_1_3;

[[nodiscard]] std::string_view toString(VkResult result) noexcept;

// Error for a failed Vulkan call, naming the call and its result.
[[nodiscard]] std::unexpected<core::Error> vulkanError(std::string_view call, VkResult result);

// Fills a vector through the two-call enumeration pattern of the Vulkan API.
template <typename T, typename Function, typename... Args>
[[nodiscard]] core::Result<std::vector<T>> enumerate(std::string_view call, Function function,
                                                     Args... args)
{
    std::vector<T> items;
    VkResult result = VK_INCOMPLETE;
    while (result == VK_INCOMPLETE)
    {
        std::uint32_t count = 0;
        result = function(args..., &count, nullptr);
        if (result < VK_SUCCESS)
        {
            return vulkanError(call, result);
        }
        items.resize(count);
        result = function(args..., &count, items.data());
        if (result < VK_SUCCESS)
        {
            return vulkanError(call, result);
        }
        items.resize(count);
    }
    return items;
}

} // namespace devex::render::vulkan

// Calls a Vulkan function and returns its error from the enclosing function when it fails.
#define DEVEX_VK_TRY(function, ...)                                                                \
    do                                                                                             \
    {                                                                                              \
        if (const VkResult devexVkResult = function(__VA_ARGS__); devexVkResult < VK_SUCCESS)      \
        {                                                                                          \
            return ::devex::render::vulkan::vulkanError(#function, devexVkResult);                 \
        }                                                                                          \
    } while (false)
