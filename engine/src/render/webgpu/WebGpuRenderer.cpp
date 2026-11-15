#include "WebGpuRenderer.hpp"

#include "BlockDecoding.hpp"
#include "WebGpuHelpers.hpp"
#include "WebGpuShaders.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/asset/ShaderData.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Culling.hpp>
#include <devex/render/Photometry.hpp>
#include <devex/render/PostProcess.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <source_location>
#include <string>
#include <utility>

namespace devex::render::webgpu {
namespace {

constexpr wgpu::TextureFormat sceneFormat = wgpu::TextureFormat::RGBA16Float;
// Captures and window captures are drawn into this, then read back.
constexpr wgpu::TextureFormat captureFormat = wgpu::TextureFormat::RGBA8UnormSrgb;
// Dynamic offsets of uniform buffers fall on this.
constexpr std::uint64_t uniformStride = 256;
constexpr std::uint32_t luminanceGridWidth = 64;
constexpr std::uint32_t luminanceGridHeight = 36;
constexpr std::uint32_t noTexture = 0xFFFFFFFFU;

// What the shaders read, laid out as their WGSL does.
struct FrameData
{
    math::Mat4 viewProjection{1.0f};
    math::Vec4 canvasModulate{1.0f};
};
struct SpriteDrawData
{
    std::uint32_t first = 0;
    std::uint32_t padding[3]{};
};
struct UiDrawData
{
    math::Vec4 rect{0.0f};
    math::Vec2 inverseViewport{0.0f};
    float radius = 0.0f;
    float sharpness = 1.0f;
    std::uint32_t kind = 0;
    std::uint32_t displaySpace = 0;
    std::uint32_t padding[2]{};
};
struct TonemapData
{
    std::uint32_t tonemapper = 0;
    float vignette = 0.0f;
    float grain = 0.0f;
    float time = 0.0f;
    float targetWidth = 0.0f;
    float targetHeight = 0.0f;
    std::uint32_t encode = 0;
    float saturation = 1.0f;
    float contrast = 1.0f;
    float padding[3]{};
    math::Vec4 filter{1.0f};
    math::Vec4 balance{1.0f};
};
struct GridData
{
    std::uint32_t width = luminanceGridWidth;
    std::uint32_t height = luminanceGridHeight;
    std::uint32_t padding[2]{};
};
static_assert(sizeof(FrameData) == 80 && sizeof(SpriteDrawData) == 16 && sizeof(UiDrawData) == 48 &&
              sizeof(TonemapData) == 80 && sizeof(GridData) == 16);

[[nodiscard]] std::string text(wgpu::StringView view)
{
    return std::string(std::string_view(view));
}

void reportError(wgpu::ErrorType type, wgpu::StringView message)
{
    static_cast<void>(type);
    core::logMessage(core::LogLevel::Error, std::string_view(message), std::source_location::current(), "webgpu");
}

[[nodiscard]] wgpu::DeviceDescriptor deviceDescriptor(const std::vector<wgpu::FeatureName>& features)
{
    wgpu::DeviceDescriptor descriptor;
    descriptor.label = "Devex";
    descriptor.requiredFeatureCount = features.size();
    descriptor.requiredFeatures = features.data();
    descriptor.SetUncapturedErrorCallback(
        [](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) { reportError(type, message); });
    descriptor.SetDeviceLostCallback(wgpu::CallbackMode::AllowSpontaneous,
                                     [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView message) {
                                         // Destroying the renderer loses the device too, which says nothing.
                                         if (reason == wgpu::DeviceLostReason::Unknown ||
                                             reason == wgpu::DeviceLostReason::FailedCreation)
                                         {
                                             core::logMessage(core::LogLevel::Error,
                                                              std::format("the GPU was lost: {}", std::string_view(message)),
                                                              std::source_location::current(), "webgpu");
                                         }
                                     });
    return descriptor;
}

// What the device needs of what the adapter offers.
[[nodiscard]] std::vector<wgpu::FeatureName> wantedFeatures(const wgpu::Adapter& adapter)
{
    std::vector<wgpu::FeatureName> features;
    for (const wgpu::FeatureName feature :
         {wgpu::FeatureName::TextureCompressionBC, wgpu::FeatureName::TextureCompressionASTC})
    {
        if (adapter.HasFeature(feature))
        {
            features.push_back(feature);
        }
    }
    return features;
}

[[nodiscard]] wgpu::Surface createSurface(const wgpu::Instance& instance, const platform::Window& window)
{
    const platform::NativeWindow native = window.native();
    wgpu::SurfaceDescriptor descriptor;
#if defined(__EMSCRIPTEN__)
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas;
    canvas.selector = "#canvas";
    descriptor.nextInChain = &canvas;
    static_cast<void>(native);
    return instance.CreateSurface(&descriptor);
#elif defined(_WIN32)
    if (native.kind != platform::NativeWindow::Kind::Win32)
    {
        return nullptr;
    }
    wgpu::SurfaceSourceWindowsHWND hwnd;
    hwnd.hinstance = native.display;
    hwnd.hwnd = native.window;
    descriptor.nextInChain = &hwnd;
    return instance.CreateSurface(&descriptor);
#elif defined(__linux__) && !defined(__ANDROID__)
    if (native.kind == platform::NativeWindow::Kind::Wayland)
    {
        wgpu::SurfaceSourceWaylandSurface wayland;
        wayland.display = native.display;
        wayland.surface = native.window;
        descriptor.nextInChain = &wayland;
        return instance.CreateSurface(&descriptor);
    }
    if (native.kind == platform::NativeWindow::Kind::X11)
    {
        wgpu::SurfaceSourceXlibWindow xlib;
        xlib.display = native.display;
        xlib.window = native.x11Window;
        descriptor.nextInChain = &xlib;
        return instance.CreateSurface(&descriptor);
    }
    return nullptr;
#else
    static_cast<void>(instance);
    static_cast<void>(native);
    return nullptr;
#endif
}

// The format WebGPU stores a texture in, as it arrives, and the size of its blocks: 1 for texels of their own.
struct FormatLayout
{
    wgpu::TextureFormat format = wgpu::TextureFormat::Undefined;
    std::uint32_t block = 1;
    std::uint32_t bytesPerBlock = 4;
};

[[nodiscard]] FormatLayout layoutOf(asset::TextureFormat format) noexcept
{
    switch (format)
    {
    case asset::TextureFormat::Rgba8Unorm:
        return {wgpu::TextureFormat::RGBA8Unorm, 1, 4};
    case asset::TextureFormat::Rgba8Srgb:
        return {wgpu::TextureFormat::RGBA8UnormSrgb, 1, 4};
    case asset::TextureFormat::Bc5Unorm:
        return {wgpu::TextureFormat::BC5RGUnorm, 4, 16};
    case asset::TextureFormat::Bc7Unorm:
        return {wgpu::TextureFormat::BC7RGBAUnorm, 4, 16};
    case asset::TextureFormat::Bc7Srgb:
        return {wgpu::TextureFormat::BC7RGBAUnormSrgb, 4, 16};
    case asset::TextureFormat::Rgba16Float:
        return {wgpu::TextureFormat::RGBA16Float, 1, 8};
    case asset::TextureFormat::R8Unorm:
        return {wgpu::TextureFormat::R8Unorm, 1, 1};
    case asset::TextureFormat::R32Float:
        return {wgpu::TextureFormat::R32Float, 1, 4};
    case asset::TextureFormat::Astc4x4Unorm:
        return {wgpu::TextureFormat::ASTC4x4Unorm, 4, 16};
    case asset::TextureFormat::Astc4x4Srgb:
        return {wgpu::TextureFormat::ASTC4x4UnormSrgb, 4, 16};
    }
    return {};
}

[[nodiscard]] wgpu::TextureFormat srgbOf(wgpu::TextureFormat format) noexcept
{
    switch (format)
    {
    case wgpu::TextureFormat::BGRA8Unorm:
        return wgpu::TextureFormat::BGRA8UnormSrgb;
    case wgpu::TextureFormat::RGBA8Unorm:
        return wgpu::TextureFormat::RGBA8UnormSrgb;
    default:
        return format;
    }
}

[[nodiscard]] wgpu::PresentMode presentModeOf(PresentMode mode) noexcept
{
    switch (mode)
    {
    case PresentMode::Fifo:
        return wgpu::PresentMode::Fifo;
    case PresentMode::Mailbox:
        return wgpu::PresentMode::Mailbox;
    case PresentMode::FifoRelaxed:
        return wgpu::PresentMode::FifoRelaxed;
    case PresentMode::Immediate:
        return wgpu::PresentMode::Immediate;
    }
    return wgpu::PresentMode::Fifo;
}

// The largest size of `size` that fits in `limit`, keeping its proportions.
[[nodiscard]] math::Extent2D fitted(math::Extent2D size, math::Extent2D limit) noexcept
{
    if (limit.width == 0 || limit.height == 0 || (size.width <= limit.width && size.height <= limit.height))
    {
        return size;
    }
    const double scale = std::min(static_cast<double>(limit.width) / size.width, static_cast<double>(limit.height) / size.height);
    return {std::max(1U, static_cast<std::uint32_t>(size.width * scale)), std::max(1U, static_cast<std::uint32_t>(size.height * scale))};
}

#if !defined(__EMSCRIPTEN__)
// Waits for an answer of the device: a computer's Dawn can.
void wait(const wgpu::Instance& instance, wgpu::Future future)
{
    static_cast<void>(instance.WaitAny(future, UINT64_MAX));
}
#endif

// The adapter and device a browser gave before the renderer existed, which asks for them without waiting.
struct BrowserDevice
{
    wgpu::Instance instance;
    wgpu::Adapter adapter;
    wgpu::Device device;
};
BrowserDevice& browserDevice()
{
    static BrowserDevice device;
    return device;
}

} // namespace

} // namespace devex::render::webgpu

