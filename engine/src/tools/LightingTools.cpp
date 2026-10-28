#include "ToolsState.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/render/CubeMaps.hpp>
#include <devex/render/SphericalHarmonics.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <string>

namespace devex::tools::detail {
namespace {

using render::OverlayVertex;

constexpr math::Vec4 boxColor{0.55f, 0.85f, 1.0f, 0.9f};
constexpr math::Vec4 blendColor{0.55f, 0.85f, 1.0f, 0.35f};
constexpr math::Vec4 captureColor{1.0f, 0.85f, 0.3f, 1.0f};
// Frames the renderer may take to answer before the bake gives up.
constexpr std::uint32_t patience = 120;

void addLine(std::vector<OverlayVertex>& lines, math::Vec3 start, math::Vec3 end, math::Vec4 color)
{
    lines.push_back({start, color});
    lines.push_back({end, color});
}

// The twelve edges of a box of a transform, half as large as given on each side.
void addBox(std::vector<OverlayVertex>& lines, const math::Mat4& transform, math::Vec3 half, math::Vec4 color)
{
    const auto corner = [&](int index) {
        const math::Vec3 local{(index & 1) != 0 ? half.x : -half.x, (index & 2) != 0 ? half.y : -half.y,
                               (index & 4) != 0 ? half.z : -half.z};
        return math::Vec3(transform * math::Vec4(local, 1.0f));
    };
    for (int index = 0; index < 8; ++index)
    {
        for (const int axis : {1, 2, 4})
        {
            if ((index & axis) == 0)
            {
                addLine(lines, corner(index), corner(index | axis), color);
            }
        }
    }
}

// A file name made of what an entity is called: the characters a file system refuses become spaces.
[[nodiscard]] std::string fileSafe(std::string_view name)
{
    std::string safe;
    for (const char character : name)
    {
        safe += std::string_view("/\\:*?\"<>|").find(character) == std::string_view::npos ? character : ' ';
    }
    return safe.empty() ? std::string("Probe") : safe;
}

// The camera that captures the faces of a probe: square, at 90 degrees, exposed by hand, without
// what blurs, darkens or glows over the image.
[[nodiscard]] render::RenderCamera captureCamera(const render::RenderCamera& base, float ev100)
{
    render::RenderCamera camera = base;
    camera.projection = render::Projection::Perspective;
    camera.verticalFov = math::radians(90.0f);
    camera.nearPlane = 0.05f;
    camera.autoExposure = false;
    camera.ev100 = ev100;
    camera.exposureCompensation = 0.0f;
    camera.antialiasing = render::Antialiasing::None;
    camera.ambientOcclusion = 0.0f;
    camera.bloom = 0.0f;
    camera.vignette = 0.0f;
    camera.grain = 0.0f;
    camera.chromaticAberration = 0.0f;
    return camera;
}

// The exposure the faces are drawn at: that of the scene in the editor, so that what glows there
// glows as much in what the probes keep, and the light stays within the half floats of the image.
[[nodiscard]] float captureEv100(const ToolsState& state)
{
    const float ev100 = state.renderer.stats().ev100;
    return ev100 != 0.0f ? ev100 : 12.0f;
}

// Whether what the scene shows is still on its way to the GPU, which the faces would leave out.
[[nodiscard]] bool stillLoading(const ToolsState& state)
{
    return state.renderer.stats().pendingUploads > 0 || (state.pendingLoads && state.pendingLoads() > 0);
}

// A face as a camera drew it, mirrored left to right, put back the way cube maps keep it.
void unmirror(const std::vector<float>& drawn, std::uint32_t size, std::span<float> face)
{
    for (std::uint32_t y = 0; y < size; ++y)
    {
        for (std::uint32_t x = 0; x < size; ++x)
        {
            const std::size_t from = (std::size_t{y} * size + (size - 1 - x)) * 3;
            const std::size_t to = (std::size_t{y} * size + x) * 3;
            std::copy_n(drawn.begin() + static_cast<std::ptrdiff_t>(from), 3, face.begin() + static_cast<std::ptrdiff_t>(to));
        }
    }
}

// A file next to the scene named after it and an entity, and numbered when that name is taken.
[[nodiscard]] std::filesystem::path fileNextToScene(const ToolsState& state, const std::vector<std::filesystem::path>& taken,
                                                    std::string_view name, std::string_view extension)
{
    std::string stem = std::format("{} {}", state.scenePath.stem().string(), fileSafe(name));
    for (int number = 2; std::ranges::any_of(taken, [&](const std::filesystem::path& file) {
             return file.stem() == std::filesystem::path(stem).filename();
         });
         ++number)
    {
        stem = std::format("{} {} {}", state.scenePath.stem().string(), fileSafe(name), number);
    }
    return state.scenePath.parent_path() / (stem + std::string(extension));
}

void stop(ToolsState& state, std::string status, bool failed)
{
    state.probeBake.reset();
    state.probeBakeStatus = std::move(status);
    state.probeBakeFailed = failed;
}

// Points the probes at the files written, once they are imported, as one undoable step.
void finish(ToolsState& state)
{
    ProbeBake& bake = *state.probeBake;
    state.database->refresh();
    state.database->waitForImports();
    std::vector<std::unique_ptr<Command>> commands;
    std::uintmax_t bytes = 0;
    for (std::size_t index = 0; index < bake.probes.size() && index < bake.files.size(); ++index)
    {
        const ProbeBake::Probe& probe = bake.probes[index];
        const std::string resource = state.database->project().resourcePath(bake.files[index]);
        const std::optional<asset::AssetId> id = state.database->findByPath(resource);
        if (!id)
        {
            stop(state, std::format("{} is written, but outside the assets folder: move the scene there", resource), true);
            return;
        }
        std::error_code error;
        bytes += std::filesystem::file_size(bake.files[index], error);
        if (probe.previous != *id)
        {
            commands.push_back(makeSetFieldCommand(probe.entity, "ReflectionProbe", "capture",
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &probe.previous),
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &*id)));
        }
    }
    if (!commands.empty())
    {
        state.pendingCommand = makeCompositeCommand(std::move(commands), "Bake Reflection Probes");
    }
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bake.started).count();
    const std::size_t count = bake.files.size();
    stop(state,
         std::format("Baked {} {} in {} ms, {} KB next to the scene.", count, count == 1 ? "probe" : "probes", milliseconds,
                     (bytes + 1023) / 1024),
         false);
}

