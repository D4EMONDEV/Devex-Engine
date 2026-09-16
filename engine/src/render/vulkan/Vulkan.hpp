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
