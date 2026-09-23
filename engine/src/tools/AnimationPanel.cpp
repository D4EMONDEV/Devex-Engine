#include "ToolsState.hpp"

#include <devex/animation/AnimationWorld.hpp>
#include <devex/asset/AnimationData.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/AnimationComponents.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace devex::tools::detail {
namespace {

// Height of one track of the timeline, and the width of the names at its left.
[[nodiscard]] float trackHeight()
{
    return ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.y;
}

[[nodiscard]] std::string formatSeconds(float seconds)
{
    return std::format("{:.2f} s", seconds);
}

// The seconds between two marks of the ruler, so that they stay readable at any zoom.
[[nodiscard]] float rulerStep(float duration, float width)
{
    const float target = std::max(duration * 80.0f / std::max(width, 1.0f), 0.01f);
    for (const float step : {0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f})
    {
        if (step >= target)
        {
            return step;
        }
    }
    return 30.0f;
}

// The keys of a clip, gathered per joint: what the timeline draws as a track.
struct Track
{
    std::string joint;
    std::vector<float> times;
};

[[nodiscard]] std::vector<Track> tracksOf(const asset::AnimationClipData& clip)
{
    std::vector<Track> tracks;
    tracks.reserve(clip.joints.size());
    for (const std::string& joint : clip.joints)
    {
        tracks.push_back({joint, {}});
    }
    for (const asset::AnimationChannel& channel : clip.channels)
    {
        if (channel.joint < tracks.size())
        {
            std::vector<float>& times = tracks[channel.joint].times;
            times.insert(times.end(), channel.times.begin(), channel.times.end());
        }
    }
    for (Track& track : tracks)
    {
        std::ranges::sort(track.times);
        const auto duplicates = std::ranges::unique(track.times);
        track.times.erase(duplicates.begin(), duplicates.end());
    }
    return tracks;
}

// The entity the panel animates: the selected one, or the closest ancestor with an Animator.
[[nodiscard]] scene::Entity animatorOf(const scene::Scene& scene, scene::Entity entity)
{
    for (scene::Entity candidate = entity; candidate.isValid(); candidate = scene.parent(candidate))
    {
        if (scene.has<scene::Animator>(candidate))
        {
            return candidate;
        }
    }
    return {};
}

} // namespace

