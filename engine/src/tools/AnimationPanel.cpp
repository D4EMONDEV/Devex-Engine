// The Animation panel, made with the interface of the engine: the clip of the selected Animator, played
// or scrubbed over the skeleton in the viewport, and its keys on a timeline that zooms and scrolls as
// the one of Godot does, under the events of the clip, which the panel adds, moves, names and removes.
#include "FormUi.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"

#include <devex/animation/AnimationWorld.hpp>
#include <devex/asset/AnimationData.hpp>
#include <devex/asset/AnimationEvents.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Profiler.hpp>
#include <devex/scene/AnimationComponents.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

constexpr std::uint32_t animationSurface = 18;

[[nodiscard]] std::string formatSeconds(float seconds)
{
    return std::format("{:.2f} s", seconds);
}

// The seconds between two marks of the ruler, so that they stay readable at any zoom.
[[nodiscard]] float rulerStep(float span, float width)
{
    const float target = std::max(span * 80.0f / std::max(width, 1.0f), 0.001f);
    for (const float step : {0.01f, 0.02f, 0.05f, 0.1f, 0.25f, 0.5f, 1.0f, 2.0f, 5.0f, 10.0f})
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

struct AnimationUi : FormUi
{
    AnimationUi()
        : FormUi(animationSurface)
    {
    }

    // A mark of the ruler: its line down the tracks, and its time.
    struct MarkView
    {
        Entity rule;
        Entity label;
    };

    bool built = false;
    float builtFont = 0.0f;
    Entity bar;
    Entity glyph;
    Entity name;
    Entity clipBox;
    Entity clipList;
    Button play;
    Button stop;
    Entity loop;
    Entity loopLabel;
    Entity speed;
    Entity time;
    // The name of the event chosen, and what removes it.
    Entity eventBox;
    Entity eventName;
    Button eventDelete;
    Entity message;

    Entity timeline;
    Entity marksLayer;
    Entity tracks;
    Entity trackRows;
    Entity playhead;
    std::vector<MarkView> marks;
    // The marks of the keys of each track, whose bounds follow what is in view.
    std::vector<Entity> keyPlots;
    // The row of the events above the tracks, their marks and the mark of the one chosen.
    Entity eventsRow;
    Entity eventPlot;
    Entity chosenPlot;
    // The events of the clip as the panel edits them, kept in the import settings of its model, and
    // the one chosen.
    asset::AssetId eventsClip;
    std::vector<asset::AnimationEvent> events;
    std::optional<std::size_t> chosenEvent;
    bool draggingEvent = false;
    bool movedEvent = false;
    bool namingEvent = false;
    std::vector<asset::AssetId> clips;
    // The clip whose tracks are shown.
    const animation::Clip* shownClip = nullptr;
    // What is in view of the clip, in seconds; both zero for all of it.
    float visibleBegin = 0.0f;
    float visibleEnd = 0.0f;
    bool scrubbing = false;
    std::optional<bool> shownPlaying;

    [[nodiscard]] float margin() const noexcept
    {
        return std::round(font * 0.5f);
    }
    [[nodiscard]] float barHeight() const noexcept
    {
        return std::round(font * 2.5f);
    }
    [[nodiscard]] float rulerHeight() const noexcept
    {
        return std::round(font * 1.6f);
    }
    [[nodiscard]] float rowHeight() const noexcept
    {
        return std::round(font * 1.55f);
    }
    // The room of the names of the joints, at the left of the tracks, and of the bar that scrolls them.
    [[nodiscard]] float namesWidth() const noexcept
    {
        return std::round(font * 9.0f);
    }
    [[nodiscard]] float rightPad() const noexcept
    {
        return 12.0f;
    }

    void build(EditorUiKit& kit);
    void fillClips(const ToolsState& state, asset::AssetId value);
    void fillTracks(const animation::Clip& clip);
    // Shows a sentence in the place of the controls and of the timeline, or of the timeline alone.
    void say(const char* sentence, bool withBar);
    void layoutRuler(float begin, float end, float width);
    // Writes the events into the import settings of the model of the clip, which imports again.
    void writeEvents(ToolsState& state);
    void update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta);
};

