#include "ToolsState.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/navigation/NavigationWorld.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <imgui.h>

#include <chrono>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

namespace devex::tools::detail {
namespace {

using render::OverlayVertex;

constexpr math::Vec4 borderColor{0.25f, 0.85f, 1.0f, 0.95f};
constexpr math::Vec4 innerColor{0.25f, 0.85f, 1.0f, 0.3f};
constexpr math::Vec4 agentColor{0.4f, 1.0f, 0.75f, 0.9f};
constexpr math::Vec4 pathColor{1.0f, 0.85f, 0.25f, 1.0f};
constexpr math::Vec4 obstacleColor{1.0f, 0.55f, 0.2f, 0.9f};
// Lines float over the ground they lie on, so that it does not hide them.
constexpr float lift = 0.04f;

void addLine(std::vector<OverlayVertex>& lines, math::Vec3 start, math::Vec3 end, math::Vec4 color)
{
    lines.push_back({start, color});
    lines.push_back({end, color});
}

void addFlatCircle(std::vector<OverlayVertex>& lines, math::Vec3 center, float radius, math::Vec4 color)
{
    constexpr int segments = 24;
    for (int segment = 0; segment < segments; ++segment)
    {
        const float first = 2.0f * std::numbers::pi_v<float> * static_cast<float>(segment) / segments;
        const float second = 2.0f * std::numbers::pi_v<float> * static_cast<float>(segment + 1) / segments;
        addLine(lines, center + math::Vec3{std::cos(first), 0.0f, std::sin(first)} * radius,
                center + math::Vec3{std::cos(second), 0.0f, std::sin(second)} * radius, color);
    }
}

// An upright cylinder from its base.
void addCylinder(std::vector<OverlayVertex>& lines, math::Vec3 base, float radius, float height, math::Vec4 color)
{
    const math::Vec3 up{0.0f, height, 0.0f};
    addFlatCircle(lines, base, radius, color);
    addFlatCircle(lines, base + up, radius, color);
    for (const math::Vec3 side : {math::Vec3{radius, 0.0f, 0.0f}, math::Vec3{-radius, 0.0f, 0.0f}, math::Vec3{0.0f, 0.0f, radius},
                                  math::Vec3{0.0f, 0.0f, -radius}})
    {
        addLine(lines, base + side, base + side + up, color);
    }
}

[[nodiscard]] std::string fileStem(const std::filesystem::path& path)
{
    return path.stem().string();
}

// Bakes the navigation mesh of a surface from the colliders of the scene, writes it next to the scene
// and points the surface at it, as one undoable step.
void bake(ToolsState& state, scene::Scene& scene, scene::Entity entity)
{
    state.navMeshBakeFailed = true;
    if (state.database == nullptr || state.scenePath.empty())
    {
        state.navMeshBakeStatus = "Save the scene first: its navigation mesh is written next to it.";
        return;
    }
    const auto started = std::chrono::steady_clock::now();
    const scene::NavMeshSurface& surface = scene.get<scene::NavMeshSurface>(entity);
    const navigation::NavGeometry geometry = navigation::collectGeometry(scene, state.meshes);
    const core::Result<asset::NavMeshData> baked = navigation::bakeNavMesh(geometry, navigation::buildSettingsOf(surface));
    if (!baked)
    {
        state.navMeshBakeStatus = std::format("Not baked: {}", baked.error().message);
        return;
    }
    // One file per scene, or per surface when a scene has several.
    std::size_t surfaces = 0;
    for ([[maybe_unused]] auto [other, component] : scene.view<scene::NavMeshSurface>())
    {
        ++surfaces;
    }
    const std::string name = surfaces > 1 ? std::format("{} {}", fileStem(state.scenePath), scene.name(entity)) : fileStem(state.scenePath);
    const std::filesystem::path file = state.scenePath.parent_path() / (name + std::string(asset::navMeshExtension));
    if (core::Result<void> written = core::writeFileAtomically(file, asset::encodeNavMesh(*baked)); !written)
    {
        state.navMeshBakeStatus = std::format("Not saved: {}", written.error().message);
        return;
    }
    const std::string resource = state.database->project().resourcePath(file);
    state.database->refresh();
    const std::optional<asset::AssetId> id = state.database->findByPath(resource);
    if (!id)
    {
        state.navMeshBakeStatus = std::format("{} is written, but outside the assets folder: move the scene there", resource);
        return;
    }
    // Imported at once, so that the new mesh is there to load and to draw.
    state.database->waitForImports();
    if (surface.navMesh != *id)
    {
        state.pendingCommand = makeSetFieldCommand(scene.uuid(entity), "NavMeshSurface", "nav_mesh",
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &surface.navMesh),
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &*id));
    }
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    state.navMeshBakeStatus = std::format("Baked in {} ms from {} triangles: {} tiles, {} layers, {} KB, in {}.", milliseconds,
                                          geometry.triangleCount(), baked->tilesX * baked->tilesZ, baked->layers.size(),
                                          (asset::encodeNavMesh(*baked).size() + 1023) / 1024, resource);
    state.navMeshBakeFailed = false;
    state.navMeshDrawn.reset();
}