// Writes the probe whose six faces came back, then moves on to the next one.
void writeProbe(ToolsState& state)
{
    ProbeBake& bake = *state.probeBake;
    const ProbeBake::Probe& probe = bake.probes[bake.current];
    const std::size_t pixels = std::size_t{probe.size} * probe.size;
    std::vector<float> rgb(pixels * 3 * 6);
    for (std::size_t face = 0; face < 6; ++face)
    {
        const std::vector<float>& drawn = bake.faces[face];
        if (drawn.size() != pixels * 3)
        {
            stop(state, std::format("Not baked: the renderer drew {} with {} values", probe.name, drawn.size()), true);
            return;
        }
        // The camera saw the face mirrored left to right.
        for (std::uint32_t y = 0; y < probe.size; ++y)
        {
            for (std::uint32_t x = 0; x < probe.size; ++x)
            {
                const std::size_t from = (std::size_t{y} * probe.size + (probe.size - 1 - x)) * 3;
                const std::size_t to = face * pixels * 3 + (std::size_t{y} * probe.size + x) * 3;
                std::copy_n(drawn.begin() + static_cast<std::ptrdiff_t>(from), 3, rgb.begin() + static_cast<std::ptrdiff_t>(to));
            }
        }
    }
    const asset::ReflectionProbeData data = asset::makeReflectionProbe(probe.size, rgb);
    // One file per probe, named after the scene and the entity.
    std::string stem = std::format("{} {}", state.scenePath.stem().string(), fileSafe(probe.name));
    for (int number = 2; std::ranges::any_of(bake.files, [&](const std::filesystem::path& file) {
             return file.stem() == std::filesystem::path(stem).filename();
         });
         ++number)
    {
        stem = std::format("{} {} {}", state.scenePath.stem().string(), fileSafe(probe.name), number);
    }
    const std::filesystem::path file = state.scenePath.parent_path() / (stem + std::string(asset::reflectionProbeExtension));
    if (core::Result<void> written = core::writeFileAtomically(file, asset::encodeReflectionProbe(data)); !written)
    {
        stop(state, std::format("Not saved: {}", written.error().message), true);
        return;
    }
    bake.files.push_back(file);
    bake.faces = {};
    bake.sent = false;
    ++bake.current;
    if (bake.current == bake.probes.size())
    {
        finish(state);
    }
}

} // namespace

