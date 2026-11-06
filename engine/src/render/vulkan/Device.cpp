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
        missing = std::format("supports Vulkan {} instead of 1.{}", inspection.candidate.info.apiVersion,
                              VK_API_VERSION_MINOR(requiredApiVersion));
        return inspection;
    }
    if (properties.limits.maxPushConstantsSize < requiredPushConstantsSize)
    {
        missing = std::format("allows {} bytes of push constants instead of {}",
                              properties.limits.maxPushConstantsSize, requiredPushConstantsSize);
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
                                    std::string_view preferredGpu, bool canChooseExclusiveDisplay)
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

    RequiredFeatures supported;
    vkGetPhysicalDeviceFeatures2(device.m_physicalDevice, &supported.features);
    // Desktop GPUs read BC textures; phone GPUs do not, and get their textures in another format.
    enabled.features.features.textureCompressionBC = supported.features.features.textureCompressionBC;
    device.m_textureCompressionBC = supported.features.features.textureCompressionBC == VK_TRUE;
    enabled.features.features.textureCompressionASTC_LDR = supported.features.features.textureCompressionASTC_LDR;
    device.m_textureCompressionAstc = supported.features.features.textureCompressionASTC_LDR == VK_TRUE;
    enabled.features.features.samplerAnisotropy = supported.features.features.samplerAnisotropy;
    enabled.features.features.depthClamp = supported.features.features.depthClamp;
    device.m_depthClamp = supported.features.features.depthClamp == VK_TRUE;
    enabled.features.features.drawIndirectFirstInstance = supported.features.features.drawIndirectFirstInstance;
    device.m_drawIndirectFirstInstance = supported.features.features.drawIndirectFirstInstance == VK_TRUE;

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
    // Each texture has its sampler beside it, and a few more samplers are bound.
    device.m_maxBindlessTextures =
        std::min({properties12.maxDescriptorSetUpdateAfterBindSampledImages,
                  properties12.maxPerStageDescriptorUpdateAfterBindSampledImages,
                  properties12.maxDescriptorSetUpdateAfterBindSamplers - 8,
                  properties12.maxPerStageDescriptorUpdateAfterBindSamplers - 8});
    device.m_sampleCounts = properties.properties.limits.framebufferColorSampleCounts &
                            properties.properties.limits.framebufferDepthSampleCounts;

    std::vector<const char*> extensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    // Tools draw through a UNORM view of the sRGB swapchain images when the driver allows it.
    if (auto available = enumerate<VkExtensionProperties>("vkEnumerateDeviceExtensionProperties",
                                                          vkEnumerateDeviceExtensionProperties,
                                                          device.m_physicalDevice, nullptr))
    {
        device.m_mutableSwapchainFormat =
            std::ranges::any_of(*available, [](const VkExtensionProperties& extension) {
                return std::string_view(extension.extensionName) == VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME;
            });
        if (device.m_mutableSwapchainFormat)
        {
            extensions.push_back(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME);
        }
        // Spelled out: its constant lives in a header that comes with windows.h.
        device.m_exclusiveDisplayChoice =
            canChooseExclusiveDisplay && std::ranges::any_of(*available, [](const VkExtensionProperties& extension) {
                return std::string_view(extension.extensionName) == "VK_EXT_full_screen_exclusive";
            });
        if (device.m_exclusiveDisplayChoice)
        {
            extensions.push_back("VK_EXT_full_screen_exclusive");
        }
    }
    const VkDeviceCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled.features,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
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
    , m_textureCompressionBC(other.m_textureCompressionBC)
    , m_textureCompressionAstc(other.m_textureCompressionAstc)
    , m_drawIndirectFirstInstance(other.m_drawIndirectFirstInstance)
    , m_mutableSwapchainFormat(other.m_mutableSwapchainFormat)
    , m_exclusiveDisplayChoice(other.m_exclusiveDisplayChoice)
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
        m_textureCompressionBC = other.m_textureCompressionBC;
        m_textureCompressionAstc = other.m_textureCompressionAstc;
        m_drawIndirectFirstInstance = other.m_drawIndirectFirstInstance;
        m_mutableSwapchainFormat = other.m_mutableSwapchainFormat;
        m_exclusiveDisplayChoice = other.m_exclusiveDisplayChoice;
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

bool Device::supportsTextureCompressionBC() const noexcept
{
    return m_textureCompressionBC;
}

bool Device::supportsTextureCompressionAstc() const noexcept
{
    return m_textureCompressionAstc;
}

bool Device::supportsDrawIndirectFirstInstance() const noexcept
{
    return m_drawIndirectFirstInstance;
}

bool Device::supportsMutableSwapchainFormat() const noexcept
{
    return m_mutableSwapchainFormat;
}

bool Device::supportsExclusiveDisplayChoice() const noexcept
{
    return m_exclusiveDisplayChoice;
}