// The lines of a navigation mesh, made again when another one is drawn.
[[nodiscard]] const navigation::NavMeshLines* linesOf(ToolsState& state, asset::AssetId id)
{
    const std::shared_ptr<const asset::NavMeshData> data = state.navMeshes && id.isValid() ? state.navMeshes(id) : nullptr;
    if (data == nullptr)
    {
        return nullptr;
    }
    if (data != state.navMeshDrawn)
    {
        core::Result<navigation::NavMeshLines> lines = navigation::navMeshLines(*data);
        if (!lines)
        {
            DEVEX_LOG_WARNING("The navigation mesh cannot be drawn: {}", lines.error());
        }
        state.navMeshLines = lines ? std::move(*lines) : navigation::NavMeshLines{};
        state.navMeshDrawn = data;
    }
    return &state.navMeshLines;
}

} // namespace

void drawNavMeshBaker(ToolsState& state, scene::Scene& scene, scene::Entity entity)
{
    const ThemeColors& colors = themeColors();
    const scene::NavMeshSurface& surface = scene.get<scene::NavMeshSurface>(entity);
    ImGui::PushID("navmesh baker");
    ImGui::Spacing();
    if (const navigation::NavMeshLines* const lines = linesOf(state, surface.navMesh))
    {
        const std::shared_ptr<const asset::NavMeshData> data = state.navMeshDrawn;
        ImGui::TextDisabled("%zu polygons in %d tiles, baked from %u triangles", lines->polygons, data->tilesX * data->tilesZ,
                            data->triangles);
        if (data->settings != navigation::buildSettingsOf(surface))
        {
            ImGui::TextColored(uiColor(colors.warning), "The settings changed since the bake: bake again.");
        }
    }
    else
    {
        ImGui::TextDisabled(surface.navMesh.isValid() ? "The navigation mesh is loading, or cannot be read." : "Not baked yet.");
    }
    const bool editing = state.playState == PlayState::Editing;
    if (primaryButton(icons::Footprints, "Bake", 0.0f, editing))
    {
        bake(state, scene, entity);
    }
    ImGui::SetItemTooltip("Finds where agents fit on the colliders that stay put, and writes the navigation mesh next to the scene");
    ImGui::SameLine();
    if (labelButton(icons::Close, "Clear", 0.0f, editing && surface.navMesh.isValid()))
    {
        const asset::AssetId none;
        state.pendingCommand = makeSetFieldCommand(scene.uuid(entity), "NavMeshSurface", "nav_mesh",
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &surface.navMesh),
                                                   scene::writeFieldValue(reflection::ValueKind::AssetId, &none));
        state.navMeshBakeStatus.clear();
    }
    if (!state.navMeshBakeStatus.empty())
    {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(uiColor(state.navMeshBakeFailed ? colors.error : colors.textDim), "%s", state.navMeshBakeStatus.c_str());
        ImGui::PopTextWrapPos();
    }
    ImGui::PopID();
}