void bakeReflectionProbes(ToolsState& state, scene::Scene& scene, std::optional<scene::Entity> only)
{
    if (state.probeBake || state.volumeBake)
    {
        return;
    }
    state.probeBakeFailed = true;
    if (state.database == nullptr || state.scenePath.empty())
    {
        state.probeBakeStatus = "Save the scene first: the captures are written next to it.";
        return;
    }
    if (state.playState != PlayState::Editing)
    {
        state.probeBakeStatus = "Stop the game first: the probes capture the scene as it is edited.";
        return;
    }
    ProbeBake bake;
    for ([[maybe_unused]] auto [entity, probe] : scene.view<scene::ReflectionProbe>())
    {
        if (!only || *only == entity)
        {
            bake.probes.push_back({.entity = scene.uuid(entity),
                                   .name = std::string(scene.name(entity)),
                                   .previous = probe.capture,
                                   .size = scene::probePixels(probe.resolution)});
        }
    }
    if (bake.probes.empty())
    {
        state.probeBakeStatus = "The scene has no reflection probe.";
        return;
    }
    // The faces are drawn at the exposure the scene is seen with: what glows there glows as much in
    // its reflections, and the light stays within the half floats of the scene image.
    const float ev100 = state.renderer.stats().ev100;
    bake.ev100 = ev100 != 0.0f ? ev100 : 12.0f;
    bake.started = std::chrono::steady_clock::now();
    state.probeBake = std::move(bake);
    state.probeBakeStatus = "Baking...";
    state.probeBakeFailed = false;
}

void prepareProbeBake(ToolsState& state, scene::Scene& scene, render::RenderWorld& world)
{
    if (!state.probeBake)
    {
        return;
    }
    ProbeBake& bake = *state.probeBake;
    if (state.playState != PlayState::Editing)
    {
        stop(state, "Not baked: the game started.", true);
        return;
    }
    const ProbeBake::Probe& probe = bake.probes[bake.current];
    // What is still loading would be left out of the faces: the bake waits for it.
    const bool loading = state.renderer.stats().pendingUploads > 0 || (state.pendingLoads && state.pendingLoads() > 0);
    if (!bake.sent && loading)
    {
        if (++bake.waited > patience * 10)
        {
            stop(state, "Not baked: the assets of the scene did not finish loading.", true);
        }
        return;
    }
    if (!bake.sent)
    {
        const scene::Entity entity = scene.findEntity(probe.entity);
        const scene::WorldTransform* const transform = entity.isValid() ? scene.tryGet<scene::WorldTransform>(entity) : nullptr;
        const scene::ReflectionProbe* const component = entity.isValid() ? scene.tryGet<scene::ReflectionProbe>(entity) : nullptr;
        if (transform == nullptr || component == nullptr)
        {
            stop(state, std::format("Not baked: {} is gone.", probe.name), true);
            return;
        }
        bake.position = math::Vec3(transform->matrix * math::Vec4(component->captureOffset, 1.0f));
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            bake.requests[face] = state.renderer.requestViewCapture(probeViewBase + face, probe.size, probe.size);
        }
        bake.sent = true;
        bake.waited = 0;
    }
    else if (++bake.waited > patience)
    {
        stop(state, std::format("Not baked: the renderer did not draw {}.", probe.name), true);
        return;
    }
    // The views are drawn every frame until their faces come back: a frame reads a picture once the
    // view is drawn again in its place.
    render::RenderCamera camera = world.camera;
    camera.projection = render::Projection::Perspective;
    camera.verticalFov = math::radians(90.0f);
    camera.nearPlane = 0.05f;
    camera.autoExposure = false;
    camera.ev100 = bake.ev100;
    camera.exposureCompensation = 0.0f;
    camera.antialiasing = render::Antialiasing::None;
    camera.ambientOcclusion = 0.0f;
    camera.bloom = 0.0f;
    camera.vignette = 0.0f;
    camera.grain = 0.0f;
    camera.chromaticAberration = 0.0f;
    for (std::uint32_t face = 0; face < 6; ++face)
    {
        camera.view = render::cubeFaceView(bake.position, face);
        world.views.push_back(render::RenderView{.id = probeViewBase + face,
                                                 .camera = camera,
                                                 .extent = {probe.size, probe.size},
                                                 .radiance = true});
    }
}