namespace devex::render {

#if defined(__EMSCRIPTEN__)
void requestBrowserDevice(std::function<void(core::Result<void>)> ready)
{
    using webgpu::BrowserDevice;
    using webgpu::browserDevice;
    using webgpu::deviceDescriptor;
    using webgpu::wantedFeatures;
    BrowserDevice& stored = browserDevice();
    stored.instance = wgpu::CreateInstance(nullptr);
    // Shared by the two answers: the callbacks of WebGPU are not mutable.
    const auto answer = std::make_shared<std::function<void(core::Result<void>)>>(std::move(ready));
    wgpu::RequestAdapterOptions options;
    options.powerPreference = wgpu::PowerPreference::HighPerformance;
    static_cast<void>(stored.instance.RequestAdapter(
        &options, wgpu::CallbackMode::AllowSpontaneous,
        [answer](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
            if (status != wgpu::RequestAdapterStatus::Success)
            {
                (*answer)(core::makeError(core::ErrorCode::Graphics, "this browser gave no GPU: {}", std::string_view(message)));
                return;
            }
            BrowserDevice& device = browserDevice();
            device.adapter = std::move(adapter);
            static std::vector<wgpu::FeatureName> features;
            features = wantedFeatures(device.adapter);
            const wgpu::DeviceDescriptor descriptor = deviceDescriptor(features);
            static_cast<void>(device.adapter.RequestDevice(
                &descriptor, wgpu::CallbackMode::AllowSpontaneous,
                [answer](wgpu::RequestDeviceStatus deviceStatus, wgpu::Device created, wgpu::StringView deviceMessage) {
                    if (deviceStatus != wgpu::RequestDeviceStatus::Success)
                    {
                        (*answer)(core::makeError(core::ErrorCode::Graphics, "this browser refused a GPU device: {}",
                                              std::string_view(deviceMessage)));
                        return;
                    }
                    browserDevice().device = std::move(created);
                    (*answer)({});
                }));
        }));
}
#endif

} // namespace devex::render

namespace devex::render::webgpu {

WebGpuRenderer::WebGpuRenderer(platform::Window& window, const RendererConfig& config)
    : m_window(window), m_config(config), m_presentMode(config.presentMode)
{
}

WebGpuRenderer::~WebGpuRenderer()
{
    // What the GPU still does finishes before what it uses goes away.
    m_readbacks.clear();
    m_luminance.reset();
    if (m_surface)
    {
        m_surface.Unconfigure();
    }
}

core::Result<std::unique_ptr<WebGpuRenderer>> WebGpuRenderer::create(const platform::Platform& platform, platform::Window& window,
                                                                     const RendererConfig& config)
{
    static_cast<void>(platform);
    std::unique_ptr<WebGpuRenderer> renderer(new WebGpuRenderer(window, config));
    if (core::Result<void> initialized = renderer->initialize(); !initialized)
    {
        return std::unexpected(initialized.error());
    }
    return renderer;
}

core::Result<void> WebGpuRenderer::initialize()
{
#if defined(__EMSCRIPTEN__)
    BrowserDevice& browser = browserDevice();
    if (!browser.device)
    {
        return core::makeError(core::ErrorCode::InvalidState,
                               "the browser has not given its GPU yet: call render::requestBrowserDevice first");
    }
    m_instance = browser.instance;
    m_adapter = browser.adapter;
    m_device = browser.device;
    m_surface = createSurface(m_instance, m_window);
#else
    static constexpr std::array<wgpu::InstanceFeatureName, 1> instanceFeatures{wgpu::InstanceFeatureName::TimedWaitAny};
    wgpu::InstanceDescriptor instanceDescriptor;
    instanceDescriptor.requiredFeatureCount = instanceFeatures.size();
    instanceDescriptor.requiredFeatures = instanceFeatures.data();
    m_instance = wgpu::CreateInstance(&instanceDescriptor);
    if (!m_instance)
    {
        return core::makeError(core::ErrorCode::Graphics, "cannot create a WebGPU instance");
    }
    m_surface = createSurface(m_instance, m_window);
    if (!m_surface)
    {
        return core::makeError(core::ErrorCode::Platform, "WebGPU cannot draw into this window");
    }
    wgpu::RequestAdapterOptions options;
    options.powerPreference = wgpu::PowerPreference::HighPerformance;
    options.compatibleSurface = m_surface;
    std::string failure;
    wait(m_instance, m_instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly,
                                               [&](wgpu::RequestAdapterStatus status, wgpu::Adapter adapter, wgpu::StringView message) {
                                                   if (status == wgpu::RequestAdapterStatus::Success)
                                                   {
                                                       m_adapter = std::move(adapter);
                                                   }
                                                   else
                                                   {
                                                       failure = text(message);
                                                   }
                                               }));
    if (!m_adapter)
    {
        return core::makeError(core::ErrorCode::Graphics, "WebGPU found no GPU: {}", failure);
    }
    const std::vector<wgpu::FeatureName> features = wantedFeatures(m_adapter);
    const wgpu::DeviceDescriptor descriptor = deviceDescriptor(features);
    wait(m_instance, m_adapter.RequestDevice(&descriptor, wgpu::CallbackMode::WaitAnyOnly,
                                             [&](wgpu::RequestDeviceStatus status, wgpu::Device device, wgpu::StringView message) {
                                                 if (status == wgpu::RequestDeviceStatus::Success)
                                                 {
                                                     m_device = std::move(device);
                                                 }
                                                 else
                                                 {
                                                     failure = text(message);
                                                 }
                                             }));
    if (!m_device)
    {
        return core::makeError(core::ErrorCode::Graphics, "the GPU refused a WebGPU device: {}", failure);
    }
#endif
    if (!m_surface)
    {
        return core::makeError(core::ErrorCode::Platform, "WebGPU cannot draw into this window");
    }
    m_queue = m_device.GetQueue();
    m_blockCompression = m_device.HasFeature(wgpu::FeatureName::TextureCompressionBC);