void AnimationUi::build(EditorUiKit& kit)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    built = true;
    builtFont = font;
    marks.clear();
    keyPlots.clear();
    shownClip = nullptr;
    shownPlaying.reset();
    panel.setKeyboardNavigation(false);

    const ThemeColors& colors = themeColors();
    const float side = margin();
    const float tall = std::round(font * 1.85f);
    const float iconSize = std::round(font * 1.2f);
    bar = add({}, "Bar", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}, .offsetMin = {side, 0.0f}, .offsetMax = {-side, barHeight()}});
    scene().add<scene::UiLayout>(bar, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.6f, .align = scene::TextAlign::Left});
    glyph = icon(kit, bar, middle({iconSize, iconSize}), Icon::Film, {});
    name = text(bar, middle({font * 6.0f, tall}), "", "text", true);
    clipBox = add(bar, "Clip", middle({font * 14.0f, tall}));
    clipList = choice(clipBox);
    tooltip(clipList, "The clip the Animator plays");
    play = button(kit, bar, Icon::Play, "Pause", "button", 0.0f, tall);
    stop = button(kit, bar, Icon::Square, "Stop", "button", 0.0f, tall);
    const float box = std::round(font * 1.3f);
    loop = add(bar, "Toggle", middle({box, box}), "toggle");
    scene().add<scene::UiImage>(loop);
    scene().add<scene::UiToggle>(loop);
    scene().add<scene::UiButton>(loop);
    tooltip(loop, "Starts the clip again when it ends");
    loopLabel = text(bar, middle({std::ceil(kit.textWidth(EditorUiKit::regularFont(), "Loop", font)) + 2.0f, tall}), "Loop", "text");
    speed = numberBox(bar, {}, colors.textDim, scene::UiNumberField{.minValue = -4.0f, .maxValue = 4.0f, .dragSpeed = 0.01f, .decimals = 2, .format = "{}x"});
    {
        UiRect& rect = scene().get<UiRect>(speed);
        rect.anchorMin = {0.0f, 0.5f};
        rect.anchorMax = {0.0f, 0.5f};
        rect.offsetMin = {0.0f, -tall * 0.5f};
        rect.offsetMax = {font * 5.0f, tall * 0.5f};
    }
    tooltip(speed, "How fast the clip plays; a negative speed plays it backwards");
    time = text(bar, middle({font * 11.0f, tall}), "", "dim");
    eventBox = add(bar, "Event", middle({font * 9.0f, tall}));
    eventName = textField(eventBox, "Event name");
    tooltip(eventName, "The name of the event chosen, which code hears in OnAnimationEvent");
    eventDelete = button(kit, bar, Icon::Trash, "Delete", "button", 0.0f, tall);
    tooltip(eventDelete.entity, "Removes the event chosen (Delete)");

    message = text({}, whole(math::Vec4{side, barHeight(), side, side}), "", "dim", false, scene::TextAlign::Center);
    scene().get<scene::UiText>(message).wrap = true;

    // The ruler over the tracks, which scroll under it; the lines of its marks run down behind them,
    // and the head that says where the clip is stands over all of it.
    timeline = add({}, "Timeline",
                   UiRect{.anchorMin = {0.0f, 0.0f},
                          .anchorMax = {1.0f, 1.0f},
                          .offsetMin = {side, barHeight()},
                          .offsetMax = {-side, -side},
                          .clipChildren = true},
                   "list");
    scene().add<scene::UiImage>(timeline);
    marksLayer = add(timeline, "Marks", whole());
    tracks = add(timeline, "Tracks",
                 UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, rulerHeight()}, .offsetMax = {0.0f, 0.0f}, .clipChildren = true},
                 "scroll");
    scene().add<scene::UiScroll>(tracks, scene::UiScroll{.speed = rowHeight() * 3.0f});
    trackRows = add(tracks, "Rows", wide(1.0f));
    scene().add<scene::UiLayout>(trackRows, scene::UiLayout{.kind = scene::UiLayoutKind::Column, .spacing = 0.0f, .align = scene::TextAlign::Left});
    playhead = add(timeline, "Playhead", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {0.0f, 0.0f}, .offsetMax = {2.0f, 0.0f}},
                   "mark");
    scene().add<scene::UiImage>(playhead, scene::UiImage{.raycastTarget = false});
}

