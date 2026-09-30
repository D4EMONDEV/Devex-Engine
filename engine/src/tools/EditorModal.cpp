// The modal windows of the editor: the veil and the frame of the one on top, made with the interface of
// the engine, and the ImGui window that holds them while ImGui still shows the images.
#include "EditorModal.hpp"

#include "EditorFrame.hpp"
#include "EditorUi.hpp"
#include "SettingsUi.hpp"
#include "ToolsState.hpp"

#include <devex/core/Profiler.hpp>

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;

namespace {

constexpr std::uint32_t modalSurface = 23;

} // namespace

// Over the whole editor: the veil, and for a window of settings its frame, its title and its cross.
struct ModalLayerUi : PanelBuilder
{
    ModalLayerUi()
        : PanelBuilder(modalSurface)
    {
    }

    bool built = false;
    float builtFont = 0.0f;
    Entity veil;
    Entity frame;
    Entity title;
    Entity close;
    // The modal shown at the last frame, which gets the keyboard when it comes on top.
    std::string shown;

    void build(EditorUiKit& kit)
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
        panel.setKeyboardNavigation(false);
        veil = add({}, "Veil", whole());
        scene().add<scene::UiImage>(veil);
        frame = add({}, "Frame", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}}, "dialog");
        scene().add<scene::UiImage>(frame);
        title = text(frame, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}}, "", "text", true, scene::TextAlign::Center);
        close = add(frame, "Close", UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 0.0f}}, "bar_button");
        scene().add<scene::UiImage>(close);
        scene().add<scene::UiButton>(close);
        const float size = std::round(font * 1.35f);
        icon(kit, close, whole(math::Vec4{std::round(size * 0.2f)}), Icon::Close, "icon_dim");
        tooltip(close, "Close (Escape)");
    }

    void update(ToolsState& state, EditorUiKit& kit, std::string_view id, ImVec2 frameMin, ImVec2 frameSize, float titleHeight,
                std::string_view heading)
    {
        const ThemeColors& colors = themeColors();
        kit.refreshTheme(colors);
        if (!built || builtFont != state.theme.fontSize)
        {
            font = state.theme.fontSize;
            build(kit);
        }
        styleTooltips(colors);
        const ImGuiIO& io = ImGui::GetIO();
        const float pixelsPerPoint = io.DisplayFramebufferScale.x > 0.0f ? io.DisplayFramebufferScale.x : 1.0f;
        const float zoom = UiPanel::zoomFor(font);
        const float unitsPerPoint = pixelsPerPoint / zoom;
        const ImVec2 origin = ImGui::GetCursorScreenPos();

        scene().get<scene::UiImage>(veil).color = linearColor(ImVec4(0.0f, 0.0f, 0.0f, 0.45f));
        UiRect& framed = scene().get<UiRect>(frame);
        framed.visible = !heading.empty();
        framed.offsetMin = math::Vec2{std::round((frameMin.x - origin.x) * unitsPerPoint), std::round((frameMin.y - origin.y) * unitsPerPoint)};
        framed.offsetMax = framed.offsetMin + math::Vec2{std::round(frameSize.x * unitsPerPoint), std::round(frameSize.y * unitsPerPoint)};
        const float strip = std::round(titleHeight * unitsPerPoint);
        UiRect& titleRect = scene().get<UiRect>(title);
        titleRect.offsetMin = {0.0f, 0.0f};
        titleRect.offsetMax = {0.0f, strip};
        if (scene::UiText& said = scene().get<scene::UiText>(title); said.text != heading)
        {
            said.text = std::string(heading);
        }
        const float size = std::round(font * 1.35f);
        const float margin = std::round((strip - size) * 0.5f);
        UiRect& closeRect = scene().get<UiRect>(close);
        closeRect.offsetMin = {-margin - size - font * 0.2f, margin};
        closeRect.offsetMax = {-margin - font * 0.2f, margin + size};
        closeRect.style = barStyle(panel.world(), close, false);

        panel.update(kit, core::Duration(io.DeltaTime), zoom);

        // The cross, or Escape while nothing in the window is being typed into, closes a window of
        // settings; the dialogs answer Escape themselves.
        const bool escaped = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape, false) &&
                             !io.WantTextInput;
        if (!heading.empty() && (panel.world().wasClicked(close) || escaped))
        {
            state.modalToClose = std::string(id);
        }
    }
};