    wgpu::AdapterInfo info;
    static_cast<void>(m_adapter.GetInfo(&info));
    m_gpu.name = text(info.device).empty() ? text(info.description) : text(info.device);
    m_gpu.type = info.adapterType == wgpu::AdapterType::DiscreteGPU     ? GpuType::Discrete
                 : info.adapterType == wgpu::AdapterType::IntegratedGPU ? GpuType::Integrated
                 : info.adapterType == wgpu::AdapterType::CPU           ? GpuType::Cpu
                                                                        : GpuType::Other;
    m_gpu.apiVersion = "WebGPU";
    m_gpu.driverName = text(info.vendor);
    m_gpu.driverVersion = text(info.architecture);

    wgpu::SurfaceCapabilities capabilities;
    static_cast<void>(m_surface.GetCapabilities(m_adapter, &capabilities));
    const std::span<const wgpu::TextureFormat> formats(capabilities.formats, capabilities.formatCount);
    for (const wgpu::TextureFormat wanted : {wgpu::TextureFormat::BGRA8UnormSrgb, wgpu::TextureFormat::RGBA8UnormSrgb,
                                             wgpu::TextureFormat::BGRA8Unorm, wgpu::TextureFormat::RGBA8Unorm})
    {
        if (std::ranges::find(formats, wanted) != formats.end())
        {
            m_surfaceFormat = wanted;
            break;
        }
    }
    if (m_surfaceFormat == wgpu::TextureFormat::Undefined)
    {
        return core::makeError(core::ErrorCode::Graphics, "the window offers WebGPU no format of 8 bits per channel");
    }
    m_surfaceViewFormat = srgbOf(m_surfaceFormat);
    m_presentModes.assign(capabilities.presentModes, capabilities.presentModes + capabilities.presentModeCount);

    if (core::Result<void> pipelines = createPipelines(); !pipelines)
    {
        return pipelines;
    }
    if (core::Result<void> scene = createScenePipelines(); !scene)
    {
        return scene;
    }
    return {};
}

wgpu::ShaderModule WebGpuRenderer::shaderModule(std::string_view name) const
{
    const std::string_view source = shaderSource(name);
    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code = wgpu::StringView(source.data(), source.size());
    wgpu::ShaderModuleDescriptor descriptor;
    descriptor.nextInChain = &wgsl;
    const std::string label(name);
    descriptor.label = wgpu::StringView(label.data(), label.size());
    return m_device.CreateShaderModule(&descriptor);
}

core::Result<void> WebGpuRenderer::createPipelines()
{
    wgpu::SamplerDescriptor linear;
    linear.addressModeU = linear.addressModeV = linear.addressModeW = wgpu::AddressMode::Repeat;
    linear.magFilter = linear.minFilter = wgpu::FilterMode::Linear;
    linear.mipmapFilter = wgpu::MipmapFilterMode::Linear;
    linear.maxAnisotropy = 16;
    m_linearSampler = m_device.CreateSampler(&linear);
    wgpu::SamplerDescriptor nearest = linear;
    nearest.magFilter = nearest.minFilter = wgpu::FilterMode::Nearest;
    nearest.mipmapFilter = wgpu::MipmapFilterMode::Nearest;
    nearest.maxAnisotropy = 1;
    m_nearestSampler = m_device.CreateSampler(&nearest);
    wgpu::SamplerDescriptor clamp = linear;
    clamp.addressModeU = clamp.addressModeV = clamp.addressModeW = wgpu::AddressMode::ClampToEdge;
    clamp.maxAnisotropy = 1;
    m_clampSampler = m_device.CreateSampler(&clamp);

    const std::array textureEntries{textureEntry(0, wgpu::ShaderStage::Fragment), samplerEntry(1, wgpu::ShaderStage::Fragment)};
    m_textureLayout = bindGroupLayout(m_device, "Texture", textureEntries);
    const wgpu::ShaderStage both = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment;
    wgpu::BindGroupLayoutEntry sprites;
    sprites.binding = 1;
    sprites.visibility = both;
    sprites.buffer.type = wgpu::BufferBindingType::ReadOnlyStorage;
    const std::array spriteEntries{uniformEntry(0, both, false, sizeof(FrameData)), sprites,
                                   uniformEntry(2, both, true, sizeof(SpriteDrawData))};
    m_spriteLayout = bindGroupLayout(m_device, "Sprites", spriteEntries);
    const std::array uiEntries{uniformEntry(0, both, true, sizeof(UiDrawData))};
    m_uiLayout = bindGroupLayout(m_device, "Interface", uiEntries);
    const std::array tonemapEntries{uniformEntry(0, wgpu::ShaderStage::Fragment, true, sizeof(TonemapData)),
                                    textureEntry(1, wgpu::ShaderStage::Fragment), samplerEntry(2, wgpu::ShaderStage::Fragment)};
    m_tonemapLayout = bindGroupLayout(m_device, "Tonemap", tonemapEntries);
    wgpu::BindGroupLayoutEntry output;
    output.binding = 3;
    output.visibility = wgpu::ShaderStage::Compute;
    output.buffer.type = wgpu::BufferBindingType::Storage;
    const std::array luminanceEntries{uniformEntry(0, wgpu::ShaderStage::Compute, false, sizeof(GridData)),
                                      textureEntry(1, wgpu::ShaderStage::Compute), samplerEntry(2, wgpu::ShaderStage::Compute),
                                      output};
    m_luminanceLayout = bindGroupLayout(m_device, "Luminance", luminanceEntries);

    // Premultiplied colours, which additive sprites leave with an alpha of zero.
    wgpu::BlendState premultiplied;
    premultiplied.color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One,
                           .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha};
    premultiplied.alpha = premultiplied.color;
    const wgpu::ShaderModule spriteModule = shaderModule("webgpu_sprite");
    wgpu::ColorTargetState spriteTarget;
    spriteTarget.format = sceneFormat;
    spriteTarget.blend = &premultiplied;
    wgpu::FragmentState spriteFragment;
    spriteFragment.module = spriteModule;
    spriteFragment.entryPoint = "spriteFragment";
    spriteFragment.targetCount = 1;
    spriteFragment.targets = &spriteTarget;
    const std::array spriteGroups{m_spriteLayout, m_textureLayout};
    // Sprites lie among the blended surfaces: behind what the meshes drew nearer, without hiding the others.
    wgpu::DepthStencilState spriteDepth;
    spriteDepth.format = wgpu::TextureFormat::Depth32Float;
    spriteDepth.depthWriteEnabled = false;
    spriteDepth.depthCompare = wgpu::CompareFunction::GreaterEqual;
    wgpu::RenderPipelineDescriptor sprite;
    sprite.label = "Sprites";
    sprite.layout = pipelineLayout(m_device, spriteGroups);
    sprite.vertex.module = spriteModule;
    sprite.vertex.entryPoint = "spriteVertex";
    sprite.depthStencil = &spriteDepth;
    sprite.fragment = &spriteFragment;
    m_spritePipeline = m_device.CreateRenderPipeline(&sprite);

    m_uiModule = shaderModule("webgpu_ui");
    const std::array uiGroups{m_uiLayout, m_textureLayout};
    m_uiPipelineLayout = pipelineLayout(m_device, uiGroups);
    m_tonemapModule = shaderModule("webgpu_tonemap");
    const std::array tonemapGroups{m_tonemapLayout};
    m_tonemapPipelineLayout = pipelineLayout(m_device, tonemapGroups);

    wgpu::ComputePipelineDescriptor luminance;
    luminance.label = "Luminance";
    const std::array luminanceGroups{m_luminanceLayout};
    luminance.layout = pipelineLayout(m_device, luminanceGroups);
    luminance.compute.module = shaderModule("webgpu_luminance");
    luminance.compute.entryPoint = "luminanceMain";
    m_luminancePipeline = m_device.CreateComputePipeline(&luminance);

    // A white pixel for what reads no texture.
    wgpu::TextureDescriptor white;
    white.size = {1, 1, 1};
    white.format = wgpu::TextureFormat::RGBA8Unorm;
    white.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    m_white = m_device.CreateTexture(&white);
    const std::array<std::uint8_t, 4> pixel{255, 255, 255, 255};
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = m_white;
    wgpu::TexelCopyBufferLayout layout;
    layout.bytesPerRow = 4;
    const wgpu::Extent3D one{1, 1, 1};
    m_queue.WriteTexture(&destination, pixel.data(), pixel.size(), &layout, &one);
    m_whiteGroup = textureGroup(m_white, false);

    wgpu::BufferDescriptor frame;
    frame.label = "Frame";
    frame.size = sizeof(FrameData);
    frame.usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;
    m_frameBuffer = m_device.CreateBuffer(&frame);
    wgpu::BufferDescriptor grid = frame;
    grid.label = "Luminance grid";
    grid.size = sizeof(GridData);
    m_gridBuffer = m_device.CreateBuffer(&grid);
    const GridData gridData;
    m_queue.WriteBuffer(m_gridBuffer, 0, &gridData, sizeof(gridData));
    wgpu::BufferDescriptor samples;
    samples.label = "Luminance";
    samples.size = std::uint64_t{luminanceGridWidth} * luminanceGridHeight * sizeof(float);
    samples.usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopySrc;
    m_luminanceBuffer = m_device.CreateBuffer(&samples);
    return {};
}