void receiveProbeFaces(ToolsState& state, std::vector<render::CapturedImage>& captures)
{
    if (!state.probeBake || !state.probeBake->sent)
    {
        return;
    }
    ProbeBake& bake = *state.probeBake;
    std::erase_if(captures, [&](render::CapturedImage& captured) {
        const auto face = std::ranges::find(bake.requests, captured.request);
        if (face == bake.requests.end())
        {
            return false;
        }
        bake.faces[static_cast<std::size_t>(face - bake.requests.begin())] = std::move(captured.radiance);
        return true;
    });
    if (std::ranges::all_of(bake.faces, [](const std::vector<float>& face) { return !face.empty(); }))
    {
        writeProbe(state);
    }
}

std::string reflectionProbeSummary(ToolsState& state, const scene::ReflectionProbe& probe)
{
    if (state.probeBake)
    {
        return std::format("Baking probe {} of {}...", state.probeBake->current + 1, state.probeBake->probes.size());
    }
    if (!probe.capture.isValid())
    {
        return "Not baked yet: surfaces reflect the sky.";
    }
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(probe.capture) : nullptr;
    return info != nullptr ? std::format("Captured in {}", info->name) : std::string("The capture cannot be found: bake again.");
}

void addReflectionProbeLines(scene::Scene& scene, const std::unordered_set<std::uint32_t>& shown, bool showAll,
                             std::vector<render::OverlayVertex>& lines)
{
    for ([[maybe_unused]] auto [entity, transform, probe] : scene.view<scene::WorldTransform, scene::ReflectionProbe>())
    {
        if (!showAll && !shown.contains(entity.index))
        {
            continue;
        }
        const math::Vec3 half = probe.size * 0.5f;
        addBox(lines, transform.matrix, half, boxColor);
        // Where it starts to fade out, when that is inside the box.
        const math::Vec3 inner = half - math::Vec3{probe.blendDistance};
        if (probe.blendDistance > 0.0f && std::min({inner.x, inner.y, inner.z}) > 0.0f)
        {
            addBox(lines, transform.matrix, inner, blendColor);
        }
        // A small star where it captures from.
        const math::Vec3 capture = math::Vec3(transform.matrix * math::Vec4(probe.captureOffset, 1.0f));
        constexpr float arm = 0.2f;
        for (const math::Vec3 axis : {math::Vec3{arm, 0.0f, 0.0f}, math::Vec3{0.0f, arm, 0.0f}, math::Vec3{0.0f, 0.0f, arm}})
        {
            addLine(lines, capture - axis, capture + axis, captureColor);
        }
    }
}