void AnimationUi::fillClips(const ToolsState& state, asset::AssetId value)
{
    // All the clips of the project while the list may open, the chosen one otherwise.
    const ui::UiWorld& world = panel.world();
    const bool full = world.hovered() == clipList || world.focused() == clipList || world.listedDropdown() == clipList;
    if (world.listedDropdown() == clipList && clips.size() > 1)
    {
        return;
    }
    clips.assign(1, asset::AssetId{});
    if (full && state.database != nullptr)
    {
        for (const asset::AssetInfo& info : state.database->assets(asset::AssetType::AnimationClip))
        {
            clips.push_back(info.id);
        }
    }
    else if (value.isValid())
    {
        clips.push_back(value);
    }
    std::vector<std::string> options;
    std::int32_t selected = -1;
    for (const asset::AssetId clip : clips)
    {
        if (clip == value)
        {
            selected = static_cast<std::int32_t>(options.size());
        }
        options.push_back(assetLabel(state, clip));
    }
    scene::UiDropdown& dropdown = scene().get<scene::UiDropdown>(clipList);
    if (dropdown.options != options)
    {
        dropdown.options = std::move(options);
    }
    dropdown.selected = selected;
    dropdown.placeholder = assetLabel(state, value);
}

void AnimationUi::fillTracks(const animation::Clip& clip)
{
    std::vector<Entity> children;
    for (Entity child = scene().firstChild(trackRows); child.isValid(); child = scene().nextSibling(child))
    {
        children.push_back(child);
    }
    for (const Entity child : children)
    {
        scene().destroyEntity(child);
    }
    keyPlots.clear();
    const std::vector<Track> made = tracksOf(clip.data());
    const math::Vec4 tint = linearColor(themeColors().animation);
    // The events first, in the colour of game code, which hears of them.
    eventsRow = add(trackRows, "Events", wide(rowHeight()));
    scene().add<scene::UiImage>(eventsRow, scene::UiImage{.color = {1.0f, 1.0f, 1.0f, 0.06f}});
    tooltip(eventsRow, "The events of the clip, which code hears in OnAnimationEvent as the clip plays past them. Right-click adds one, "
                       "a drag moves it, Delete removes it.");
    text(eventsRow, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {font * 0.5f, 0.0f}, .offsetMax = {namesWidth() - 4.0f, 0.0f}},
         "Events", "text", true);
    const UiRect eventArea{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {namesWidth(), 0.0f}, .offsetMax = {-rightPad(), 0.0f}};
    const float eventWidth = std::max(std::round(font * 0.4f), 3.0f);
    eventPlot = add(eventsRow, "Marks", eventArea);
    scene().add<scene::UiPlot>(eventPlot, scene::UiPlot{.kind = scene::UiPlotKind::Marks, .color = linearColor(themeColors().gameCode), .lineWidth = eventWidth});
    chosenPlot = add(eventsRow, "Chosen", eventArea);
    scene().add<scene::UiPlot>(chosenPlot, scene::UiPlot{.kind = scene::UiPlotKind::Marks, .color = linearColor(themeColors().text), .lineWidth = eventWidth});
    keyPlots.push_back(eventPlot);
    keyPlots.push_back(chosenPlot);
    for (std::size_t index = 0; index < made.size(); ++index)
    {
        // One line in two a little lighter, so that the eye follows a track across.
        const Entity row = add(trackRows, "Track", wide(rowHeight()));
        if (index % 2 == 1)
        {
            scene().add<scene::UiImage>(row, scene::UiImage{.color = {1.0f, 1.0f, 1.0f, 0.025f}, .raycastTarget = false});
        }
        text(row, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {font * 0.5f, 0.0f}, .offsetMax = {namesWidth() - 4.0f, 0.0f}},
             made[index].joint, "text");
        const Entity keys = add(row, "Keys", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {namesWidth(), 0.0f}, .offsetMax = {-rightPad(), 0.0f}});
        scene().add<scene::UiPlot>(keys, scene::UiPlot{.values = made[index].times,
                                                       .kind = scene::UiPlotKind::Marks,
                                                       .color = tint,
                                                       .lineWidth = std::max(std::round(font * 0.26f), 2.0f)});
        keyPlots.push_back(keys);
    }
    scene().get<UiRect>(trackRows).offsetMax.y = rowHeight() * static_cast<float>(made.size() + 1);
    scene().get<scene::UiScroll>(tracks).offset = {0.0f, 0.0f};
}