std::uint32_t WebGpuRenderer::uiPipelineIndex(wgpu::TextureFormat format)
{
    for (std::uint32_t index = 0; index < m_uiPipelines.size(); ++index)
    {
        if (m_uiPipelines[index].first == format)
        {
            return index;
        }
    }
    // The interface blends straight alpha over the image.
    wgpu::BlendState straight;
    straight.color = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::SrcAlpha,
                      .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha};
    straight.alpha = {.operation = wgpu::BlendOperation::Add, .srcFactor = wgpu::BlendFactor::One,
                      .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha};
    wgpu::ColorTargetState target;
    target.format = format;
    target.blend = &straight;
    wgpu::FragmentState fragment;
    fragment.module = m_uiModule;
    fragment.entryPoint = "uiFragment";
    fragment.targetCount = 1;
    fragment.targets = &target;
    const std::array attributes{
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x2, .offset = 0, .shaderLocation = 0},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x2, .offset = 8, .shaderLocation = 1},
        wgpu::VertexAttribute{.format = wgpu::VertexFormat::Float32x4, .offset = 16, .shaderLocation = 2},
    };
    wgpu::VertexBufferLayout vertices;
    vertices.arrayStride = sizeof(GpuUiVertex);
    vertices.attributeCount = attributes.size();
    vertices.attributes = attributes.data();
    wgpu::RenderPipelineDescriptor ui;
    ui.label = "Interface";
    ui.layout = m_uiPipelineLayout;
    ui.vertex.module = m_uiModule;
    ui.vertex.entryPoint = "uiVertex";
    ui.vertex.bufferCount = 1;
    ui.vertex.buffers = &vertices;
    ui.fragment = &fragment;
    m_uiPipelines.emplace_back(format, m_device.CreateRenderPipeline(&ui));

    wgpu::ColorTargetState tonemapTarget;
    tonemapTarget.format = format;
    wgpu::FragmentState tonemapFragment;
    tonemapFragment.module = m_tonemapModule;
    tonemapFragment.entryPoint = "tonemapFragment";
    tonemapFragment.targetCount = 1;
    tonemapFragment.targets = &tonemapTarget;
    wgpu::RenderPipelineDescriptor tonemap;
    tonemap.label = "Tonemap";
    tonemap.layout = m_tonemapPipelineLayout;
    tonemap.vertex.module = m_tonemapModule;
    tonemap.vertex.entryPoint = "tonemapVertex";
    tonemap.fragment = &tonemapFragment;
    m_tonemapPipelines.emplace_back(format, m_device.CreateRenderPipeline(&tonemap));
    return static_cast<std::uint32_t>(m_uiPipelines.size() - 1);
}

wgpu::BindGroup WebGpuRenderer::textureGroup(const wgpu::Texture& texture, bool nearest) const
{
    const std::array entries{viewBinding(0, texture.CreateView()), samplerBinding(1, nearest ? m_nearestSampler : m_linearSampler)};
    return bindGroup(m_device, m_textureLayout, entries);
}

const WebGpuRenderer::GpuTexture* WebGpuRenderer::textureAtSlot(std::uint32_t slot) const noexcept
{
    // A slot is the index of the texture among the renderer's: the sprites name their texture by it.
    return slot < m_slotHandles.size() ? m_textures.find(m_slotHandles[slot]) : nullptr;
}

void WebGpuRenderer::growBuffer(wgpu::Buffer& buffer, std::uint64_t& capacity, std::uint64_t size, wgpu::BufferUsage usage,
                                        const char* label) const
{
    if (!buffer || capacity < size)
    {
        capacity = std::max<std::uint64_t>(std::bit_ceil(std::max<std::uint64_t>(size, 256)), capacity);
        wgpu::BufferDescriptor descriptor;
        descriptor.label = label;
        descriptor.size = capacity;
        descriptor.usage = usage | wgpu::BufferUsage::CopyDst;
        buffer = m_device.CreateBuffer(&descriptor);
    }
}

core::Result<void> WebGpuRenderer::configureSurface(math::Extent2D size)
{
    wgpu::SurfaceConfiguration configuration;
    configuration.device = m_device;
    configuration.format = m_surfaceFormat;
    configuration.usage = wgpu::TextureUsage::RenderAttachment;
    configuration.width = size.width;
    configuration.height = size.height;
    if (m_surfaceViewFormat != m_surfaceFormat)
    {
        configuration.viewFormatCount = 1;
        configuration.viewFormats = &m_surfaceViewFormat;
    }
    const wgpu::PresentMode wanted = presentModeOf(m_presentMode);
    const bool offered = std::ranges::find(m_presentModes, wanted) != m_presentModes.end();
    configuration.presentMode = offered ? wanted : wgpu::PresentMode::Fifo;
    if (!offered)
    {
        m_presentMode = PresentMode::Fifo;
    }
    m_surface.Configure(&configuration);
    m_surfaceSize = size;
    m_surfaceOutdated = false;
    return {};
}

void WebGpuRenderer::ensureSceneTarget(math::Extent2D size)
{
    if (m_sceneTarget && m_sceneSize == size)
    {
        return;
    }
    wgpu::TextureDescriptor descriptor;
    descriptor.label = "Scene";
    descriptor.size = {size.width, size.height, 1};
    descriptor.format = sceneFormat;
    descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::TextureBinding;
    m_sceneTarget = m_device.CreateTexture(&descriptor);
    m_sceneView = m_sceneTarget.CreateView();
    wgpu::TextureDescriptor depth;
    depth.label = "Depth";
    depth.size = {size.width, size.height, 1};
    depth.format = wgpu::TextureFormat::Depth32Float;
    depth.usage = wgpu::TextureUsage::RenderAttachment;
    m_depthTarget = m_device.CreateTexture(&depth);
    m_depthView = m_depthTarget.CreateView();
    m_sceneSize = size;
}

const GpuInfo& WebGpuRenderer::gpu() const noexcept
{
    return m_gpu;
}

PresentMode WebGpuRenderer::presentMode() const noexcept
{
    return m_presentMode;
}

void WebGpuRenderer::setPresentMode(PresentMode mode)
{
    if (mode != m_presentMode)
    {
        m_presentMode = mode;
        m_surfaceOutdated = true;
    }
}

core::Result<std::uint32_t> WebGpuRenderer::addWindowTarget(platform::Window& window)
{
    static_cast<void>(window);
    return core::makeError(core::ErrorCode::Unsupported, "WebGPU draws into one window");
}

void WebGpuRenderer::removeWindowTarget(std::uint32_t target)
{
    static_cast<void>(target);
}

core::Result<std::string> WebGpuRenderer::describeGraphics() const
{
    return std::format("WebGPU on {} ({}, {})", m_gpu.name, m_gpu.driverName, m_gpu.driverVersion);
}