namespace {

void stopVolumes(ToolsState& state, std::string status, bool failed)
{
    if (state.volumeBake)
    {
        for (const render::IrradianceVolumeHandle volume : state.volumeBake->previous)
        {
            state.renderer.destroyIrradianceVolume(volume);
        }
    }
    state.volumeBake.reset();
    state.volumeBakeStatus = std::move(status);
    state.volumeBakeFailed = failed;
}

[[nodiscard]] asset::IrradianceVolumeData volumeData(const VolumeBake::Volume& volume)
{
    return asset::IrradianceVolumeData{.countX = volume.counts[0], .countY = volume.counts[1], .countZ = volume.counts[2],
                                       .coefficients = volume.coefficients};
}

// Where a probe of a volume stands in the world: the grid spans the box from corner to corner.
[[nodiscard]] math::Vec3 probePosition(const VolumeBake::Volume& volume, std::size_t probe)
{
    const std::size_t x = probe % volume.counts[0];
    const std::size_t y = probe / volume.counts[0] % volume.counts[1];
    const std::size_t z = probe / (std::size_t{volume.counts[0]} * volume.counts[1]);
    const math::Vec3 step = volume.size / (math::Vec3{static_cast<float>(volume.counts[0]), static_cast<float>(volume.counts[1]),
                                                     static_cast<float>(volume.counts[2])} -
                                           1.0f);
    const math::Vec3 local = volume.size * -0.5f + step * math::Vec3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    return math::Vec3(volume.transform * math::Vec4(local, 1.0f));
}

// Writes the volumes once every bounce is baked, and points them at their files as one undoable step.
void finishVolumes(ToolsState& state)
{
    VolumeBake& bake = *state.volumeBake;
    std::vector<std::filesystem::path> files;
    for (const VolumeBake::Volume& volume : bake.volumes)
    {
        const std::filesystem::path file = fileNextToScene(state, files, volume.name, asset::irradianceVolumeExtension);
        if (core::Result<void> written = core::writeFileAtomically(file, asset::encodeIrradianceVolume(volumeData(volume))); !written)
        {
            stopVolumes(state, std::format("Not saved: {}", written.error().message), true);
            return;
        }
        files.push_back(file);
    }
    state.database->refresh();
    state.database->waitForImports();
    std::vector<std::unique_ptr<Command>> commands;
    for (std::size_t index = 0; index < bake.volumes.size(); ++index)
    {
        const VolumeBake::Volume& volume = bake.volumes[index];
        const std::string resource = state.database->project().resourcePath(files[index]);
        const std::optional<asset::AssetId> id = state.database->findByPath(resource);
        if (!id)
        {
            stopVolumes(state, std::format("{} is written, but outside the assets folder: move the scene there", resource), true);
            return;
        }
        if (volume.previous != *id)
        {
            commands.push_back(makeSetFieldCommand(volume.entity, "LightProbeVolume", "irradiance",
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &volume.previous),
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &*id)));
        }
    }
    if (!commands.empty())
    {
        state.pendingCommand = makeCompositeCommand(std::move(commands), "Bake Irradiance Volumes");
    }
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - bake.started).count();
    const std::size_t count = bake.volumes.size();
    stopVolumes(state,
                std::format("Baked {} {} of {} probes, {} {}, in {} ms.", count, count == 1 ? "volume" : "volumes", bake.probes.size(),
                            bake.bounces, bake.bounces == 1 ? "bounce" : "bounces", milliseconds),
                false);
}

// Once every probe of a bounce came back: the light of this bounce goes to the GPU for the next one,
// or the bake ends.
void finishBounce(ToolsState& state)
{
    VolumeBake& bake = *state.volumeBake;
    if (bake.bounce >= bake.bounces)
    {
        finishVolumes(state);
        return;
    }
    for (const render::IrradianceVolumeHandle volume : bake.previous)
    {
        state.renderer.destroyIrradianceVolume(volume);
    }
    bake.previous.clear();
    for (const VolumeBake::Volume& volume : bake.volumes)
    {
        core::Result<render::IrradianceVolumeHandle> created = state.renderer.createIrradianceVolume(volumeData(volume));
        if (!created)
        {
            stopVolumes(state, std::format("Not baked: {}", created.error().message), true);
            return;
        }
        bake.previous.push_back(*created);
    }
    ++bake.bounce;
    bake.next = 0;
    bake.received = 0;
}

} // namespace