void addNavigationLines(ToolsState& state, scene::Scene& scene, const std::unordered_set<std::uint32_t>& shown, bool showAll,
                        std::vector<render::OverlayVertex>& lines)
{
    const auto visible = [&](scene::Entity entity) { return showAll || shown.contains(entity.index); };
    for ([[maybe_unused]] auto [entity, surface] : scene.view<scene::NavMeshSurface>())
    {
        const navigation::NavMeshLines* const mesh = visible(entity) ? linesOf(state, surface.navMesh) : nullptr;
        if (mesh == nullptr)
        {
            continue;
        }
        const math::Vec3 up{0.0f, lift, 0.0f};
        for (std::size_t point = 0; point + 1 < mesh->inner.size(); point += 2)
        {
            addLine(lines, mesh->inner[point] + up, mesh->inner[point + 1] + up, innerColor);
        }
        for (std::size_t point = 0; point + 1 < mesh->borders.size(); point += 2)
        {
            addLine(lines, mesh->borders[point] + up, mesh->borders[point + 1] + up, borderColor);
        }
        // Agents walk one navigation mesh: the first surface.
        break;
    }
    for ([[maybe_unused]] auto [entity, world, agent] : scene.view<scene::WorldTransform, scene::NavMeshAgent>())
    {
        if (!visible(entity))
        {
            continue;
        }
        const math::Vec3 feet(world.matrix[3]);
        addCylinder(lines, feet, agent.radius, agent.height, agentColor);
        // While the game plays, the way ahead of the agent.
        if (state.navigationWorld != nullptr && state.playState != PlayState::Editing)
        {
            const std::vector<math::Vec3> path = state.navigationWorld->path(entity);
            for (std::size_t corner = 1; corner < path.size(); ++corner)
            {
                addLine(lines, path[corner - 1] + math::Vec3{0.0f, lift * 2.0f, 0.0f}, path[corner] + math::Vec3{0.0f, lift * 2.0f, 0.0f},
                        pathColor);
            }
            if (!path.empty())
            {
                addFlatCircle(lines, path.back() + math::Vec3{0.0f, lift * 2.0f, 0.0f}, 0.2f, pathColor);
            }
        }
    }
    for ([[maybe_unused]] auto [entity, world, obstacle] : scene.view<scene::WorldTransform, scene::NavMeshObstacle>())
    {
        if (!visible(entity))
        {
            continue;
        }
        const math::Vec3 half = math::abs(obstacle.size) * 0.5f;
        if (obstacle.shape == scene::NavObstacleShape::Cylinder)
        {
            const math::Vec3 scale{math::length(math::Vec3(world.matrix[0])), math::length(math::Vec3(world.matrix[1])),
                                   math::length(math::Vec3(world.matrix[2]))};
            const math::Vec3 center(world.matrix * math::Vec4(obstacle.center, 1.0f));
            addCylinder(lines, center - math::Vec3{0.0f, half.y * scale.y, 0.0f}, std::max(half.x * scale.x, half.z * scale.z),
                        half.y * 2.0f * scale.y, obstacleColor);
            continue;
        }
        std::array<math::Vec3, 8> corners;
        for (std::size_t index = 0; index < corners.size(); ++index)
        {
            const math::Vec3 sign{(index & 1) != 0 ? 1.0f : -1.0f, (index & 2) != 0 ? 1.0f : -1.0f, (index & 4) != 0 ? 1.0f : -1.0f};
            corners[index] = math::Vec3(world.matrix * math::Vec4(obstacle.center + sign * half, 1.0f));
        }
        for (std::size_t index = 0; index < corners.size(); ++index)
        {
            for (const std::size_t bit : {1u, 2u, 4u})
            {
                if ((index & bit) == 0)
                {
                    addLine(lines, corners[index], corners[index | bit], obstacleColor);
                }
            }
        }
    }
}

} // namespace devex::tools::detail
