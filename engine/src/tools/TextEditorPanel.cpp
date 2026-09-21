#include "ToolsState.hpp"

#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>

#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <utility>

namespace devex::tools::detail {

TextDocument* findTextDocument(ToolsState& state, const std::filesystem::path& path)
{
    for (TextDocument& document : state.textDocuments)
        if (sameTextPath(document.path, path))
            return &document;
    return nullptr;
}

std::vector<TextDocument*> affectedTextDocuments(ToolsState& state, const PendingAction& action)
{
    std::vector<TextDocument*> documents;
    if (action.kind == PendingAction::Kind::CloseTab)
        return documents;
    for (TextDocument& document : state.textDocuments)
    {
        const bool concerned = (action.kind != PendingAction::Kind::CloseText && action.kind != PendingAction::Kind::ReloadText) ||
                               sameTextPath(document.path, action.path);
        if (concerned && document.modified())
            documents.push_back(&document);
    }
    return documents;
}

void openTextFile(ToolsState& state, const std::filesystem::path& path)
{
    // The game overlay has no editor session or unsaved-document confirmation on exit.
    if (state.mode != ToolsMode::Editor)
    {
        openInCodeEditor(state, path);
        return;
    }
    state.showTextEditor = true;
    state.focusTextEditor = true;
    state.textOpenError.clear();
    TextDocument* document = findTextDocument(state, path);
    if (document == nullptr)
    {
        auto opened = TextDocument::open(path);
        if (!opened)
        {
            state.textOpenError = opened.error().message;
            DEVEX_LOG_WARNING("Cannot open text file: {}", opened.error());
            return;
        }
        document = &state.textDocuments.emplace_back(std::move(*opened));
    }
    state.activeText = document->path;
    state.selectTextTab = true;
}

void showOpenTextDialog(ToolsState& state)
{
    const std::weak_ptr<DialogAnswers> answers = state.dialogAnswers;
    state.platform.showFileDialog(state.window,
        {.type = platform::FileDialogType::OpenFile,
         .filters = {{"Text and code", "cpp;h;hpp;c;cs;csproj;txt;md;json;glsl;vert;frag;cmake;dvxscene;dvxmat;dvxproj;dvxmeta;dvxprefab"},
                     {"All files", "*"}},
         .defaultLocation = state.database != nullptr ? state.database->project().root : std::filesystem::path{}},
        [answers](std::optional<std::filesystem::path> chosen) {
            if (const auto inbox = answers.lock(); inbox && chosen)
                inbox->openText = std::move(*chosen);
        });
}

bool textEditorFocused()
{
    const ImGuiWindow* window = ImGui::FindWindowByName(textEditorWindow);
    const ImGuiWindow* focused = GImGui->NavWindow;
    return window != nullptr && focused != nullptr && focused->RootWindow == window->RootWindow;
}

namespace {

int trackCursor(ImGuiInputTextCallbackData* data)
{
    auto& document = *static_cast<TextDocument*>(data->UserData);
    document.line = 1;
    document.column = 1;
    for (int i = 0; i < data->CursorPos; ++i)
    {
        if (data->Buf[i] == '\n')
        {
            ++document.line;
            document.column = 1;
        }
        else if ((static_cast<unsigned char>(data->Buf[i]) & 0xC0) != 0x80)
            ++document.column;
    }
    return 0;
}

} // namespace

void drawTextEditorPanel(ToolsState& state, scene::Scene& scene)
{
    if (auto path = std::exchange(state.dialogAnswers->openText, std::nullopt))
        openTextFile(state, *path);
    if (!state.showTextEditor)
        return;
    ImGui::SetNextWindowSize(ImVec2(900.0f, 620.0f), ImGuiCond_FirstUseEver);
    if (std::exchange(state.focusTextEditor, false))
        ImGui::SetNextWindowFocus();
    if (!ImGui::Begin(textEditorWindow, &state.showTextEditor))
    {
        ImGui::End();
        return;
    }

    std::optional<PendingAction> action;
    if (labelButton(icons::FolderOpen, "Open..."))
        showOpenTextDialog(state);
    ImGui::SameLine();
    if (labelButton(icons::Save, "Save All"))
        for (TextDocument& document : state.textDocuments)
            if (document.modified())
                static_cast<void>(saveTextFile(state, scene, document));
    if (!state.textOpenError.empty())
        ImGui::TextWrapped("%s", state.textOpenError.c_str());

    if (state.textDocuments.empty())
    {
        ImGui::Spacing();
        ImGui::TextWrapped("Open a code file from FileSystem, or use Edit as Text on a Devex asset.");
        ImGui::TextDisabled("Drag this panel's title to dock or detach it.");
    }
    if (ImGui::BeginTabBar("Text files", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs |
                                         ImGuiTabBarFlags_FittingPolicyScroll | ImGuiTabBarFlags_TabListPopupButton))
    {
        const std::filesystem::path requestedTab = state.selectTextTab ? state.activeText : std::filesystem::path{};
        for (TextDocument& document : state.textDocuments)
        {
            const std::string path = core::toUtf8(document.path);
            const std::string label = core::toUtf8(document.path.filename()) + "###" + path;
            ImGuiTabItemFlags flags = document.modified() ? ImGuiTabItemFlags_UnsavedDocument : 0;
            if (document.path == requestedTab)
            {
                flags |= ImGuiTabItemFlags_SetSelected;
                state.selectTextTab = false;
            }
            bool open = true;
            const bool selected = ImGui::BeginTabItem(label.c_str(), &open, flags);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", path.c_str());
            if (!open)
                action = PendingAction{.kind = PendingAction::Kind::CloseText, .path = document.path};
            if (!selected)
                continue;
            state.activeText = document.path;
            ImGui::PushID(path.c_str());
            ImGui::BeginDisabled(!document.modified());
            if (labelButton(icons::Save, "Save"))
                static_cast<void>(saveTextFile(state, scene, document));
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (labelButton(icons::Refresh, "Reload"))
                action = PendingAction{.kind = PendingAction::Kind::ReloadText, .path = document.path};
            ImGui::SameLine();
            if (labelButton(icons::ExternalLink, "External Editor"))
                openInCodeEditor(state, document.path);
            ImGui::TextDisabled("%s", path.c_str());
            if (!document.error.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, uiColor(themeColors().error));
                ImGui::TextWrapped("%s", document.error.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::PushFont(editorFonts().mono, 0.0f);
            ImGui::PushID(static_cast<int>(document.revision));
            const float height = std::max(ImGui::GetTextLineHeight() * 3.0f,
                                          ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing());
            ImGui::InputTextMultiline("##content", &document.text, ImVec2(-1.0f, height),
                                     ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackAlways,
                                     trackCursor, &document);
            ImGui::PopID();
            ImGui::PopFont();
            ImGui::TextDisabled("Ln %d, Col %d  |  UTF-8%s  |  %s  |  Ctrl+S Save   Ctrl+Z Undo   Ctrl+Y Redo",
                                document.line, document.column, document.bom() ? " BOM" : "",
                                document.crlf() ? "CRLF" : "LF");
            ImGui::PopID();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    // Closing/reloading may invalidate a document: do this after all widgets have used it.
    if (action)
        requestAction(state, scene, std::move(*action));
}

} // namespace devex::tools::detail
