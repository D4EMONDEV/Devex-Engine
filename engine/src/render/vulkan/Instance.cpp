#include "Instance.hpp"

#include <devex/core/Log.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace devex::render::vulkan {
namespace {

constexpr const char* validationLayerName = "VK_LAYER_KHRONOS_validation";

VkBool32 VKAPI_CALL forwardDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                        VkDebugUtilsMessageTypeFlagsEXT types,
                                        const VkDebugUtilsMessengerCallbackDataEXT* data,
                                        void* /*userData*/)
{
    // General warnings mostly come from the loader about third-party layers such as overlays and
    // capture tools, which the application cannot fix.
    const bool isError = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0;
    const bool isGeneralOnly = types == VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
    const core::LogLevel level = isError         ? core::LogLevel::Error
                                 : isGeneralOnly ? core::LogLevel::Debug
                                                 : core::LogLevel::Warning;
    core::logMessage(level, std::format("Vulkan: {}", data->pMessage != nullptr ? data->pMessage
                                                                                : "(no message)"));
    return VK_FALSE;
}

[[nodiscard]] VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo() noexcept
{
    return {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = &forwardDebugMessage,
    };
}

[[nodiscard]] core::Result<bool> isLayerAvailable(std::string_view name)
{
    auto layers = enumerate<VkLayerProperties>("vkEnumerateInstanceLayerProperties",
                                               vkEnumerateInstanceLayerProperties);
    if (!layers)
    {
        return std::unexpected(layers.error());
    }
    return std::ranges::any_of(*layers, [name](const VkLayerProperties& layer) {
        return std::string_view(layer.layerName) == name;
    });
}

} // namespace

core::Result<Instance> Instance::create(const InstanceConfig& config)
{
    if (const VkResult loaded = volkInitialize(); loaded != VK_SUCCESS)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "no Vulkan loader is installed ({})", toString(loaded));
    }

    const std::uint32_t loaderVersion = volkGetInstanceVersion();
    if (loaderVersion < requiredApiVersion)
    {
        return core::makeError(core::ErrorCode::Unsupported,
                               "the Vulkan loader supports version {}.{}, Devex needs 1.4",
                               VK_API_VERSION_MAJOR(loaderVersion),
                               VK_API_VERSION_MINOR(loaderVersion));
    }

    std::vector<const char*> extensions(config.requiredExtensions.begin(),
                                        config.requiredExtensions.end());
    std::vector<const char*> layers;
    bool validation = false;
    if (config.validation)
    {
        const core::Result<bool> available = isLayerAvailable(validationLayerName);
        if (!available)
        {
            return std::unexpected(available.error());
        }
        if (*available)
        {
            layers.push_back(validationLayerName);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
            validation = true;
        }
        else
        {
            DEVEX_LOG_WARNING("Vulkan validation requested, but {} is not installed", validationLayerName);
        }
    }

    const std::string applicationName(config.applicationName);
    const VkApplicationInfo applicationInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationName.c_str(),
        .pEngineName = "Devex",
        .apiVersion = requiredApiVersion,
    };

    // The messenger in pNext also reports problems during instance creation and destruction.
    const VkDebugUtilsMessengerCreateInfoEXT messengerInfo = messengerCreateInfo();
    const VkBool32 enabled = VK_TRUE;
    const VkLayerSettingEXT layerSettings[] = {
        {validationLayerName, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1, &enabled},
    };
    const VkLayerSettingsCreateInfoEXT layerSettingsInfo{
        .sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT,
        .pNext = &messengerInfo,
        .settingCount = static_cast<std::uint32_t>(std::size(layerSettings)),
        .pSettings = layerSettings,
    };

    const VkInstanceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = validation ? &layerSettingsInfo : nullptr,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };

    Instance instance;
    DEVEX_VK_TRY(vkCreateInstance, &createInfo, nullptr, &instance.m_instance);
    volkLoadInstance(instance.m_instance);

    if (validation)
    {
        DEVEX_VK_TRY(vkCreateDebugUtilsMessengerEXT, instance.m_instance, &messengerInfo, nullptr,
                     &instance.m_messenger);
        instance.m_validation = true;
    }
    DEVEX_LOG_DEBUG("Vulkan instance created (validation {})", validation ? "on" : "off");
    return instance;
}

Instance::Instance(Instance&& other) noexcept
    : m_instance(std::exchange(other.m_instance, VK_NULL_HANDLE))
    , m_messenger(std::exchange(other.m_messenger, VK_NULL_HANDLE))
    , m_validation(std::exchange(other.m_validation, false))
{
}

Instance& Instance::operator=(Instance&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_instance = std::exchange(other.m_instance, VK_NULL_HANDLE);
        m_messenger = std::exchange(other.m_messenger, VK_NULL_HANDLE);
        m_validation = std::exchange(other.m_validation, false);
    }
    return *this;
}

Instance::~Instance()
{
    destroy();
}

void Instance::destroy() noexcept
{
    if (m_messenger != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
        m_messenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

VkInstance Instance::handle() const noexcept
{
    return m_instance;
}

bool Instance::isValidationEnabled() const noexcept
{
    return m_validation;
}

Surface::Surface(VkInstance instance, VkSurfaceKHR surface) noexcept
    : m_instance(instance)
    , m_surface(surface)
{
}

Surface::Surface(Surface&& other) noexcept
    : m_instance(std::exchange(other.m_instance, VK_NULL_HANDLE))
    , m_surface(std::exchange(other.m_surface, VK_NULL_HANDLE))
{
}

Surface& Surface::operator=(Surface&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_instance = std::exchange(other.m_instance, VK_NULL_HANDLE);
        m_surface = std::exchange(other.m_surface, VK_NULL_HANDLE);
    }
    return *this;
}

Surface::~Surface()
{
    destroy();
}

void Surface::destroy() noexcept
{
    if (m_surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

VkSurfaceKHR Surface::handle() const noexcept
{
    return m_surface;
}

} // namespace devex::render::vulkan