core::Result<MeshHandle> WebGpuRenderer::createMesh(const asset::MeshData& mesh)
{
    if (mesh.vertices.empty() || mesh.indices.empty())
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "a mesh needs vertices and indices");
    }
    const auto upload = [this](const void* data, std::size_t size, wgpu::BufferUsage usage, const char* label) {
        // Writes go by four bytes.
        const std::size_t padded = (size + 3) / 4 * 4;
        wgpu::BufferDescriptor descriptor;
        descriptor.label = label;
        descriptor.size = padded;
        descriptor.usage = usage | wgpu::BufferUsage::CopyDst;
        const wgpu::Buffer buffer = m_device.CreateBuffer(&descriptor);
        std::vector<std::byte> bytes(padded);
        std::memcpy(bytes.data(), data, size);
        m_queue.WriteBuffer(buffer, 0, bytes.data(), bytes.size());
        return buffer;
    };
    GpuMesh gpu;
    gpu.vertices = upload(mesh.vertices.data(), mesh.vertices.size() * sizeof(asset::Vertex), wgpu::BufferUsage::Vertex, "Vertices");
    gpu.indices = upload(mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t), wgpu::BufferUsage::Index, "Indices");
    if (!mesh.skin.empty())
    {
        gpu.skin = upload(mesh.skin.data(), mesh.skin.size() * sizeof(asset::VertexSkin), wgpu::BufferUsage::Vertex, "Skin");
    }
    gpu.submeshes = mesh.submeshes;
    if (gpu.submeshes.empty())
    {
        gpu.submeshes.push_back({.firstIndex = 0, .indexCount = static_cast<std::uint32_t>(mesh.indices.size())});
    }
    gpu.bounds = mesh.bounds;
    return m_meshes.insert(std::move(gpu));
}

bool WebGpuRenderer::isReady(MeshHandle mesh) const noexcept
{
    return m_meshes.contains(mesh);
}

bool WebGpuRenderer::isReady(TextureHandle texture) const noexcept
{
    return m_textures.contains(texture);
}

void WebGpuRenderer::destroyMesh(MeshHandle mesh)
{
    static_cast<void>(m_meshes.remove(mesh));
}

std::uint32_t WebGpuRenderer::submeshCount(MeshHandle mesh) const noexcept
{
    const GpuMesh* const record = m_meshes.find(mesh);
    return record != nullptr ? static_cast<std::uint32_t>(record->submeshes.size()) : 0;
}

core::Result<TextureHandle> WebGpuRenderer::createTexture(const asset::TextureData& texture)
{
    if (core::Result<void> valid = asset::validate(texture); !valid)
    {
        return std::unexpected(valid.error());
    }
    const asset::TextureMip& base = texture.mips.front();
    FormatLayout layout = layoutOf(texture.format);
    const bool blocks = layout.block > 1;
    const bool astc = texture.format == asset::TextureFormat::Astc4x4Unorm || texture.format == asset::TextureFormat::Astc4x4Srgb;
    const bool readable = astc ? static_cast<bool>(m_device.HasFeature(wgpu::FeatureName::TextureCompressionASTC)) : m_blockCompression;
    // WebGPU takes blocks only for sizes of whole blocks; a device without them, or another size, gets the pixels.
    const bool decode = blocks && (!readable || base.width % 4 != 0 || base.height % 4 != 0);
    if (decode && astc)
    {
        return core::makeError(core::ErrorCode::Unsupported, "this GPU cannot read ASTC textures");
    }
    if (decode)
    {
        layout = {texture.format == asset::TextureFormat::Bc7Srgb ? wgpu::TextureFormat::RGBA8UnormSrgb : wgpu::TextureFormat::RGBA8Unorm,
                  1, 4};
    }

    wgpu::TextureDescriptor descriptor;
    descriptor.size = {base.width, base.height, 1};
    descriptor.format = layout.format;
    descriptor.mipLevelCount = static_cast<std::uint32_t>(texture.mips.size());
    descriptor.usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst;
    GpuTexture gpu;
    gpu.texture = m_device.CreateTexture(&descriptor);
    gpu.format = texture.format;
    gpu.size = {base.width, base.height};
    gpu.decoded = decode;
    gpu.bindable = texture.format != asset::TextureFormat::R32Float;
    gpu.nearest = texture.filter == asset::TextureFilter::Nearest;

    for (std::uint32_t level = 0; level < texture.mips.size(); ++level)
    {
        const asset::TextureMip& mip = texture.mips[level];
        std::vector<std::byte> decoded;
        std::span<const std::byte> bytes = mip.bytes;
        if (decode)
        {
            decoded = decodeBlocks(texture.format, mip.bytes, mip.width, mip.height);
            bytes = decoded;
        }
        // A level of blocks covers whole blocks, past its edges.
        const std::uint32_t wide = (mip.width + layout.block - 1) / layout.block;
        const std::uint32_t high = (mip.height + layout.block - 1) / layout.block;
        wgpu::TexelCopyTextureInfo destination;
        destination.texture = gpu.texture;
        destination.mipLevel = level;
        wgpu::TexelCopyBufferLayout rows;
        rows.bytesPerRow = wide * layout.bytesPerBlock;
        rows.rowsPerImage = high;
        const wgpu::Extent3D extent{wide * layout.block, high * layout.block, 1};
        m_queue.WriteTexture(&destination, bytes.data(), bytes.size(), &rows, &extent);
    }
    if (gpu.bindable)
    {
        gpu.group = textureGroup(gpu.texture, texture.filter == asset::TextureFilter::Nearest);
    }
    const TextureHandle handle = m_textures.insert(std::move(gpu));
    if (m_slotHandles.size() <= handle.index)
    {
        m_slotHandles.resize(handle.index + 1);
    }
    m_slotHandles[handle.index] = handle;
    return handle;
}

void WebGpuRenderer::destroyTexture(TextureHandle texture)
{
    if (m_textures.remove(texture))
    {
        // The materials that read it sample white from now on.
        ++m_textureEpoch;
    }
}

core::Result<void> WebGpuRenderer::updateTexture(TextureHandle texture, std::uint32_t top, std::uint32_t height,
                                                 std::span<const std::byte> rows)
{
    const GpuTexture* const gpu = m_textures.find(texture);
    if (gpu == nullptr)
    {
        return core::makeError(core::ErrorCode::NotFound, "no such texture");
    }
    const FormatLayout layout = layoutOf(gpu->format);
    if (layout.block > 1 || gpu->decoded)
    {
        return core::makeError(core::ErrorCode::Unsupported, "rows of a texture of blocks cannot be replaced");
    }
    if (top + height > gpu->size.height || rows.size() < std::size_t{gpu->size.width} * height * layout.bytesPerBlock)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "the rows go past the texture");
    }
    wgpu::TexelCopyTextureInfo destination;
    destination.texture = gpu->texture;
    destination.origin = {0, top, 0};
    wgpu::TexelCopyBufferLayout copied;
    copied.bytesPerRow = gpu->size.width * layout.bytesPerBlock;
    copied.rowsPerImage = height;
    const wgpu::Extent3D extent{gpu->size.width, height, 1};
    m_queue.WriteTexture(&destination, rows.data(), rows.size(), &copied, &extent);
    return {};
}

core::Result<ReflectionProbeHandle> WebGpuRenderer::createReflectionProbe(const asset::ReflectionProbeData& probe)
{
    static_cast<void>(probe);
    return m_probes.insert({});
}

void WebGpuRenderer::destroyReflectionProbe(ReflectionProbeHandle probe)
{
    static_cast<void>(m_probes.remove(probe));
}

core::Result<IrradianceVolumeHandle> WebGpuRenderer::createIrradianceVolume(const asset::IrradianceVolumeData& volume)
{
    static_cast<void>(volume);
    return m_volumes.insert({});
}

void WebGpuRenderer::destroyIrradianceVolume(IrradianceVolumeHandle volume)
{
    static_cast<void>(m_volumes.remove(volume));
}

MaterialHandle WebGpuRenderer::createMaterial(const MaterialDesc& material)
{
    return m_materials.insert({.description = material});
}