void bakeIrradianceVolumes(ToolsState& state, scene::Scene& scene)
{
    if (state.volumeBake || state.probeBake)
    {
        return;
    }
    state.volumeBakeFailed = true;
    if (state.database == nullptr || state.scenePath.empty())
    {
        state.volumeBakeStatus = "Save the scene first: the light is written next to it.";
        return;
    }
    if (state.playState != PlayState::Editing)
    {
        state.volumeBakeStatus = "Stop the game first: the volumes capture the scene as it is edited.";
        return;
    }
    VolumeBake bake;
    for ([[maybe_unused]] auto [entity, transform, volume] : scene.view<scene::WorldTransform, scene::LightProbeVolume>())
    {
        VolumeBake::Volume baked{.entity = scene.uuid(entity),
                                 .name = std::string(scene.name(entity)),
                                 .previous = volume.irradiance,
                                 .transform = transform.matrix,
                                 .size = math::max(volume.size, math::Vec3{0.01f}),
                                 .blendDistance = volume.blendDistance,
                                 .counts = scene::probeCounts(volume)};
        const std::size_t probes = std::size_t{baked.counts[0]} * baked.counts[1] * baked.counts[2];
        if (probes > asset::maxIrradianceProbes)
        {
            state.volumeBakeStatus = std::format("{} would have {} probes, more than {}: space them out.", baked.name, probes,
                                                 asset::maxIrradianceProbes);
            return;
        }
        baked.coefficients.assign(probes * asset::irradianceCoefficients, 0.0f);
        for (std::size_t probe = 0; probe < probes; ++probe)
        {
            bake.probes.emplace_back(bake.volumes.size(), probe);
        }
        bake.bounces = std::max(bake.bounces, std::clamp(volume.bounces, 1, 4));
        bake.volumes.push_back(std::move(baked));
    }
    if (bake.volumes.empty())
    {
        state.volumeBakeStatus = "The scene has no light probe volume.";
        return;
    }
    bake.ev100 = captureEv100(state);
    bake.started = std::chrono::steady_clock::now();
    state.volumeBake = std::move(bake);
    state.volumeBakeStatus = "Baking...";
    state.volumeBakeFailed = false;
}

void prepareVolumeBake(ToolsState& state, render::RenderWorld& world)
{
    if (!state.volumeBake)
    {
        return;
    }
    VolumeBake& bake = *state.volumeBake;
    if (state.playState != PlayState::Editing)
    {
        stopVolumes(state, "Not baked: the game started.", true);
        return;
    }
    // The faces see the light of the previous bounce, and not what the volumes held before the bake.
    world.irradianceVolumes.clear();
    for (std::size_t index = 0; index < bake.previous.size() && index < bake.volumes.size(); ++index)
    {
        const VolumeBake::Volume& volume = bake.volumes[index];
        world.irradianceVolumes.push_back(
            {.volume = bake.previous[index], .transform = volume.transform, .size = volume.size, .blendDistance = volume.blendDistance});
    }
    if (stillLoading(state) && bake.next == 0 && bake.bounce == 1)
    {
        if (++bake.waited > patience * 10)
        {
            stopVolumes(state, "Not baked: the assets of the scene did not finish loading.", true);
        }
        return;
    }
    if (bake.cubes.size() > volumeProbesPerFrame * 4 && ++bake.waited > patience)
    {
        stopVolumes(state, "Not baked: the renderer did not draw the probes.", true);
        return;
    }
    // A few probes a frame, each through six views the renderer answers some frames later; the same
    // views take other probes on the next frames.
    const render::RenderCamera base = captureCamera(world.camera, bake.ev100);
    for (std::uint32_t slot = 0; slot < volumeProbesPerFrame && bake.next < bake.probes.size(); ++slot)
    {
        const auto [volume, probe] = bake.probes[bake.next++];
        VolumeBake::Cube& cube = bake.cubes.emplace_back(VolumeBake::Cube{.volume = volume, .probe = probe});
        const math::Vec3 position = probePosition(bake.volumes[volume], probe);
        render::RenderCamera camera = base;
        for (std::uint32_t face = 0; face < 6; ++face)
        {
            const std::uint32_t view = volumeViewBase + slot * 6 + face;
            camera.view = render::cubeFaceView(position, face);
            world.views.push_back(render::RenderView{.id = view, .camera = camera, .extent = {volumeFaceSize, volumeFaceSize}, .radiance = true});
            cube.requests[face] = state.renderer.requestViewCapture(view, volumeFaceSize, volumeFaceSize);
        }
    }
}