void AnimationUi::say(const char* sentence, bool withBar)
{
    scene().get<UiRect>(message).visible = true;
    scene().get<UiRect>(timeline).visible = false;
    scene().get<UiRect>(bar).visible = withBar;
    // With the bar, only what chooses the clip stays.
    for (const Entity control : {play.entity, stop.entity, loop, loopLabel, speed, time, eventBox, eventDelete.entity})
    {
        scene().get<UiRect>(control).visible = false;
    }
    scene::UiText& shown = scene().get<scene::UiText>(message);
    if (shown.text != sentence)
    {
        shown.text = sentence;
    }
}

void AnimationUi::layoutRuler(float begin, float end, float width)
{
    const float step = rulerStep(end - begin, width);
    std::size_t used = 0;
    // A few marks past what is in view cost nothing, and none is ever missing at an edge.
    for (float mark = std::floor(begin / step) * step; mark <= end + step * 0.5f && used < 256; mark += step)
    {
        if (mark < begin - 1.0e-4f)
        {
            continue;
        }
        if (used == marks.size())
        {
            MarkView made;
            made.rule = add(marksLayer, "Line", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}}, "separator");
            scene().add<scene::UiImage>(made.rule, scene::UiImage{.raycastTarget = false});
            made.label = text(marksLayer, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}}, "", "dim", false, scene::TextAlign::Left,
                              std::round(font * 0.85f));
            marks.push_back(made);
        }
        const MarkView& shown = marks[used++];
        const float x = namesWidth() + (mark - begin) / (end - begin) * width;
        UiRect& rule = scene().get<UiRect>(shown.rule);
        rule.visible = true;
        rule.offsetMin = {x, 0.0f};
        rule.offsetMax = {x + 1.0f, 0.0f};
        UiRect& label = scene().get<UiRect>(shown.label);
        // A time that would be cut by the edge is left out.
        label.visible = x + font * 3.2f <= namesWidth() + width + rightPad();
        label.offsetMin = {x + 3.0f, 0.0f};
        label.offsetMax = {x + font * 5.0f, rulerHeight()};
        scene::UiText& written = scene().get<scene::UiText>(shown.label);
        if (std::string value = step < 0.1f ? std::format("{:.2f} s", mark) : std::format("{:.1f} s", mark); written.text != value)
        {
            written.text = std::move(value);
        }
    }
    for (std::size_t index = used; index < marks.size(); ++index)
    {
        scene().get<UiRect>(marks[index].rule).visible = false;
        scene().get<UiRect>(marks[index].label).visible = false;
    }
}

void AnimationUi::writeEvents(ToolsState& state)
{
    if (state.database == nullptr || !eventsClip.isValid())
    {
        return;
    }
    // In the order of their times, the one chosen staying chosen.
    const std::optional<asset::AnimationEvent> chosen = chosenEvent && *chosenEvent < events.size() ? std::optional(events[*chosenEvent]) : std::nullopt;
    std::ranges::stable_sort(events, {}, &asset::AnimationEvent::time);
    if (chosen)
    {
        const auto found = std::ranges::find(events, *chosen);
        chosenEvent = found != events.end() ? std::optional(static_cast<std::size_t>(found - events.begin())) : std::nullopt;
    }
    const core::Result<void> written = state.database->setSubAssetOption(
        eventsClip, "events", events.empty() ? std::nullopt : std::optional(asset::writeAnimationEvents(events)));
    if (!written)
    {
        DEVEX_LOG_ERROR("Cannot keep the events of the clip: {}", written.error());
    }
}

void AnimationUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    if (!built || builtFont != state.theme.fontSize)
    {
        setFont(state.theme.fontSize);
        build(kit);
    }
    styleTooltips(colors);
    ui::UiWorld& world = panel.world();
    const float zoom = UiPanel::zoomFor(font);

    const scene::Entity entity = animatorOf(edited, edited.findEntity(state.selection.active()));
    if (!entity.isValid())
    {
        say("Select an entity with an Animator to play its clips.", false);
        panel.update(kit, delta, zoom);
        return;
    }
    scene::Animator& animator = edited.get<scene::Animator>(entity);
    const bool playing = state.playState != PlayState::Editing;

    // The entity and its clip, which the list changes.
    scene().get<UiRect>(bar).visible = true;
    scene().get<scene::UiImage>(glyph).color = linearColor(colors.animation);
    fitText(kit, name, std::string(edited.name(entity)), true);
    fillClips(state, animator.clip);
    const auto answerClip = [&] {
        if (world.wasChanged(clipList))
        {
            const std::int32_t chosen = scene().get<scene::UiDropdown>(clipList).selected;
            if (chosen >= 0 && static_cast<std::size_t>(chosen) < clips.size() && clips[static_cast<std::size_t>(chosen)] != animator.clip)
            {
                animator.clip = clips[static_cast<std::size_t>(chosen)];
                state.animationPreviewTime = 0.0f;
            }
        }
    };

    const std::shared_ptr<const animation::Clip> clip = state.animationClips ? state.animationClips(animator.clip) : nullptr;
    if (clip == nullptr)
    {
        shownClip = nullptr;
        say(animator.clip.isValid() ? "The clip cannot be loaded." : "Choose an animation clip to preview it.", true);
        panel.update(kit, delta, zoom);
        answerClip();
        return;
    }
    scene().get<UiRect>(message).visible = false;
    scene().get<UiRect>(timeline).visible = true;
    for (const Entity control : {play.entity, stop.entity, loop, loopLabel, speed, time})
    {
        scene().get<UiRect>(control).visible = true;
    }
    const float duration = std::max(clip->duration(), 1e-3f);
    if (shownClip != clip.get())
    {
        // A clip imported again, as after its events changed, keeps the view and the event chosen.
        const bool sameClip = eventsClip == animator.clip;
        shownClip = clip.get();
        fillTracks(*clip);
        if (!draggingEvent)
        {
            events = clip->data().events;
        }
        if (!sameClip)
        {
            visibleBegin = 0.0f;
            visibleEnd = 0.0f;
            chosenEvent.reset();
            draggingEvent = false;
        }
        eventsClip = animator.clip;
    }
    if (chosenEvent && *chosenEvent >= events.size())
    {
        chosenEvent.reset();
    }
    // The events belong to the model the clip comes from; the game that plays only reads them.
    const bool editable = !playing && state.database != nullptr && state.database->sourceOf(animator.clip).has_value();

    // While the game plays, the panel follows the animation instead of driving it.
    if (playing && state.animationWorld != nullptr)
    {
        state.animationPreviewTime = state.animationWorld->time(entity);
        state.animationPreviewPlaying = state.animationWorld->isPlaying(entity);
    }
    // The preview advances outside Play; the game drives it during Play.
    if (!playing && state.animationPreviewPlaying)
    {
        state.animationPreviewTime += static_cast<float>(delta.count()) * animator.speed;
        if (state.animationPreviewTime > duration || state.animationPreviewTime < 0.0f)
        {
            state.animationPreviewTime = animator.loop ? state.animationPreviewTime - std::floor(state.animationPreviewTime / duration) * duration
                                                       : std::clamp(state.animationPreviewTime, 0.0f, duration);
            state.animationPreviewPlaying = animator.loop;
        }
    }

    if (shownPlaying != state.animationPreviewPlaying)
    {
        shownPlaying = state.animationPreviewPlaying;
        relabel(kit, play, *shownPlaying ? "Pause" : "Play");
        scene().get<scene::UiImage>(play.icon).texture = kit.icon(*shownPlaying ? Icon::Pause : Icon::Play);
    }
    scene().get<scene::UiToggle>(loop).value = animator.loop;
    setNumber(speed, animator.speed);
    if (std::string value = std::format("{} / {}", formatSeconds(state.animationPreviewTime), formatSeconds(clip->duration()));
        scene().get<scene::UiText>(time).text != value)
    {
        scene().get<scene::UiText>(time).text = std::move(value);
    }

    // What is in view, the marks of the ruler, the keys of the tracks between the same bounds, and
    // the head where the clip is.
    float begin = 0.0f;
    float end = duration;
    if (visibleEnd > visibleBegin)
    {
        const float span = std::min(visibleEnd - visibleBegin, duration);
        begin = std::clamp(visibleBegin, 0.0f, duration - span);
        end = begin + span;
    }
    const float areaWidth = std::max(panel.size().x - margin() * 2.0f - namesWidth() - rightPad(), 1.0f);
    layoutRuler(begin, end, areaWidth);
    for (const Entity keys : keyPlots)
    {
        scene::UiPlot& plot = scene().get<scene::UiPlot>(keys);
        plot.minValue = begin;
        plot.maxValue = end;
    }
    {
        std::vector<float> times;
        for (const asset::AnimationEvent& event : events)
        {
            times.push_back(event.time);
        }
        scene().get<scene::UiPlot>(eventPlot).values = std::move(times);
        scene().get<scene::UiPlot>(chosenPlot).values =
            chosenEvent ? std::vector<float>{events[*chosenEvent].time} : std::vector<float>{};
        scene().get<UiRect>(eventBox).visible = chosenEvent.has_value();
        scene().get<UiRect>(eventDelete.entity).visible = chosenEvent.has_value() && editable;
        if (chosenEvent)
        {
            setText(eventName, events[*chosenEvent].name);
        }
    }
    {
        UiRect& head = scene().get<UiRect>(playhead);
        const float at = (state.animationPreviewTime - begin) / (end - begin);
        head.visible = at >= 0.0f && at <= 1.0f;
        const float x = namesWidth() + at * areaWidth;
        head.offsetMin.x = x - 1.0f;
        head.offsetMax.x = x + 1.0f;
    }
    // The wheel scrolls the tracks; with Ctrl it zooms, and with Shift it slides along the clip.
    scene().get<scene::UiScroll>(tracks).speed = state.input.ctrl() || state.input.shift() ? 0.0f : rowHeight() * 3.0f;

    panel.update(kit, delta, zoom);
    answerClip();

    // The name of the event chosen, kept once the field lets go of it; Escape drops what was typed.
    if (world.editedField() == eventName)
    {
        namingEvent = true;
    }
    else if (std::exchange(namingEvent, false) && !panel.input().cancelPressed && chosenEvent && editable)
    {
        const std::string typed = scene().get<scene::UiText>(eventName).text;
        if (!typed.empty() && typed != events[*chosenEvent].name)
        {
            events[*chosenEvent].name = typed;
            writeEvents(state);
        }
    }
    const bool removing = chosenEvent && editable &&
                          (world.wasClicked(eventDelete.entity) ||
                           (panel.focused() && !world.isEditing() && state.input.pressed(platform::Key::Delete, false)));
    if (removing)
    {
        events.erase(events.begin() + static_cast<std::ptrdiff_t>(*chosenEvent));
        chosenEvent.reset();
        writeEvents(state);
    }

    if (world.wasClicked(play.entity))
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
    if (world.wasClicked(stop.entity))
    {
        state.animationPreviewPlaying = false;
        state.animationPreviewTime = 0.0f;
        if (playing && state.animationWorld != nullptr)
        {
            state.animationWorld->stop(entity);
        }
    }
    if (world.wasChanged(loop))
    {
        animator.loop = scene().get<scene::UiToggle>(loop).value;
    }
    if (world.wasChanged(speed))
    {
        animator.speed = scene().get<scene::UiNumberField>(speed).value;
    }

    // The timeline: where the pointer is over it, in seconds.
    const ui::LaidOutRect* const area = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(timeline);
    if (area != nullptr && areaWidth > 16.0f)
    {
        const ui::UiInput& input = panel.input();
        const float left = area->min.x + namesWidth();
        const bool inside = panel.hovered() && input.pointer.x >= left && input.pointer.x <= left + areaWidth && input.pointer.y >= area->min.y &&
                            input.pointer.y <= area->max.y;
        const float across = std::clamp((input.pointer.x - left) / areaWidth, 0.0f, 1.0f);
        float span = end - begin;
        if (inside && input.wheel != 0.0f && state.input.ctrl())
        {
            // Around the pointer, no closer than a few hundredths of a second across the width.
            const float at = begin + across * span;
            span = std::clamp(span * std::pow(0.8f, input.wheel), std::min(0.05f, duration), duration);
            begin = std::clamp(at - across * span, 0.0f, duration - span);
        }
        else if (inside && input.wheel != 0.0f && state.input.shift())
        {
            begin = std::clamp(begin - input.wheel * span * 0.1f, 0.0f, duration - span);
        }
        if (inside && state.input.down(Mouse::Middle))
        {
            begin = std::clamp(begin - state.input.mouseDelta().x * (panel.unitsOf(math::Vec2(1.0f, 0.0f)).x - panel.unitsOf(math::Vec2(0.0f, 0.0f)).x) / areaWidth * span, 0.0f,
                               duration - span);
        }
        // A double click shows the whole clip again.
        if (inside && state.input.doubleClicked(Mouse::Left))
        {
            begin = 0.0f;
            span = duration;
        }
        end = begin + span;
        const bool entire = begin <= 0.0f && end >= duration;
        visibleBegin = entire ? 0.0f : begin;
        visibleEnd = entire ? 0.0f : end;

        // The row of the events: a press on one chooses it and drags it, a press beside them chooses
        // none, and a right click adds one there.
        const ui::LaidOutRect* const row = world.canvases().front().layout.find(eventsRow);
        const bool onEvents = inside && row != nullptr && input.pointer.y >= row->min.y && input.pointer.y <= row->max.y;
        // To the millisecond, which keeps the times of the file short.
        const float pointed = std::clamp(std::round((begin + across * span) * 1000.0f) / 1000.0f, 0.0f, clip->duration());
        if (onEvents && input.pointerPressed)
        {
            std::optional<std::size_t> nearest;
            float closest = std::max(font * 0.5f, 5.0f) * span / areaWidth;
            for (std::size_t index = 0; index < events.size(); ++index)
            {
                if (const float away = std::abs(events[index].time - pointed); away <= closest)
                {
                    closest = away;
                    nearest = index;
                }
            }
            chosenEvent = nearest;
            draggingEvent = nearest.has_value() && editable;
        }
        if (onEvents && editable && state.input.clicked(Mouse::Right))
        {
            events.push_back({.time = pointed, .name = "event"});
            chosenEvent = events.size() - 1;
            writeEvents(state);
        }
        if (draggingEvent && chosenEvent)
        {
            // A click chooses the event; only a drag moves it.
            if (input.pointerDown && state.input.dragging(Mouse::Left, 3.0f))
            {
                events[*chosenEvent].time = pointed;
                movedEvent = true;
            }
            else if (!input.pointerDown)
            {
                draggingEvent = false;
                if (std::exchange(movedEvent, false))
                {
                    writeEvents(state);
                }
            }
        }

        // Pressing on the ruler or on the tracks, then dragging, moves along the clip.
        if (input.pointerPressed && inside && !onEvents)
        {
            scrubbing = true;
        }
        if (scrubbing && input.pointerDown)
        {
            state.animationPreviewTime = std::clamp(begin + across * span, 0.0f, duration);
            state.animationPreviewPlaying = false;
            if (playing && state.animationWorld != nullptr)
            {
                state.animationWorld->pause(entity);
                state.animationWorld->setTime(edited, entity, state.animationPreviewTime);
            }
        }
    }
    if (!panel.input().pointerDown)
    {
        scrubbing = false;
    }

    // Outside Play, the panel poses the skeleton itself, so the viewport shows the clip.
    if (!playing)
    {
        animation::applyClip(edited, entity, *clip, state.animationPreviewTime);
        state.previewedAnimation = animator.clip;
    }
}

void drawAnimationPanel(ToolsState& state, scene::Scene& scene)
{
    DEVEX_PROFILE_SCOPE("Animation panel");
    if (!state.showAnimation)
    {
        return;
    }
    if (std::exchange(state.focusAnimation, false))
    {
        focusPanel(state, animationWindow);
    }
    if (!beginDockedPanel(state, animationWindow))
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.animationUi)
    {
        state.animationUi = std::make_shared<AnimationUi>();
    }
    state.animationUi->update(state, kit, scene, core::Duration(state.input.delta()));
    endDockedPanel(state);
}

void renderAnimationPanel(ToolsState& state, render::RenderWorld& world)
{
    if (state.animationUi && state.uiKit)
    {
        state.animationUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