void WebGpuRenderer::updateMaterial(MaterialHandle handle, const MaterialDesc& material)
{
    if (GpuMaterial* const stored = m_materials.find(handle))
    {
        stored->description = material;
        // Its group is made again when it is drawn.
        stored->textureEpoch = 0;
    }
}

void WebGpuRenderer::destroyMaterial(MaterialHandle material)
{
    static_cast<void>(m_materials.remove(material));
}

core::Result<ShaderHandle> WebGpuRenderer::createShader(const asset::ShaderData& shader)
{
    // The shaders of projects draw from web step 3: until then their materials draw as the standard ones.
    static_cast<void>(shader);
    return m_shaders.insert({});
}

core::Result<void> WebGpuRenderer::updateShader(ShaderHandle handle, const asset::ShaderData& shader)
{
    static_cast<void>(shader);
    if (!m_shaders.contains(handle))
    {
        return core::makeError(core::ErrorCode::NotFound, "no such shader");
    }
    return {};
}

void WebGpuRenderer::destroyShader(ShaderHandle shader)
{
    static_cast<void>(m_shaders.remove(shader));
}

RenderWorld& WebGpuRenderer::beginFrame() noexcept
{
    m_world.reset();
    return m_world;
}

void WebGpuRenderer::resetSceneHistory() noexcept
{
    m_snapExposure = true;
}

void WebGpuRenderer::updateExposure()
{
    const RenderCamera& camera = m_world.camera;
    const auto now = std::chrono::steady_clock::now();
    const float deltaSeconds =
        m_exposureInitialized ? std::clamp(std::chrono::duration<float>(now - m_lastExposureUpdate).count(), 0.0f, 0.25f) : 0.0f;
    m_lastExposureUpdate = now;
    if (!camera.autoExposure || !m_exposureInitialized)
    {
        m_ev100 = camera.ev100 - camera.exposureCompensation;
        m_targetEv100 = m_ev100;
        m_exposureInitialized = true;
        if (!camera.autoExposure)
        {
            m_snapExposure = false;
            return;
        }
    }
    // The grid a frame before measured, once the GPU gave it back.
    if (m_luminance && *m_luminance->state != 0)
    {
        if (*m_luminance->state > 0 && m_measuredExposure > 0.0f)
        {
            const auto* const samples = static_cast<const float*>(m_luminance->buffer.GetConstMappedRange());
            m_luminanceSamples.assign(samples, samples + std::size_t{luminanceGridWidth} * luminanceGridHeight);
            for (float& sample : m_luminanceSamples)
            {
                sample /= m_measuredExposure;
            }
            if (const float average = averageLuminance(m_luminanceSamples); average > 0.0f)
            {
                m_targetEv100 =
                    std::clamp(ev100FromAverageLuminance(average) - camera.exposureCompensation, camera.minEv100, camera.maxEv100);
                if (m_snapExposure)
                {
                    m_ev100 = m_targetEv100;
                    m_snapExposure = false;
                }
            }
            m_luminance->buffer.Unmap();
        }
        m_luminance.reset();
    }
    m_ev100 = adaptExposure(m_ev100, m_targetEv100, camera.adaptationSpeed, deltaSeconds);
}

void WebGpuRenderer::drawImage(const wgpu::CommandEncoder& encoder, const wgpu::TextureView& target, wgpu::TextureFormat format,
                               math::Extent2D size, bool interface)
{
    const std::uint32_t pipelines = uiPipelineIndex(format);
    // The settings of this pass, at an offset of their own.
    const math::Vec3 balance = whiteBalanceFactors(m_world.camera.temperature, m_world.camera.tint);
    TonemapData settings{
        .tonemapper = static_cast<std::uint32_t>(m_world.camera.tonemapper),
        .vignette = std::clamp(m_world.camera.vignette, 0.0f, 1.0f),
        .grain = std::max(m_world.camera.grain, 0.0f),
        .time = m_time,
        .targetWidth = static_cast<float>(size.width),
        .targetHeight = static_cast<float>(size.height),
        .encode = 0,
        .saturation = std::max(m_world.camera.saturation, 0.0f),
        .contrast = std::max(m_world.camera.contrast, 0.01f),
        .filter = math::Vec4(math::max(m_world.camera.colorFilter, math::Vec3{0.0f}), 1.0f),
        .balance = math::Vec4(balance, 1.0f),
    };
    const std::uint64_t offset = std::uint64_t{m_tonemapPass++} * uniformStride;
    m_queue.WriteBuffer(m_tonemapBuffer, offset, &settings, sizeof(settings));

    wgpu::RenderPassColorAttachment color;
    color.view = target;
    color.loadOp = wgpu::LoadOp::Clear;
    color.storeOp = wgpu::StoreOp::Store;
    color.clearValue = {0.0, 0.0, 0.0, 1.0};
    wgpu::RenderPassDescriptor descriptor;
    descriptor.label = interface ? "Image and interface" : "Image";
    descriptor.colorAttachmentCount = 1;
    descriptor.colorAttachments = &color;
    const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
    pass.SetPipeline(m_tonemapPipelines[pipelines].second);
    const auto tonemapOffset = static_cast<std::uint32_t>(offset);
    pass.SetBindGroup(0, m_tonemapGroup, 1, &tonemapOffset);
    pass.Draw(3);
    ++m_drawCalls;

    if (interface && !m_uiRuns.empty())
    {
        pass.SetPipeline(m_uiPipelines[pipelines].second);
        pass.SetVertexBuffer(0, m_uiVertexBuffer);
        pass.SetIndexBuffer(m_uiIndexBuffer, wgpu::IndexFormat::Uint32);
        for (std::uint32_t run = 0; run < m_uiRuns.size(); ++run)
        {
            const UiRun& drawn = m_uiRuns[run];
            const std::uint32_t right = std::min(drawn.scissor[0] + drawn.scissor[2], size.width);
            const std::uint32_t bottom = std::min(drawn.scissor[1] + drawn.scissor[3], size.height);
            if (drawn.scissor[0] >= right || drawn.scissor[1] >= bottom)
            {
                continue;
            }
            pass.SetScissorRect(drawn.scissor[0], drawn.scissor[1], right - drawn.scissor[0], bottom - drawn.scissor[1]);
            const auto drawOffset = static_cast<std::uint32_t>(std::uint64_t{run} * uniformStride);
            pass.SetBindGroup(0, m_uiGroup, 1, &drawOffset);
            pass.SetBindGroup(1, drawn.texture);
            pass.DrawIndexed(drawn.indexCount, 1, drawn.firstIndex);
            ++m_drawCalls;
        }
    }
    pass.End();
}

void WebGpuRenderer::queueReadback(const wgpu::CommandEncoder& encoder, const wgpu::Texture& texture, math::Extent2D size,
                                   std::uint64_t request)
{
    Readback readback;
    readback.request = request;
    readback.size = size;
    // Rows of a copy to a buffer start on 256 bytes.
    readback.bytesPerRow = (size.width * 4 + 255) / 256 * 256;
    wgpu::BufferDescriptor descriptor;
    descriptor.label = "Capture";
    descriptor.size = std::uint64_t{readback.bytesPerRow} * size.height;
    descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
    readback.buffer = m_device.CreateBuffer(&descriptor);
    wgpu::TexelCopyTextureInfo source;
    source.texture = texture;
    wgpu::TexelCopyBufferInfo destination;
    destination.buffer = readback.buffer;
    destination.layout.bytesPerRow = readback.bytesPerRow;
    destination.layout.rowsPerImage = size.height;
    const wgpu::Extent3D extent{size.width, size.height, 1};
    encoder.CopyTextureToBuffer(&source, &destination, &extent);
    m_readbacks.push_back(std::move(readback));
}