core::Result<std::string> Device::describeAll(VkInstance instance, VkSurfaceKHR surface, std::string_view inUse)
{
    auto physicalDevices = enumerate<VkPhysicalDevice>("vkEnumeratePhysicalDevices",
                                                       vkEnumeratePhysicalDevices, instance);
    if (!physicalDevices)
    {
        return std::unexpected(physicalDevices.error());
    }
    if (physicalDevices->empty())
    {
        return std::string("No GPU offers Vulkan.\n");
    }
    const auto answer = [](VkBool32 value) { return value == VK_TRUE ? "yes" : "no"; };
    std::string report;
    for (VkPhysicalDevice physicalDevice : *physicalDevices)
    {
        const core::Result<Inspection> inspection = inspect(physicalDevice, surface);
        if (!inspection)
        {
            report += std::format("A GPU cannot be read: {}\n", inspection.error());
            continue;
        }
        const GpuInfo& info = inspection->candidate.info;
        const std::string& missing = inspection->candidate.missingRequirement;
        VkPhysicalDeviceVulkan12Properties properties12{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES,
        };
        VkPhysicalDeviceProperties2 properties{
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
            .pNext = &properties12,
        };
        vkGetPhysicalDeviceProperties2(physicalDevice, &properties);
        const VkPhysicalDeviceLimits& limits = properties.properties.limits;

        report += std::format("{}{}\n", info.name, !inUse.empty() && info.name == inUse ? " (in use)" : "");
        report += std::format("  Kind: {}, vendor {:#06x}, device {:#06x}\n", toString(info.type),
                              properties.properties.vendorID, properties.properties.deviceID);
        report += std::format("  Vulkan {}, driver: {} {}\n", info.apiVersion, info.driverName, info.driverVersion);
        report += std::format("  Can run Devex: {}\n", missing.empty() ? std::string("yes") : "no, " + missing);

        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memory);
        report += "  Memory:";
        for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap)
        {
            const bool onGpu = (memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0;
            report += std::format(" {:.1f} GiB{}", static_cast<double>(memory.memoryHeaps[heap].size) / (1024.0 * 1024.0 * 1024.0),
                                  onGpu ? " for the GPU" : " shared");
            report += heap + 1 < memory.memoryHeapCount ? "," : "\n";
        }

        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(physicalDevice, &features);
        report += std::format("  Optional: BC textures {}, ASTC textures {}, anisotropy {}, depth clamp {}, indirect first instance {}\n",
                              answer(features.textureCompressionBC), answer(features.textureCompressionASTC_LDR),
                              answer(features.samplerAnisotropy), answer(features.depthClamp),
                              answer(features.drawIndirectFirstInstance));
        report += std::format("  Limits: 2D images {}, push constants {}, bindless textures {}, compute invocations {}, color samples mask {:#x}\n",
                              limits.maxImageDimension2D, limits.maxPushConstantsSize,
                              properties12.maxPerStageDescriptorUpdateAfterBindSampledImages,
                              limits.maxComputeWorkGroupInvocations, limits.framebufferColorSampleCounts);
        if (const auto extensions = enumerate<VkExtensionProperties>("vkEnumerateDeviceExtensionProperties",
                                                                     vkEnumerateDeviceExtensionProperties, physicalDevice, nullptr))
        {
            const auto has = [&extensions](std::string_view name) {
                return std::ranges::any_of(*extensions, [name](const VkExtensionProperties& extension) {
                    return std::string_view(extension.extensionName) == name;
                });
            };
            report += std::format("  Extensions: mutable swapchain format {}, choice of the exclusive display {}\n",
                                  has(VK_KHR_SWAPCHAIN_MUTABLE_FORMAT_EXTENSION_NAME) ? "yes" : "no",
                                  has("VK_EXT_full_screen_exclusive") ? "yes" : "no");
        }
        // Only a GPU that presents to this surface can be asked about it.
        if (!missing.empty())
        {
            continue;
        }
        if (const auto modes = enumerate<VkPresentModeKHR>("vkGetPhysicalDeviceSurfacePresentModesKHR",
                                                           vkGetPhysicalDeviceSurfacePresentModesKHR, physicalDevice, surface))
        {
            report += "  Present modes:";
            for (const VkPresentModeKHR mode : *modes)
            {
                switch (mode)
                {
                case VK_PRESENT_MODE_FIFO_KHR:
                    report += " FIFO";
                    break;
                case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
                    report += " FIFO-relaxed";
                    break;
                case VK_PRESENT_MODE_MAILBOX_KHR:
                    report += " mailbox";
                    break;
                case VK_PRESENT_MODE_IMMEDIATE_KHR:
                    report += " immediate";
                    break;
                default:
                    report += std::format(" {}", static_cast<int>(mode));
                    break;
                }
            }
            report += "\n";
        }
        if (const auto formats = enumerate<VkSurfaceFormatKHR>("vkGetPhysicalDeviceSurfaceFormatsKHR",
                                                               vkGetPhysicalDeviceSurfaceFormatsKHR, physicalDevice, surface))
        {
            report += "  Window formats:";
            for (const VkSurfaceFormatKHR& format : *formats)
            {
                report += std::format(" {}/{}", static_cast<int>(format.format), static_cast<int>(format.colorSpace));
            }
            report += "\n";
        }
    }
    return report;
}

} // namespace devex::render::vulkan
