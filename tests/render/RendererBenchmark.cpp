// How the renderer copes with a crowded world: thousands of objects of a few meshes and materials,
// hundreds of local lights, the sun and its cascades, seen over a field the camera only partly
// sees. Hidden, run on demand, in Release, on the machine measured:
//     devex_render_tests "Renderer stress benchmark"
// DEVEX_STRESS_OBJECTS and DEVEX_STRESS_LIGHTS change how many there are; DEVEX_GPU_CULLING=0 has
// the CPU cull and draw every instance, to compare.
#include <devex/asset/Primitives.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::uint32_t countFrom(const char* variable, std::uint32_t fallback)
{
    const std::string value = devex::platform::environmentVariable(variable);
    std::uint32_t count = 0;
    for (const char digit : value)
    {
        if (digit < '0' || digit > '9')
        {
            return fallback;
        }
        count = count * 10 + static_cast<std::uint32_t>(digit - '0');
    }
    return value.empty() ? fallback : count;
}

} // namespace

TEST_CASE("Renderer stress benchmark", "[.][benchmark]")
{
    const std::uint32_t objects = countFrom("DEVEX_STRESS_OBJECTS", 20000);
    const std::uint32_t lights = countFrom("DEVEX_STRESS_LIGHTS", 256);
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 1280, .height = 720, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    const bool gpuCulling = devex::platform::environmentVariable("DEVEX_GPU_CULLING") != "0";
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = false, .gpuCulling = gpuCulling});
    REQUIRE(renderer.has_value());

    // A few meshes and materials, as a level reuses them.
    std::array<devex::render::MeshHandle, 3> meshes{};
    meshes[0] = *renderer->createMesh(devex::asset::makeCube());
    meshes[1] = *renderer->createMesh(devex::asset::makeUvSphere());
    meshes[2] = *renderer->createMesh(devex::asset::makePlane(1.0f, 4));
    std::array<devex::render::MaterialHandle, 8> materials{};
    for (std::size_t index = 0; index < materials.size(); ++index)
    {
        const float shade = 0.3f + 0.08f * static_cast<float>(index);
        materials[index] = renderer->createMaterial({.baseColorFactor = {shade, 0.5f, 1.0f - shade, 1.0f},
                                                     .metallicFactor = index % 3 == 0 ? 1.0f : 0.0f,
                                                     .roughnessFactor = 0.3f + 0.08f * static_cast<float>(index)});
    }

    // A square field of objects 3 m apart, the camera at one corner looking across it.
    const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(objects))));
    std::vector<devex::render::MeshInstance> instances;
    instances.reserve(objects);
    for (std::uint32_t index = 0; index < objects; ++index)
    {
        const float x = static_cast<float>(index % side) * 3.0f;
        const float z = -static_cast<float>(index / side) * 3.0f;
        const float scale = 0.6f + 0.1f * static_cast<float>(index % 5);
        instances.push_back({.mesh = meshes[index % meshes.size()],
                             .material = materials[(index / 3) % materials.size()],
                             .transform = devex::math::scale(devex::math::translate(devex::math::Mat4{1.0f}, {x, scale * 0.5f, z}),
                                                             devex::math::Vec3{scale}),
                             .objectId = index + 1});
    }
    std::vector<devex::render::RenderLight> localLights;
    for (std::uint32_t index = 0; index < lights; ++index)
    {
        const float x = static_cast<float>((index * 37) % side) * 3.0f;
        const float z = -static_cast<float>((index * 61) % side) * 3.0f;
        localLights.push_back({.position = {x, 2.5f, z},
                               .intensity = devex::math::Vec3{300.0f + 100.0f * static_cast<float>(index % 2), 280.0f, 200.0f},
                               .range = 8.0f});
    }
    const float extent = static_cast<float>(side) * 3.0f;
    devex::render::RenderCamera camera;
    camera.view = devex::math::lookAt(devex::math::Vec3{-10.0f, 12.0f, 10.0f}, devex::math::Vec3{extent * 0.5f, 0.0f, -extent * 0.5f},
                                      devex::math::Vec3{0.0f, 1.0f, 0.0f});
    camera.farPlane = 1000.0f;

    devex::core::profiler::setEnabled(true);
    const auto frame = [&](float time) {
        devex::core::profiler::beginFrame();
        platform->pollEvents([](const devex::platform::Event&) {});
        devex::render::RenderWorld& world = renderer->beginFrame();
        // The camera turns a little every frame, as a player looks around: nothing is kept.
        world.camera = camera;
        world.camera.view = devex::math::rotate(devex::math::Mat4{1.0f}, 0.02f * std::sin(time), devex::math::Vec3{0.0f, 1.0f, 0.0f}) *
                            camera.view;
        world.sun = {.direction = {-0.3f, -1.0f, -0.4f}, .illuminance = devex::math::Vec3{60000.0f}};
        world.environment.intensity = 5000.0f;
        world.meshes = instances;
        world.lights = localLights;
        REQUIRE(renderer->endFrame());
        devex::core::profiler::endFrame();
    };
    // Everything uploaded and the pipelines warm before measuring.
    for (int index = 0; index < 60; ++index)
    {
        frame(static_cast<float>(index) * 0.016f);
    }
    devex::core::profiler::clear();
    constexpr int frames = 300;
    const auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < frames; ++index)
    {
        frame(static_cast<float>(60 + index) * 0.016f);
    }
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const devex::render::RendererStats stats = renderer->stats();

    // The zones of the thread of the frames, and the passes of the GPU, a frame's worth each.
    std::map<std::string, double> cpu;
    std::map<std::string, double> gpu;
    std::size_t gpuFrames = 0;
    for (const auto& kept : devex::core::profiler::history())
    {
        for (const devex::core::ProfileZone& zone : kept->cpu)
        {
            if (zone.thread == 0)
            {
                cpu[std::string(zone.depth, ' ') + zone.name] += static_cast<double>(zone.end - zone.begin) / 1.0e6 / frames;
            }
        }
        gpuFrames += kept->gpu.empty() ? 0 : 1;
        for (const devex::core::ProfileZone& zone : kept->gpu)
        {
            gpu[zone.name] += static_cast<double>(zone.end - zone.begin) / 1.0e6;
        }
    }
    std::printf("%u objects, %u lights, %d frames at 1280x720, culled by the %s: %.2f ms a frame\n", objects, lights, frames,
                gpuCulling ? "GPU" : "CPU", total / frames);
    std::printf("%u draw calls, %u instances culled in the last frame\n", stats.drawCalls, stats.culledInstances);
    std::printf("CPU\n");
    std::vector<std::pair<std::string, double>> sorted(cpu.begin(), cpu.end());
    std::ranges::sort(sorted, std::ranges::greater{}, &std::pair<std::string, double>::second);
    for (std::size_t index = 0; index < std::min<std::size_t>(sorted.size(), 16); ++index)
    {
        std::printf("  %7.3f ms  %s\n", sorted[index].second, sorted[index].first.c_str());
    }
    std::printf("GPU\n");
    sorted.assign(gpu.begin(), gpu.end());
    std::ranges::sort(sorted, std::ranges::greater{}, &std::pair<std::string, double>::second);
    for (const auto& [name, milliseconds] : sorted)
    {
        std::printf("  %7.3f ms  %s\n", milliseconds / static_cast<double>(std::max<std::size_t>(gpuFrames, 1)), name.c_str());
    }
    devex::core::profiler::setEnabled(false);
}
