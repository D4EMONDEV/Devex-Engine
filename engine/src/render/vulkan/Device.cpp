#include "Device.hpp"

#include "../Selection.hpp"

#include <devex/core/Log.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::render::vulkan {
namespace {

struct Inspection
{
    GpuCandidate candidate;
    std::uint32_t queueFamily = 0;
};

// Feature structures chained together, used both to query and to enable the required features.
struct RequiredFeatures
{
    RequiredFeatures() noexcept
    {
        features11.pNext = &features12;
        features12.pNext = &features13;
    }

    RequiredFeatures(const RequiredFeatures&) = delete;
    RequiredFeatures& operator=(const RequiredFeatures&) = delete;

    VkPhysicalDeviceVulkan13Features features13{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
    };
    VkPhysicalDeviceVulkan12Features features12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
    };
    VkPhysicalDeviceVulkan11Features features11{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
    };
    VkPhysicalDeviceFeatures2 features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &features11,
    };
};

[[nodiscard]] GpuType toGpuType(VkPhysicalDeviceType type) noexcept
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        return GpuType::Integrated;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        return GpuType::Discrete;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        return GpuType::Virtual;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        return GpuType::Cpu;
    default:
        return GpuType::Other;
    }
}

[[nodiscard]] GpuInfo describe(VkPhysicalDevice physicalDevice)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    GpuInfo info;
    info.name = properties.deviceName;
    info.type = toGpuType(properties.deviceType);
    info.apiVersion = std::format("{}.{}.{}", VK_API_VERSION_MAJOR(properties.apiVersion),
                                  VK_API_VERSION_MINOR(properties.apiVersion),
                                  VK_API_VERSION_PATCH(properties.apiVersion));

    // Driver strings only exist from Vulkan 1.2 on.
    if (properties.apiVersion >= VK_API_VERSION_1_2)
    {
        VkPhysicalDeviceVulkan12Properties properties12{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
        };
        VkPhysicalDeviceProperties2 properties2{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &properties12,
        };
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);
        info.driverName = properties12.driverName;
        info.driverVersion = properties12.driverInfo;
    }
    return info;
}

[[nodiscard]] core::Result<Inspection> inspect(VkPhysicalDevice physicalDevice,
                                               VkSurfaceKHR surface)
{
    Inspection inspection;
    inspection.candidate.info = describe(physicalDevice);
    std::string& missing = inspection.candidate.missingRequirement;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    if (properties.apiVersion < requiredApiVersion)
    {
        missing = std::format("supports Vulkan {} instead of 1.4", inspection.candidate.info.apiVersion);
        return inspection;
    }

    auto extensions = enumerate<VkExtensionProperties>("vkEnumerateDeviceExtensionProperties",
                                                       vkEnumerateDeviceExtensionProperties,
                                                       physicalDevice, nullptr);
    if (!extensions)
    {
        return std::unexpected(extensions.error());
    }
    const bool hasSwapchain =
        std::ranges::any_of(*extensions, [](const VkExtensionProperties& extension) {
            return std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        });
    if (!hasSwapchain)
    {
        missing = "no VK_KHR_swapchain extension";
        return inspection;
    }

    RequiredFeatures supported;
    vkGetPhysicalDeviceFeatures2(physicalDevice, &supported.features);
    if (supported.features13.dynamicRendering != VK_TRUE ||
        supported.features13.synchronization2 != VK_TRUE)
    {
        missing = "no dynamic rendering or synchronization2";
        return inspection;
    }
    if (supported.features12.bufferDeviceAddress != VK_TRUE ||
        supported.features11.shaderDrawParameters != VK_TRUE)
    {
        missing = "no buffer device address or shader draw parameters";
        return inspection;
    }
    if (supported.features12.runtimeDescriptorArray != VK_TRUE ||
        supported.features12.descriptorBindingPartiallyBound != VK_TRUE ||
        supported.features12.descriptorBindingSampledImageUpdateAfterBind != VK_TRUE ||
        supported.features12.shaderSampledImageArrayNonUniformIndexing != VK_TRUE)
    {
        missing = "no descriptor indexing for bindless textures";
        return inspection;
    }
    if (supported.features.features.textureCompressionBC != VK_TRUE)
    {
        missing = "no BC texture compression";
        return inspection;
    }

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    std::optional<std::uint32_t> queueFamily;
    for (std::uint32_t family = 0; family < familyCount && !queueFamily; ++family)
    {
        // Passes and environment baking use compute shaders on the same queue.
        const VkQueueFlags required = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((families[family].queueFlags & required) != required)
        {
            continue;
        }
        VkBool32 canPresent = VK_FALSE;
        DEVEX_VK_TRY(vkGetPhysicalDeviceSurfaceSupportKHR, physicalDevice, family, surface,
                     &canPresent);
        if (canPresent == VK_TRUE)
        {
            queueFamily = family;
        }
    }
    if (!queueFamily)
    {
        missing = "no queue that can draw, compute and present to the window";
        return inspection;
    }

    inspection.queueFamily = *queueFamily;
    return inspection;
}

} // namespace