void WebGpuRenderer::collectReadbacks()
{
#if !defined(__EMSCRIPTEN__)
    m_instance.ProcessEvents();
#endif
    std::erase_if(m_readbacks, [this](Readback& readback) {
        if (*readback.state == 0)
        {
            return false;
        }
        if (*readback.state > 0)
        {
            CapturedImage image{.request = readback.request, .width = readback.size.width, .height = readback.size.height};
            image.rgba.resize(std::size_t{readback.size.width} * readback.size.height * 4);
            const auto* const bytes = static_cast<const std::uint8_t*>(readback.buffer.GetConstMappedRange());
            for (std::uint32_t row = 0; row < readback.size.height; ++row)
            {
                std::memcpy(image.rgba.data() + std::size_t{row} * readback.size.width * 4,
                            bytes + std::size_t{row} * readback.bytesPerRow, std::size_t{readback.size.width} * 4);
            }
            // Opaque, as the display shows it.
            for (std::size_t pixel = 3; pixel < image.rgba.size(); pixel += 4)
            {
                image.rgba[pixel] = 255;
            }
            readback.buffer.Unmap();
            m_captured.push_back(std::move(image));
        }
        return true;
    });
}

core::Result<void> WebGpuRenderer::endFrame()
{
    collectReadbacks();
    const math::Extent2D size = m_window.pixelSize();
    if (size.width == 0 || size.height == 0)
    {
        return {};
    }
    if (m_surfaceOutdated || size != m_surfaceSize)
    {
        if (core::Result<void> configured = configureSurface(size); !configured)
        {
            return configured;
        }
    }
    wgpu::SurfaceTexture surfaceTexture;
    m_surface.GetCurrentTexture(&surfaceTexture);
    if (surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal &&
        surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        // Configured again at the next frame, as a swapchain out of date.
        m_surfaceOutdated = true;
        return {};
    }

    m_world.camera = blendPostVolumes(m_world.camera, m_world.postVolumes);
    m_drawCalls = 0;
    m_tonemapPass = 0;
    m_time = std::fmod(m_time + 1.0f, 1024.0f);
    ensureSceneTarget(size);
    updateExposure();
    m_exposure = exposureFromEv100(m_ev100);

    // WebGPU's clip space has Y up, as the engine's matrices.
    const ViewMatrices matrices = prepareView(m_world.camera, size, math::Vec2{0.0f}, math::Mat4{1.0f});
    const FrameData frameData{.viewProjection = matrices.viewProjection, .canvasModulate = math::Vec4(m_world.canvasModulate, 1.0f)};
    m_queue.WriteBuffer(m_frameBuffer, 0, &frameData, sizeof(frameData));

    // The sprites in drawing order, by runs that read one texture.
    const SpriteResources resources{
        .textureSlot = [this](TextureHandle handle) {
            const GpuTexture* const texture = m_textures.find(handle);
            return texture != nullptr && texture->bindable ? handle.index : noTexture;
        },
        .materialIndex = [](MaterialHandle) { return 0U; },
        .canvasShader = [](MaterialHandle) { return ShaderHandle{}; },
        .sortDistance =
            [this, camera = matrices.cameraPosition](math::Vec3 point) {
                if (m_world.camera.projection == Projection::Orthographic)
                {
                    return -(m_world.camera.view * math::Vec4(point, 1.0f)).z;
                }
                const math::Vec3 toCamera = point - camera;
                return math::dot(toCamera, toCamera);
            },
    };
    const Frustum frustum = frustumOf(matrices.unjitteredViewProjection);
    prepareMeshes(matrices, frustum);
    gatherSprites(m_world, frustum, resources, m_spriteFrame);
    m_spriteRuns.clear();
    for (std::uint32_t index = 0; index < m_spriteFrame.sprites.size(); ++index)
    {
        const std::uint32_t texture = m_spriteFrame.sprites[index].texture;
        if (m_spriteRuns.empty() || m_spriteRuns.back().texture != texture)
        {
            m_spriteRuns.push_back({.first = index, .count = 0, .texture = texture});
        }
        ++m_spriteRuns.back().count;
    }
    const std::uint64_t spriteBytes = std::max<std::size_t>(m_spriteFrame.sprites.size(), 1) * sizeof(GpuSprite);
    growBuffer(m_spriteBuffer, m_spriteCapacity, spriteBytes, wgpu::BufferUsage::Storage, "Sprites");
    if (!m_spriteFrame.sprites.empty())
    {
        m_queue.WriteBuffer(m_spriteBuffer, 0, m_spriteFrame.sprites.data(), m_spriteFrame.sprites.size() * sizeof(GpuSprite));
    }
    growBuffer(m_drawBuffer, m_drawCapacity, std::max<std::size_t>(m_spriteRuns.size(), 1) * uniformStride,
               wgpu::BufferUsage::Uniform, "Sprite draws");
    std::vector<std::byte> draws(m_spriteRuns.size() * uniformStride);
    for (std::size_t run = 0; run < m_spriteRuns.size(); ++run)
    {
        const SpriteDrawData data{.first = m_spriteRuns[run].first};
        std::memcpy(draws.data() + run * uniformStride, &data, sizeof(data));
    }
    if (!draws.empty())
    {
        m_queue.WriteBuffer(m_drawBuffer, 0, draws.data(), draws.size());
    }
    const std::array spriteEntries{bufferBinding(0, m_frameBuffer, sizeof(FrameData)),
                                   bufferBinding(1, m_spriteBuffer, spriteBytes),
                                   bufferBinding(2, m_drawBuffer, sizeof(SpriteDrawData))};
    const wgpu::BindGroup spriteGroup = bindGroup(m_device, m_spriteLayout, spriteEntries);

    // The interface, in batches of one texture and one shape, cut to their rectangles.
    packUi(m_world, false, m_ui);
    m_uiRuns.clear();
    std::vector<std::byte> uiDraws;
    for (const UiDraw& draw : m_world.uiDraws)
    {
        if (draw.source != UiSource::Texture || draw.indexCount == 0)
        {
            // The images of the tools are not drawn by WebGPU.
            continue;
        }
        const GpuTexture* const texture = m_textures.find(draw.texture);
        UiRun run{.firstIndex = draw.firstIndex, .indexCount = draw.indexCount,
                  .texture = texture != nullptr && texture->bindable ? texture->group : m_whiteGroup};
        const bool clipped = draw.clip.z > draw.clip.x && draw.clip.w > draw.clip.y;
        if (clipped)
        {
            const float left = std::max(draw.clip.x, 0.0f);
            const float top = std::max(draw.clip.y, 0.0f);
            run.scissor[0] = static_cast<std::uint32_t>(left);
            run.scissor[1] = static_cast<std::uint32_t>(top);
            run.scissor[2] = static_cast<std::uint32_t>(std::clamp(draw.clip.z - left, 0.0f, static_cast<float>(size.width)));
            run.scissor[3] = static_cast<std::uint32_t>(std::clamp(draw.clip.w - top, 0.0f, static_cast<float>(size.height)));
        }
        else
        {
            run.scissor[2] = size.width;
            run.scissor[3] = size.height;
        }
        const UiDrawData data{
            .rect = draw.rect,
            .inverseViewport = {2.0f / static_cast<float>(size.width), 2.0f / static_cast<float>(size.height)},
            .radius = draw.radius,
            .sharpness = draw.sharpness,
            .kind = static_cast<std::uint32_t>(draw.kind),
        };
        const std::size_t at = uiDraws.size();
        uiDraws.resize(at + uniformStride);
        std::memcpy(uiDraws.data() + at, &data, sizeof(data));
        m_uiRuns.push_back(run);
    }
    if (!m_uiRuns.empty() && !m_ui.indices.empty())
    {
        growBuffer(m_uiVertexBuffer, m_uiVertexCapacity, m_ui.vertices.size() * sizeof(GpuUiVertex), wgpu::BufferUsage::Vertex,
                   "Interface vertices");
        growBuffer(m_uiIndexBuffer, m_uiIndexCapacity, m_ui.indices.size() * sizeof(std::uint32_t), wgpu::BufferUsage::Index,
                   "Interface indices");
        growBuffer(m_uiDrawBuffer, m_uiDrawCapacity, uiDraws.size(), wgpu::BufferUsage::Uniform, "Interface draws");
        m_queue.WriteBuffer(m_uiVertexBuffer, 0, m_ui.vertices.data(), m_ui.vertices.size() * sizeof(GpuUiVertex));
        m_queue.WriteBuffer(m_uiIndexBuffer, 0, m_ui.indices.data(), m_ui.indices.size() * sizeof(std::uint32_t));
        m_queue.WriteBuffer(m_uiDrawBuffer, 0, uiDraws.data(), uiDraws.size());
        const std::array uiEntries{bufferBinding(0, m_uiDrawBuffer, sizeof(UiDrawData))};
        m_uiGroup = bindGroup(m_device, m_uiLayout, uiEntries);
    }
    else
    {
        m_uiRuns.clear();
    }

    // Every image this frame tonemaps reads its settings at an offset of its own.
    growBuffer(m_tonemapBuffer, m_tonemapCapacity, (m_captureRequests.size() + 1) * uniformStride, wgpu::BufferUsage::Uniform,
               "Tonemap");
    const std::array tonemapEntries{bufferBinding(0, m_tonemapBuffer, sizeof(TonemapData)), viewBinding(1, m_sceneView),
                                    samplerBinding(2, m_clampSampler)};
    m_tonemapGroup = bindGroup(m_device, m_tonemapLayout, tonemapEntries);

    const wgpu::CommandEncoder encoder = m_device.CreateCommandEncoder();
    {
        // The sky covers the image, then the opaque meshes, the blended ones from the farthest, and the sprites.
        wgpu::RenderPassColorAttachment color;
        color.view = m_sceneView;
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        color.clearValue = {0.0, 0.0, 0.0, 1.0};
        // Reversed depth: 0 is infinitely far.
        wgpu::RenderPassDepthStencilAttachment depth;
        depth.view = m_depthView;
        depth.depthLoadOp = wgpu::LoadOp::Clear;
        depth.depthStoreOp = wgpu::StoreOp::Discard;
        depth.depthClearValue = 0.0f;
        wgpu::RenderPassDescriptor descriptor;
        descriptor.label = "Scene";
        descriptor.colorAttachmentCount = 1;
        descriptor.colorAttachments = &color;
        descriptor.depthStencilAttachment = &depth;
        const wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&descriptor);
        drawSky(pass);
        drawMeshes(pass, false);
        drawMeshes(pass, true);
        if (!m_spriteRuns.empty())
        {
            pass.SetPipeline(m_spritePipeline);
            for (std::uint32_t run = 0; run < m_spriteRuns.size(); ++run)
            {
                const SpriteRun& drawn = m_spriteRuns[run];
                const GpuTexture* const texture = textureAtSlot(drawn.texture);
                const auto offset = static_cast<std::uint32_t>(std::uint64_t{run} * uniformStride);
                pass.SetBindGroup(0, spriteGroup, 1, &offset);
                pass.SetBindGroup(1, texture != nullptr ? texture->group : m_whiteGroup);
                pass.Draw(6, drawn.count);
                ++m_drawCalls;
            }
        }
        pass.End();
    }

    // The luminance of the scene for the exposure of the frames after, unless a measure is still on its way.
    std::optional<Readback> measured;
    if (m_world.camera.autoExposure && !m_luminance)
    {
        const std::array entries{bufferBinding(0, m_gridBuffer, sizeof(GridData)), viewBinding(1, m_sceneView),
                                 samplerBinding(2, m_clampSampler),
                                 bufferBinding(3, m_luminanceBuffer, std::uint64_t{luminanceGridWidth} * luminanceGridHeight * sizeof(float))};
        const wgpu::BindGroup group = bindGroup(m_device, m_luminanceLayout, entries);
        const wgpu::ComputePassEncoder pass = encoder.BeginComputePass();
        pass.SetPipeline(m_luminancePipeline);
        pass.SetBindGroup(0, group);
        pass.DispatchWorkgroups((luminanceGridWidth + 7) / 8, (luminanceGridHeight + 7) / 8);
        pass.End();
        measured.emplace();
        wgpu::BufferDescriptor descriptor;
        descriptor.label = "Luminance read back";
        descriptor.size = std::uint64_t{luminanceGridWidth} * luminanceGridHeight * sizeof(float);
        descriptor.usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead;
        measured->buffer = m_device.CreateBuffer(&descriptor);
        encoder.CopyBufferToBuffer(m_luminanceBuffer, 0, measured->buffer, 0, descriptor.size);
    }

    // The captures asked for: the scene at a small size, or the whole window with its interface.
    for (const CaptureRequest& request : std::exchange(m_captureRequests, {}))
    {
        const math::Extent2D captured = request.window ? size : fitted(size, request.maxSize);
        wgpu::TextureDescriptor descriptor;
        descriptor.label = "Capture";
        descriptor.size = {captured.width, captured.height, 1};
        descriptor.format = captureFormat;
        descriptor.usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc;
        const wgpu::Texture texture = m_device.CreateTexture(&descriptor);
        drawImage(encoder, texture.CreateView(), captureFormat, captured, request.window);
        queueReadback(encoder, texture, captured, request.id);
    }

    // The window: the scene tonemapped, then the interface over it.
    wgpu::TextureViewDescriptor surfaceView;
    surfaceView.format = m_surfaceViewFormat;
    drawImage(encoder, surfaceTexture.texture.CreateView(&surfaceView), m_surfaceViewFormat, size, true);

    const wgpu::CommandBuffer commands = encoder.Finish();
    m_queue.Submit(1, &commands);
