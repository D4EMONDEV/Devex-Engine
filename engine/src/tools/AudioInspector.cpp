#include "ToolsState.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/audio/AudioEngine.hpp>
#include <devex/core/Log.hpp>

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace devex::tools::detail {
namespace {

// The loading modes a clip can ask for, as its import option names them.
struct LoadingChoice
{
    const char* option;
    const char* label;
    const char* tooltip;
};

constexpr std::array loadingChoices{
    LoadingChoice{"auto", "Auto", "Decoded up to 10 seconds, streamed when longer"},
    LoadingChoice{"decoded", "Decoded", "Decoded once when loaded: plays at once, takes memory, for short sounds"},
    LoadingChoice{"streamed", "Streamed", "Decoded while it plays: little memory, for music and ambiences"},
};

[[nodiscard]] std::string formatTime(double seconds)
{
    const auto minutes = static_cast<int>(seconds / 60.0);
    return std::format("{}:{:04.1f}", minutes, seconds - minutes * 60.0);
}

[[nodiscard]] std::string formatSize(std::size_t bytes)
{
    if (bytes < 1024)
    {
        return std::format("{} B", bytes);
    }
    if (bytes < 1024 * 1024)
    {
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    }
    return std::format("{:.2f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

// Reads what the inspector shows of the selected clip, without its file.
void loadClipInfo(ToolsState& state)
{
    state.selectedClipInfo.reset();
    state.selectedClipSize = 0;
    const core::Result<std::vector<std::byte>> bytes = state.database->loadArtifact(state.selectedAsset);
    if (!bytes)
    {
        return;
    }
    core::Result<asset::AudioClipData> info = asset::decodeAudioClipInfo(*bytes);
    if (info)
    {
        state.selectedClipInfo = std::move(*info);
        state.selectedClipSize = bytes->size();
    }
}

// The peaks of the clip, mirrored around the middle, and where the preview plays.
void drawWaveform(const asset::AudioClipData& clip, std::optional<float> playhead)
{
    const ThemeColors& colors = themeColors();
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFontSize() * 4.5f;
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const ImVec2 max(min.x + width, min.y + height);
    ImGui::Dummy(ImVec2(width, height));
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(min, max, uiColorU32(colors.field), ImGui::GetStyle().FrameRounding);
    const float middle = (min.y + max.y) * 0.5f;
    const float halfHeight = height * 0.5f - 3.0f;
    draw->AddLine(ImVec2(min.x, middle), ImVec2(max.x, middle), uiColorU32(colors.border));
    if (clip.waveform.empty() || width < 2.0f)
    {
        return;
    }
    // One bar per pixel column, the loudest slice it covers.
    const ImU32 wave = uiColorU32(colors.audio);
    const auto columns = static_cast<std::size_t>(width);
    for (std::size_t column = 0; column < columns; ++column)
    {
        const std::size_t first = column * clip.waveform.size() / columns;
        const std::size_t last = std::max(first + 1, (column + 1) * clip.waveform.size() / columns);
        std::uint8_t peak = 0;
        for (std::size_t slice = first; slice < last && slice < clip.waveform.size(); ++slice)
        {
            peak = std::max(peak, clip.waveform[slice]);
        }
        const float extent = std::max(0.5f, static_cast<float>(peak) / 255.0f * halfHeight);
        const float x = min.x + static_cast<float>(column) + 0.5f;
        draw->AddLine(ImVec2(x, middle - extent), ImVec2(x, middle + extent), wave);
    }
    if (playhead)
    {
        const float x = min.x + std::clamp(*playhead, 0.0f, 1.0f) * width;
        draw->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), uiColorU32(colors.accent), 2.0f);
    }
}

} // namespace

void selectAsset(ToolsState& state, asset::AssetId id)
{
    if (state.selectedAsset != id)
    {
        state.selectedAsset = id;
        state.selectedClipStale = true;
    }
    // The inspector shows one thing at a time.
    state.selection = core::Uuid{};
    state.selectedCode.clear();
}

void previewAudioClip(ToolsState& state, asset::AssetId clip)
{
    if (state.audio == nullptr || !state.audioClips)
    {
        return;
    }
    state.audio->preview(state.audioClips(clip));
    state.previewedClip = clip;
}

void stopAudioPreview(ToolsState& state)
{
    if (state.audio != nullptr)
    {
        state.audio->stopPreview();
    }
    state.previewedClip = {};
}

void drawAudioClipInspector(ToolsState& state)
{
    const ThemeColors& colors = themeColors();
    const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(state.selectedAsset) : nullptr;
    const std::optional<asset::SourceFile> source =
        state.database != nullptr ? state.database->sourceOf(state.selectedAsset) : std::nullopt;
    if (info == nullptr || !source)
    {
        state.selectedAsset = {};
        return;
    }
    // Read again once an import of the file ends, such as after changing how it loads.
    if (source->status == asset::ImportStatus::Importing)
    {
        state.selectedClipStale = true;
    }
    else if (std::exchange(state.selectedClipStale, false))
    {
        loadClipInfo(state);
    }

    ImGui::AlignTextToFramePadding();
    iconLabel(icons::AudioWaveform, colors.audio);
    boldText(info->name.c_str());
    ImGui::TextDisabled("%s", source->path.c_str());
    ImGui::Spacing();

    if (!state.selectedClipInfo)
    {
        ImGui::TextDisabled(source->status == asset::ImportStatus::Failed ? "The file could not be imported."
                                                                           : "Importing...");
        return;
    }
    const asset::AudioClipData& clip = *state.selectedClipInfo;

    // The preview, which plays on the output of the editor whether or not the game plays.
    const bool previewing = state.audio != nullptr && state.previewedClip == state.selectedAsset && state.audio->isPreviewing();
    if (!previewing && state.previewedClip == state.selectedAsset)
    {
        state.previewedClip = {};
    }
    const bool canPreview = state.audio != nullptr && state.audio->hasDevice();
    if (labelButton(previewing ? icons::Square : icons::Play, previewing ? "Stop" : "Play", 0.0f, canPreview))
    {
        if (previewing)
        {
            stopAudioPreview(state);
        }
        else
        {
            previewAudioClip(state, state.selectedAsset);
        }
    }
    if (!canPreview)
    {
        ImGui::SetItemTooltip("No sound output is available");
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    const double seconds = clip.seconds();
    const double position = previewing ? std::min(state.audio->previewSeconds(), seconds) : 0.0;
    ImGui::TextDisabled("%s / %s", formatTime(position).c_str(), formatTime(seconds).c_str());
    drawWaveform(clip, previewing && seconds > 0.0 ? std::optional(static_cast<float>(position / seconds)) : std::nullopt);
    ImGui::Spacing();

    if (beginProperties("clip"))
    {
        propertyName("Format");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(asset::toString(clip.encoding).data());
        propertyName("Channels");
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%u%s", clip.channels, clip.channels == 1 ? " (mono)" : clip.channels == 2 ? " (stereo)" : "");
        propertyName("Sample rate");
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%u Hz", clip.sampleRate);
        propertyName("Duration");
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%.2f s", seconds);
        propertyName("File size");
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(formatSize(state.selectedClipSize).c_str());

        propertyName("Loading");
        const std::optional<serialization::TextValue> option = state.database->importOption(state.selectedAsset, "loading");
        const std::string* const text = option ? serialization::asString(*option) : nullptr;
        const std::string current = text != nullptr ? *text : "auto";
        const auto chosen = std::ranges::find_if(loadingChoices, [&](const LoadingChoice& choice) { return current == choice.option; });
        const std::string preview =
            std::format("{} ({})", chosen != loadingChoices.end() ? chosen->label : current.c_str(), asset::toString(clip.loading));
        if (beginCombo("##loading", preview.c_str()))
        {
            for (const LoadingChoice& choice : loadingChoices)
            {
                if (ImGui::Selectable(choice.label, current == choice.option) && current != choice.option)
                {
                    if (core::Result<void> changed =
                            state.database->setImportOption(state.selectedAsset, "loading", std::string(choice.option));
                        !changed)
                    {
                        DEVEX_LOG_ERROR("Cannot change how {} loads: {}", info->name, changed.error());
                    }
                }
                ImGui::SetItemTooltip("%s", choice.tooltip);
            }
            ImGui::EndCombo();
        }
        endProperties();
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Play an AudioSource, or a one-shot sound from code.");
}

} // namespace devex::tools::detail