core::Result<Device> Device::create(VkInstance instance, VkSurfaceKHR surface,
                                    std::string_view preferredGpu)
{
    auto physicalDevices = enumerate<VkPhysicalDevice>("vkEnumeratePhysicalDevices",
                                                       vkEnumeratePhysicalDevices, instance);
    if (!physicalDevices)
    {
        return std::unexpected(physicalDevices.error());
    }

    std::vector<GpuCandidate> candidates;
    std::vector<std::uint32_t> queueFamilies;
    for (VkPhysicalDevice physicalDevice : *physicalDevices)
    {
        core::Result<Inspection> inspection = inspect(physicalDevice, surface);
        if (!inspection)
        {
            return std::unexpected(inspection.error());
        }
        candidates.push_back(std::move(inspection->candidate));
        queueFamilies.push_back(inspection->queueFamily);
    }

    const core::Result<std::size_t> selected = selectGpu(candidates, preferredGpu);
    if (!selected)
    {
        return std::unexpected(selected.error());
    }

    Device device;
    device.m_physicalDevice = (*physicalDevices)[*selected];
    device.m_queueFamily = queueFamilies[*selected];
    device.m_gpu = std::move(candidates[*selected].info);

    const float queuePriority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = device.m_queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    RequiredFeatures enabled;
    enabled.features13.synchronization2 = VK_TRUE;
    enabled.features13.dynamicRendering = VK_TRUE;
    enabled.features12.bufferDeviceAddress = VK_TRUE;
    enabled.features12.runtimeDescriptorArray = VK_TRUE;
    enabled.features12.descriptorBindingPartiallyBound = VK_TRUE;
    enabled.features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    enabled.features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    enabled.features11.shaderDrawParameters = VK_TRUE;
    enabled.features.features.textureCompressionBC = VK_TRUE;

    RequiredFeatures supported;
    vkGetPhysicalDeviceFeatures2(device.m_physicalDevice, &supported.features);
    enabled.features.features.samplerAnisotropy = supported.features.features.samplerAnisotropy;
    enabled.features.features.depthClamp = supported.features.features.depthClamp;
    device.m_depthClamp = supported.features.features.depthClamp == VK_TRUE;

    VkPhysicalDeviceVulkan12Properties properties12{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
    };
    VkPhysicalDeviceProperties2 properties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &properties12,
    };
    vkGetPhysicalDeviceProperties2(device.m_physicalDevice, &properties);
    device.m_maxSamplerAnisotropy = supported.features.features.samplerAnisotropy == VK_TRUE
                                        ? properties.properties.limits.maxSamplerAnisotropy
                                        : 1.0f;
    device.m_maxBindlessTextures =
        std::min(properties12.maxDescriptorSetUpdateAfterBindSampledImages,
                 properties12.maxPerStageDescriptorUpdateAfterBindSampledImages);
    device.m_sampleCounts = properties.properties.limits.framebufferColorSampleCounts &
                            properties.properties.limits.framebufferDepthSampleCounts;

    const char* const extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled.features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<std::uint32_t>(std::size(extensions)),
        .ppEnabledExtensionNames = extensions,
    };

    DEVEX_VK_TRY(vkCreateDevice, device.m_physicalDevice, &createInfo, nullptr, &device.m_device);
    volkLoadDevice(device.m_device);
    vkGetDeviceQueue(device.m_device, device.m_queueFamily, 0, &device.m_queue);
    return device;
}

Device::Device(Device&& other) noexcept
    : m_physicalDevice(std::exchange(other.m_physicalDevice, VK_NULL_HANDLE))
    , m_device(std::exchange(other.m_device, VK_NULL_HANDLE))
    , m_queue(std::exchange(other.m_queue, VK_NULL_HANDLE))
    , m_queueFamily(other.m_queueFamily)
    , m_gpu(std::move(other.m_gpu))
    , m_maxSamplerAnisotropy(other.m_maxSamplerAnisotropy)
    , m_maxBindlessTextures(other.m_maxBindlessTextures)
    , m_sampleCounts(other.m_sampleCounts)
    , m_depthClamp(other.m_depthClamp)
{
}

Device& Device::operator=(Device&& other) noexcept
{
    if (this != &other)
    {
        destroy();
        m_physicalDevice = std::exchange(other.m_physicalDevice, VK_NULL_HANDLE);
        m_device = std::exchange(other.m_device, VK_NULL_HANDLE);
        m_queue = std::exchange(other.m_queue, VK_NULL_HANDLE);
        m_queueFamily = other.m_queueFamily;
        m_gpu = std::move(other.m_gpu);
        m_maxSamplerAnisotropy = other.m_maxSamplerAnisotropy;
        m_maxBindlessTextures = other.m_maxBindlessTextures;
        m_sampleCounts = other.m_sampleCounts;
        m_depthClamp = other.m_depthClamp;
    }
    return *this;
}

Device::~Device()
{
    destroy();
}

void Device::destroy() noexcept
{
    if (m_device != VK_NULL_HANDLE)
    {
        vkDestroyDevice(m_device, nullptr);
        m_device = VK_NULL_HANDLE;
    }
}

VkPhysicalDevice Device::physicalDevice() const noexcept
{
    return m_physicalDevice;
}

VkDevice Device::handle() const noexcept
{
    return m_device;
}

VkQueue Device::queue() const noexcept
{
    return m_queue;
}

std::uint32_t Device::queueFamily() const noexcept
{
    return m_queueFamily;
}

const GpuInfo& Device::gpu() const noexcept
{
    return m_gpu;
}

float Device::maxSamplerAnisotropy() const noexcept
{
    return m_maxSamplerAnisotropy;
}

std::uint32_t Device::maxBindlessTextures() const noexcept
{
    return m_maxBindlessTextures;
}

VkSampleCountFlagBits Device::sampleCount(std::uint32_t requested) const noexcept
{
    for (std::uint32_t samples = 64; samples > 1; samples /= 2)
    {
        if (samples <= requested && (m_sampleCounts & samples) != 0)
        {
            return static_cast<VkSampleCountFlagBits>(samples);
        }
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

bool Device::supportsDepthClamp() const noexcept
{
    return m_depthClamp;
}

} // namespace devex::render::vulkan
