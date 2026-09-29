// The page of a sound in the inspector: its wave, which plays in the editor, what the file holds, and
// how it loads, applied by Reimport.
#include "InspectorUi.hpp"

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

using scene::Entity;
using Button = PanelButton;

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

// The wave shows at most this many bars, each the loudest of the slices it covers.
constexpr std::size_t waveBars = 256;

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

// Read again once an import of the file ends, such as after changing how it loads.
void refreshClipInfo(ToolsState& state, const asset::SourceFile& source)
{
    if (source.status == asset::ImportStatus::Importing)
    {
        state.selectedClipStale = true;
    }
    else if (std::exchange(state.selectedClipStale, false))
    {
        loadClipInfo(state);
    }
}

class AudioPage final : public InspectorPage
{
public:
    std::string signature(ToolsState& state) override
    {
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (source)
        {
            refreshClipInfo(state, *source);
        }
        return std::format("{}|{}", source ? static_cast<int>(source->status) : -1, state.selectedClipInfo.has_value());
    }

    void build(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        m_settings.show(state.selectedAsset);
        m_loading = {};
        const asset::AssetInfo* const info = state.database->find(state.selectedAsset);
        const std::optional<asset::SourceFile> source = state.database->sourceOf(state.selectedAsset);
        if (info == nullptr || !source)
        {
            return;
        }
        ui.heading(kit, icons::AudioWaveform, themeColors().audio, info->name, source->path);
        if (!state.selectedClipInfo)
        {
            ui.note(nullptr, source->status == asset::ImportStatus::Failed ? "The file could not be imported." : "Importing...",
                    source->status == asset::ImportStatus::Failed ? "error" : "dim");
            return;
        }
        const asset::AudioClipData& clip = *state.selectedClipInfo;

        // The preview, which plays on the output of the editor whether or not the game plays.
        const Entity row = ui.actions(nullptr);
        m_play = ui.action(kit, row, Icon::Play, "Play");
        m_time = ui.text(row, rects::middle({ui.font * 8.0f, ui.line}), "", "dim");
        const float height = std::round(ui.font * 4.5f);
        const Entity box = ui.add(ui.content, "Wave", rects::wide(height), "list");
        ui.scene().add<scene::UiImage>(box);
        m_wave = ui.add(box, "Plot", rects::whole(math::Vec4{ui.font * 0.3f, 3.0f, ui.font * 0.3f, 3.0f}));
        scene::UiPlot plot{.kind = scene::UiPlotKind::MirroredBars,
                           .color = linearColor(themeColors().audio),
                           .lineWidth = 2.0f,
                           .markerColor = linearColor(themeColors().accent)};
        // The loudest slice of each bar.
        const std::size_t bars = std::min(waveBars, clip.waveform.size());
        for (std::size_t bar = 0; bar < bars; ++bar)
        {
            const std::size_t first = bar * clip.waveform.size() / bars;
            const std::size_t last = std::max(first + 1, (bar + 1) * clip.waveform.size() / bars);
            std::uint8_t peak = 0;
            for (std::size_t slice = first; slice < last && slice < clip.waveform.size(); ++slice)
            {
                peak = std::max(peak, clip.waveform[slice]);
            }
            plot.values.push_back(static_cast<float>(peak) / 255.0f);
        }
        ui.scene().add<scene::UiPlot>(m_wave, std::move(plot));

        Section& card = ui.card(kit, "Clip");
        const auto fact = [&](const char* name, std::string value) {
            const FormRow line = ui.formRow(card, name);
            ui.text(line.editor, rects::whole(math::Vec4{ui.font * 0.3f, 0.0f, 0.0f, 0.0f}), std::move(value), "text");
        };
        fact("Format", std::string(asset::toString(clip.encoding)));
        fact("Channels", std::format("{}{}", clip.channels, clip.channels == 1 ? " (mono)" : clip.channels == 2 ? " (stereo)" : ""));
        fact("Sample rate", std::format("{} Hz", clip.sampleRate));
        fact("Duration", std::format("{:.2f} s", clip.seconds()));
        fact("File size", formatSize(state.selectedClipSize));
        m_loadingRow = ui.formRow(card, "Loading");
        std::vector<std::string> labels;
        for (const LoadingChoice& choice : loadingChoices)
        {
            labels.emplace_back(choice.label);
        }
        m_loading = ui.choice(m_loadingRow.editor, std::move(labels));
        m_footer = importFooter(ui, kit, card);
        ui.note(nullptr, "Play an AudioSource, or a one-shot sound from code.", "dim", 2.0f);
    }