#if defined(__EMSCRIPTEN__)
    constexpr wgpu::CallbackMode mapMode = wgpu::CallbackMode::AllowSpontaneous;
#else
    constexpr wgpu::CallbackMode mapMode = wgpu::CallbackMode::AllowProcessEvents;
    static_cast<void>(m_surface.Present());
#endif
    const auto map = [](Readback& readback, std::uint64_t bytes) {
        static_cast<void>(readback.buffer.MapAsync(wgpu::MapMode::Read, 0, static_cast<std::size_t>(bytes), mapMode,
                                                   [state = readback.state](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                                       *state = status == wgpu::MapAsyncStatus::Success ? 1 : -1;
                                                   }));
    };
    for (Readback& readback : m_readbacks)
    {
        if (*readback.state == 0 && readback.buffer.GetMapState() == wgpu::BufferMapState::Unmapped)
        {
            map(readback, std::uint64_t{readback.bytesPerRow} * readback.size.height);
        }
    }
    if (measured)
    {
        map(*measured, std::uint64_t{luminanceGridWidth} * luminanceGridHeight * sizeof(float));
        m_luminance = std::move(measured);
        m_measuredExposure = m_exposure;
    }
    collectReadbacks();
    return {};
}

std::vector<PickResult> WebGpuRenderer::takePickResults()
{
    // Picking comes with the tools of the web, after step 4.
    return {};
}

std::uint64_t WebGpuRenderer::requestCapture(std::uint32_t maxWidth, std::uint32_t maxHeight, std::optional<std::uint32_t> view)
{
    static_cast<void>(view);
    const std::uint64_t id = m_nextCapture++;
    m_captureRequests.push_back({.id = id, .maxSize = {maxWidth, maxHeight}});
    return id;
}

std::uint64_t WebGpuRenderer::requestWindowCapture()
{
    const std::uint64_t id = m_nextCapture++;
    m_captureRequests.push_back({.id = id, .window = true});
    return id;
}

std::vector<CapturedImage> WebGpuRenderer::takeCaptures()
{
    collectReadbacks();
    return std::exchange(m_captured, {});
}

RendererStats WebGpuRenderer::stats() const noexcept
{
    RendererStats stats;
    stats.drawCalls = m_drawCalls;
    stats.meshCount = m_meshes.size();
    stats.textureCount = m_textures.size();
    stats.materialCount = m_materials.size();
    stats.shaderCount = m_shaders.size();
    stats.ev100 = m_ev100;
    stats.swapchainExtent = m_surfaceSize;
    stats.sceneExtent = m_sceneSize;
    return stats;
}

} // namespace devex::render::webgpu
