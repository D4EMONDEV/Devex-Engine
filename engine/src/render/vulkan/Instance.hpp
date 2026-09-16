#pragma once

#include "Vulkan.hpp"

#include <span>
#include <string_view>

namespace devex::render::vulkan {

struct InstanceConfig
{
    std::string_view applicationName;
    std::span<const char* const> requiredExtensions;
    bool validation = false;
};

// Loads Vulkan and owns the VkInstance, with a debug messenger forwarding validation messages to
// the Devex log.
class Instance
{
public:
    [[nodiscard]] static core::Result<Instance> create(const InstanceConfig& config);

    Instance(Instance&& other) noexcept;
    Instance& operator=(Instance&& other) noexcept;
    ~Instance();

    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;

    [[nodiscard]] VkInstance handle() const noexcept;
    [[nodiscard]] bool isValidationEnabled() const noexcept;

private:
    Instance() = default;
    void destroy() noexcept;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    bool m_validation = false;
};

// Owns a VkSurfaceKHR created on an instance.
class Surface
{
public:
    Surface(VkInstance instance, VkSurfaceKHR surface) noexcept;

    Surface(Surface&& other) noexcept;
    Surface& operator=(Surface&& other) noexcept;
    ~Surface();

    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;

    [[nodiscard]] VkSurfaceKHR handle() const noexcept;

private:
    void destroy() noexcept;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
};

} // namespace devex::render::vulkan
