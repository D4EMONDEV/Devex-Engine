#include "ToolsState.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/render/CubeMaps.hpp>
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
    if (state.probeBake)
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

} // namespace devex::tools::detail