    void sync(InspectorUi& ui, ToolsState& state, EditorUiKit& kit) override
    {
        if (!m_loading.isValid() || !state.selectedClipInfo)
        {
            return;
        }
        const asset::AudioClipData& clip = *state.selectedClipInfo;
        const bool previewing = state.audio != nullptr && state.previewedClip == state.selectedAsset && state.audio->isPreviewing();
        if (!previewing && state.previewedClip == state.selectedAsset)
        {
            state.previewedClip = {};
        }
        const bool canPreview = state.audio != nullptr && state.audio->hasDevice();
        ui.enable(m_play, canPreview);
        ui.relabel(kit, m_play, previewing ? "Stop" : "Play");
        ui.scene().get<scene::UiImage>(m_play.icon).texture = kit.icon(previewing ? Icon::Square : Icon::Play);
        ui.tooltip(m_play.entity, canPreview ? "" : "No sound output is available");
        const double seconds = clip.seconds();
        const double position = previewing ? std::min(state.audio->previewSeconds(), seconds) : 0.0;
        ui.scene().get<scene::UiText>(m_time).text = std::format("{} / {}", formatTime(position), formatTime(seconds));
        ui.scene().get<scene::UiPlot>(m_wave).marker = previewing && seconds > 0.0 ? static_cast<float>(position / seconds) : -1.0f;

        const std::string current = m_settings.text(state, "loading", "auto");
        const auto chosen = std::ranges::find_if(loadingChoices, [&](const LoadingChoice& choice) { return current == choice.option; });
        scene::UiDropdown& dropdown = ui.scene().get<scene::UiDropdown>(m_loading);
        // The mode the clip loads with now, beside the choice.
        std::vector<std::string> labels;
        for (const LoadingChoice& choice : loadingChoices)
        {
            labels.push_back(current == choice.option && !m_settings.waits("loading")
                                 ? std::format("{} ({})", choice.label, asset::toString(clip.loading))
                                 : std::string(choice.label));
        }
        if (dropdown.options != labels)
        {
            dropdown.options = std::move(labels);
        }
        dropdown.selected = chosen != loadingChoices.end() ? static_cast<std::int32_t>(chosen - loadingChoices.begin()) : -1;
        dropdown.placeholder = current;
        ui.tooltip(m_loading, chosen != loadingChoices.end() ? chosen->tooltip : "");
        ui.scene().get<scene::UiRect>(m_loadingRow.mark).visible = m_settings.waits("loading");
        syncImportFooter(ui, kit, m_footer, m_settings);
    }

    void answer(InspectorUi& ui, ToolsState& state, EditorUiKit&) override
    {
        if (!m_loading.isValid())
        {
            return;
        }
        const ui::UiWorld& world = ui.panel.world();
        if (world.wasClicked(m_play.entity))
        {
            if (state.audio != nullptr && state.previewedClip == state.selectedAsset && state.audio->isPreviewing())
            {
                stopAudioPreview(state);
            }
            else
            {
                previewAudioClip(state, state.selectedAsset);
            }
        }
        if (world.wasChanged(m_loading))
        {
            const std::int32_t selected = ui.scene().get<scene::UiDropdown>(m_loading).selected;
            if (selected >= 0 && static_cast<std::size_t>(selected) < loadingChoices.size())
            {
                m_settings.set(state, "loading", std::string(loadingChoices[static_cast<std::size_t>(selected)].option));
            }
        }
        answerImportFooter(ui, state, m_footer, m_settings);
    }

private:
    ImportSettings m_settings;
    Button m_play;
    Entity m_time;
    Entity m_wave;
    FormRow m_loadingRow;
    Entity m_loading;
    ImportFooter m_footer;
};

} // namespace

void selectAsset(ToolsState& state, asset::AssetId id)
{
    if (state.selectedAsset != id)
    {
        state.selectedAsset = id;
        state.selectedClipStale = true;
    }
    // The inspector shows one thing at a time.
    state.selection.clear();
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

std::unique_ptr<InspectorPage> makeAudioPage()
{
    return std::make_unique<AudioPage>();
}

} // namespace devex::tools::detail