void openModal(ToolsState& state, std::string_view id)
{
    std::erase(state.modals, id);
    state.modals.emplace_back(id);
    if (std::ranges::find(state.modalsAsked, id) == state.modalsAsked.end())
    {
        state.modalsAsked.emplace_back(id);
    }
}

void closeModal(ToolsState& state, std::string_view id)
{
    std::erase(state.modals, id);
    if (state.modalLayerUi && state.modalLayerUi->shown == id)
    {
        state.modalLayerUi->shown.clear();
    }
}

bool isModalOpen(const ToolsState& state) noexcept
{
    return !state.modals.empty();
}

void pruneModals(ToolsState& state)
{
    std::vector<std::string> gone;
    for (const std::string& id : state.modals)
    {
        if (std::ranges::find(state.modalsAsked, id) == state.modalsAsked.end())
        {
            gone.push_back(id);
        }
    }
    for (const std::string& id : gone)
    {
        closeModal(state, id);
    }
    state.modalsAsked.clear();
}

bool beginModal(ToolsState& state, std::string_view id, ImVec2 size, std::string_view title)
{
    if (std::ranges::find(state.modalsAsked, id) == state.modalsAsked.end())
    {
        state.modalsAsked.emplace_back(id);
    }
    if (state.modals.empty() || state.modals.back() != id)
    {
        return false;
    }
    DEVEX_PROFILE_SCOPE("Modal");
    EditorUiKit& kit = editorUiKit(state);
    if (!state.modalLayerUi)
    {
        state.modalLayerUi = std::make_shared<ModalLayerUi>();
    }
    ModalLayerUi& layer = *state.modalLayerUi;

    // In the middle of the window, as large as asked and as it holds; a title makes a frame around.
    const ImGuiViewport* const viewport = ImGui::GetMainViewport();
    const float titleHeight = title.empty() ? 0.0f : std::round(ImGui::GetFrameHeight() + 10.0f);
    const float border = title.empty() ? 0.0f : 4.0f;
    const ImVec2 card(std::round(std::min(size.x, viewport->WorkSize.x * 0.95f - border * 2.0f)),
                      std::round(std::min(size.y, viewport->WorkSize.y * 0.95f - titleHeight - border)));
    const ImVec2 frameSize(card.x + border * 2.0f, card.y + titleHeight + border);
    const ImVec2 center = viewport->GetWorkCenter();
    const ImVec2 frameMin(std::round(center.x - frameSize.x * 0.5f), std::round(center.y - frameSize.y * 0.5f));

    const std::string window = "##modal " + std::string(id);
    if (layer.shown != id)
    {
        ImGui::SetNextWindowFocus();
        layer.shown = std::string(id);
    }
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin(window.c_str(), nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNav);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    layer.update(state, kit, id, frameMin, frameSize, titleHeight, title);
    ImGui::SetCursorScreenPos(ImVec2(frameMin.x + border, frameMin.y + titleHeight));
    ImGui::BeginChild("##card", card, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    ImGui::PopStyleVar(3);
    return true;
}

void endModal()
{
    ImGui::EndChild();
    ImGui::End();
}

bool takeModalClose(ToolsState& state, std::string_view id)
{
    if (state.modalToClose != id)
    {
        return false;
    }
    state.modalToClose.clear();
    return true;
}

void renderModalLayer(ToolsState& state, render::RenderWorld& world)
{
    if (state.modalLayerUi && state.uiKit)
    {
        state.modalLayerUi->panel.render(*state.uiKit, world, math::Vec4{0.0f});
    }
}

} // namespace devex::tools::detail