void receiveVolumeFaces(ToolsState& state, std::vector<render::CapturedImage>& captures)
{
    if (!state.volumeBake)
    {
        return;
    }
    VolumeBake& bake = *state.volumeBake;
    std::erase_if(captures, [&](render::CapturedImage& captured) {
        for (VolumeBake::Cube& cube : bake.cubes)
        {
            const auto face = std::ranges::find(cube.requests, captured.request);
            if (face != cube.requests.end())
            {
                cube.faces[static_cast<std::size_t>(face - cube.requests.begin())] = std::move(captured.radiance);
                return true;
            }
        }
        return false;
    });
    // The probes whose six faces came back become harmonics.
    constexpr std::size_t pixels = std::size_t{volumeFaceSize} * volumeFaceSize;
    std::vector<float> faces(pixels * 3 * 6);
    std::erase_if(bake.cubes, [&](const VolumeBake::Cube& cube) {
        if (!std::ranges::all_of(cube.faces, [](const std::vector<float>& face) { return face.size() == pixels * 3; }))
        {
            return false;
        }
        for (std::size_t face = 0; face < 6; ++face)
        {
            unmirror(cube.faces[face], volumeFaceSize, std::span<float>(faces).subspan(face * pixels * 3, pixels * 3));
        }
        const render::IrradianceHarmonics harmonics = render::projectIrradiance(faces, volumeFaceSize);
        float* const out = bake.volumes[cube.volume].coefficients.data() + cube.probe * asset::irradianceCoefficients;
        for (std::size_t index = 0; index < harmonics.size(); ++index)
        {
            out[index * 3] = harmonics[index].x;
            out[index * 3 + 1] = harmonics[index].y;
            out[index * 3 + 2] = harmonics[index].z;
        }
        ++bake.received;
        bake.waited = 0;
        return true;
    });
    if (bake.received == bake.probes.size())
    {
        finishBounce(state);
    }
}

std::string irradianceVolumeSummary(ToolsState& state, const scene::LightProbeVolume& volume)
{
    const std::array<std::uint32_t, 3> counts = scene::probeCounts(volume);
    const std::string probes = std::format("{} × {} × {} probes", counts[0], counts[1], counts[2]);
    if (state.volumeBake)
    {
        return std::format("Baking bounce {} of {}: {} of {} probes...", state.volumeBake->bounce, state.volumeBake->bounces,
                           state.volumeBake->received, state.volumeBake->probes.size());
    }
    if (!volume.irradiance.isValid())
    {
        return std::format("{}, not baked yet: surfaces take the light of the sky.", probes);
    }
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(volume.irradiance) : nullptr;
    return info != nullptr ? std::format("{}, baked in {}", probes, info->name) : std::format("{}: the bake cannot be found, bake again.", probes);
}

void addIrradianceVolumeLines(scene::Scene& scene, const std::unordered_set<std::uint32_t>& shown, std::vector<render::OverlayVertex>& lines)
{
    for ([[maybe_unused]] auto [entity, transform, volume] : scene.view<scene::WorldTransform, scene::LightProbeVolume>())
    {
        if (!shown.contains(entity.index))
        {
            continue;
        }
        addBox(lines, transform.matrix, volume.size * 0.5f, boxColor);
        // A small cross at each probe, as long as there are not too many to draw.
        VolumeBake::Volume grid{.transform = transform.matrix, .size = math::max(volume.size, math::Vec3{0.01f}), .counts = scene::probeCounts(volume)};
        const std::size_t probes = std::size_t{grid.counts[0]} * grid.counts[1] * grid.counts[2];
        if (probes > 4096)
        {
            continue;
        }
        constexpr float arm = 0.06f;
        for (std::size_t probe = 0; probe < probes; ++probe)
        {
            const math::Vec3 at = probePosition(grid, probe);
            for (const math::Vec3 axis : {math::Vec3{arm, 0.0f, 0.0f}, math::Vec3{0.0f, arm, 0.0f}, math::Vec3{0.0f, 0.0f, arm}})
            {
                addLine(lines, at - axis, at + axis, captureColor);
            }
        }
    }
}

} // namespace devex::tools::detail