void drawAnimationPanel(ToolsState& state, scene::Scene& scene)
{
    if (!state.showAnimation)
    {
        return;
    }
    const ThemeColors& colors = themeColors();
    if (!ImGui::Begin(animationWindow, &state.showAnimation))
    {
        ImGui::End();
        return;
    }

    const scene::Entity entity = animatorOf(scene, scene.findEntity(state.selection.active()));
    if (!entity.isValid())
    {
        ImGui::TextDisabled("Select an entity with an Animator to play its clips.");
        ImGui::End();
        return;
    }
    scene::Animator& animator = scene.get<scene::Animator>(entity);
    const bool playing = state.playState != PlayState::Editing;

    ImGui::AlignTextToFramePadding();
    iconLabel(icons::Film, colors.animation);
    boldText(scene.name(entity).c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    if (drawAssetPicker(state, "##clip", asset::AssetType::AnimationClip, animator.clip))
    {
        state.animationPreviewTime = 0.0f;
    }
    ImGui::SetItemTooltip("The clip the Animator plays");

    const std::shared_ptr<const animation::Clip> clip =
        state.animationClips ? state.animationClips(animator.clip) : nullptr;
    if (clip == nullptr)
    {
        ImGui::TextDisabled(animator.clip.isValid() ? "The clip cannot be loaded."
                                                    : "Choose an animation clip to preview it.");
        ImGui::End();
        return;
    }
    const float duration = std::max(clip->duration(), 1e-3f);

    // While the game plays, the panel follows the animation instead of driving it.
    if (playing && state.animationWorld != nullptr)
    {
        state.animationPreviewTime = state.animationWorld->time(entity);
        state.animationPreviewPlaying = state.animationWorld->isPlaying(entity);
    }

    if (labelButton(state.animationPreviewPlaying ? icons::Pause : icons::Play,
                    state.animationPreviewPlaying ? "Pause" : "Play"))
    {
        state.animationPreviewPlaying = !state.animationPreviewPlaying;
        if (playing && state.animationWorld != nullptr)
        {
            if (state.animationPreviewPlaying)
            {
                state.animationWorld->resume(entity);
            }
            else
            {
                state.animationWorld->pause(entity);
            }
        }
    }
    ImGui::SameLine();
    if (labelButton(icons::Square, "Stop"))
    {
        state.animationPreviewPlaying = false;
        state.animationPreviewTime = 0.0f;
        if (playing && state.animationWorld != nullptr)
        {
            state.animationWorld->stop(entity);
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox("Loop", &animator.loop);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
    ImGui::DragFloat("##speed", &animator.speed, 0.01f, -4.0f, 4.0f, "%.2fx");
    ImGui::SetItemTooltip("How fast the clip plays; a negative speed plays it backwards");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s / %s", formatSeconds(state.animationPreviewTime).c_str(),
                        formatSeconds(clip->duration()).c_str());

    // The preview advances outside Play; the game drives it during Play.
    if (!playing && state.animationPreviewPlaying)
    {
        state.animationPreviewTime += ImGui::GetIO().DeltaTime * animator.speed;
        if (state.animationPreviewTime > duration || state.animationPreviewTime < 0.0f)
        {
            state.animationPreviewTime = animator.loop
                                             ? state.animationPreviewTime -
                                                   std::floor(state.animationPreviewTime / duration) * duration
                                             : std::clamp(state.animationPreviewTime, 0.0f, duration);
            state.animationPreviewPlaying = animator.loop;
        }
    }

    const std::vector<Track> tracks = tracksOf(clip->data());
    const float nameWidth = ImGui::GetFontSize() * 8.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, nameWidth + 40.0f);
    const float timelineWidth = width - nameWidth;
    const float height = std::min(ImGui::GetContentRegionAvail().y,
                                  trackHeight() * static_cast<float>(tracks.size() + 2));
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), uiColorU32(colors.field),
                        ImGui::GetStyle().FrameRounding);

    // The ruler, then one track per joint with a diamond at each of its keys.
    const auto timeToX = [&](float time) {
        return origin.x + nameWidth + timelineWidth * std::clamp(time / duration, 0.0f, 1.0f);
    };
    const float step = rulerStep(duration, timelineWidth);
    for (float mark = 0.0f; mark <= duration + 1e-4f; mark += step)
    {
        const float x = timeToX(mark);
        draw->AddLine(ImVec2(x, origin.y), ImVec2(x, origin.y + height), uiColorU32(colors.border));
        draw->AddText(ImVec2(x + 2.0f, origin.y + 2.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                      formatSeconds(mark).c_str());
    }
    for (std::size_t index = 0; index < tracks.size(); ++index)
    {
        const float y = origin.y + trackHeight() * static_cast<float>(index + 1) + trackHeight() * 0.5f;
        if (y > origin.y + height)
        {
            break;
        }
        draw->AddText(ImVec2(origin.x + 4.0f, y - ImGui::GetFontSize() * 0.5f),
                      ImGui::GetColorU32(ImGuiCol_Text), tracks[index].joint.c_str());
        for (const float time : tracks[index].times)
        {
            const float x = timeToX(time);
            const float radius = 3.5f;
            draw->AddQuadFilled(ImVec2(x, y - radius), ImVec2(x + radius, y), ImVec2(x, y + radius),
                                ImVec2(x - radius, y), uiColorU32(colors.animation));
        }
    }

    // Dragging anywhere over the timeline scrubs the clip.
    ImGui::InvisibleButton("##timeline", ImVec2(width, height));
    if (ImGui::IsItemActive() && timelineWidth > 0.0f)
    {
        const float x = ImGui::GetIO().MousePos.x - origin.x - nameWidth;
        state.animationPreviewTime = std::clamp(x / timelineWidth, 0.0f, 1.0f) * duration;
        state.animationPreviewPlaying = false;
        if (playing && state.animationWorld != nullptr)
        {
            state.animationWorld->pause(entity);
            state.animationWorld->setTime(scene, entity, state.animationPreviewTime);
        }
    }
    const float playhead = timeToX(state.animationPreviewTime);
    draw->AddLine(ImVec2(playhead, origin.y), ImVec2(playhead, origin.y + height),
                  uiColorU32(colors.accent), 2.0f);

    // Outside Play, the panel poses the skeleton itself, so the viewport shows the clip.
    if (!playing)
    {
        animation::applyClip(scene, entity, *clip, state.animationPreviewTime);
        state.previewedAnimation = animator.clip;
    }
    ImGui::End();
}

} // namespace devex::tools::detail
