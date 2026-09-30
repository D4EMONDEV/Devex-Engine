#include "SettingsUi.hpp"
#include "EditorModal.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

// The code of the game in FileSystem, the components created from the inspector, and the window that
// tells how to attach a debugger to the C# code.
namespace devex::tools::detail {

using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

EntityIcon codeIcon(const std::filesystem::path& path)
{
    const ThemeColors& colors = themeColors();
    if (path.extension() == ".cs")
    {
        return {icons::FileCode, colors.gameCode};
    }
    if (path.extension() == ".cpp" || path.extension() == ".hpp" || path.extension() == ".h")
    {
        return {icons::Code, colors.entity};
    }
    return {icons::FileText, colors.neutral};
}

void openInCodeEditor(ToolsState& state, const std::filesystem::path& file)
{
    if (core::Result<void> opened = state.platform.openPath(file); !opened)
    {
        DEVEX_LOG_WARNING("Cannot open {}: {}", core::toUtf8(file.filename()), opened.error());
    }
}

// The C# Debugging window: whether a debugger is attached, the process to attach it to and how, and
// whether Play waits for one.
struct DebuggingUi : FormUi
{
    DebuggingUi()
        : FormUi(debuggingSurface)
    {
    }

    bool built = false;
    float builtFont = 0.0f;
    Entity statusIcon;
    Entity statusText;
    Entity process;
    Button copy;
    Entity wait;
    Button stopWaiting;

    void build(EditorUiKit& kit);
    void update(ToolsState& state, EditorUiKit& kit, core::Duration delta);
};

void DebuggingUi::build(EditorUiKit& kit)
{
    built = true;
    builtFont = font;
    buildForm(add({}, "Debugging", whole()), whole(math::Vec4{font * 0.3f, font * 0.3f, 0.0f, 0.0f}));
    panel.setKeyboardNavigation(false);

    Section& status = card(kit, "Status");
    const float iconSize = std::round(font * 1.2f);
    const Entity statusRow = add(status.card, "Status", wide(line));
    statusIcon = icon(kit, statusRow,
                      UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 0.35f, -iconSize * 0.5f},
                             .offsetMax = {font * 0.35f + iconSize, iconSize * 0.5f}},
                      Icon::Bug, {});
    statusText = text(statusRow, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.85f + iconSize, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                      "", "text");
    status.lines.push_back(Line{.entity = statusRow});
    const Entity waitRow = actions(&status);
    const float box = std::round(font * 1.3f);
    wait = add(waitRow, "Wait", middle({box, box}), "toggle");
    scene().add<scene::UiImage>(wait);
    scene().add<scene::UiToggle>(wait);
    scene().add<scene::UiButton>(wait);
    const std::string waitLabel = "Wait for a debugger when Play starts";
    text(waitRow, middle({kit.textWidth(EditorUiKit::regularFont(), waitLabel, font) + 4.0f, line}), waitLabel, "text");
    stopWaiting = action(kit, waitRow, Icon::Close, "Stop Waiting");

    Section& attach = card(kit, "Attach a Debugger");
    const Entity processRow = actions(&attach);
    process = text(processRow, middle({font * 16.0f, line}), "", "text", true);
    copy = action(kit, processRow, Icon::Copy, "Copy Process Id");
    note(&attach, "Attach the debugger of your code editor to this process, for .NET code:", "dim");
    note(&attach, "•  Visual Studio: Debug > Attach to Process (Ctrl+Alt+P), devex-editor.exe, code type Managed (.NET Core, .NET 5+).", "text", 2.0f);
    note(&attach, "•  Rider: Run > Attach to Process, devex-editor.", "text");
    note(&attach, "•  VS Code with C# Dev Kit: .NET: Attach to a .NET 5+ or .NET Core process.", "text", 2.0f);
    note(&attach, "Breakpoints follow the code as the editor builds and reloads it.", "dim");
}

void DebuggingUi::update(ToolsState& state, EditorUiKit& kit, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    setFont(state.theme.fontSize);
    if (!built || builtFont != font)
    {
        if (built)
        {
            clearForm();
            std::vector<Entity> children;
            for (Entity child = scene().firstChild(panel.canvas()); child.isValid(); child = scene().nextSibling(child))
            {
                children.push_back(child);
            }
            for (const Entity child : children)
            {
                scene().destroyEntity(child);
            }
        }
        build(kit);
    }
    styleTooltips(colors);

    const DebuggerStatus& debugger = state.debugger;
    const auto [glyph, color, message] =
        !debugger.available  ? std::tuple{Icon::Info, colors.warning, "The project has no C# code loaded."}
        : debugger.attached ? std::tuple{Icon::CircleCheck, colors.success, "A debugger is attached: breakpoints in the C# code stop the game."}
        : debugger.waiting  ? std::tuple{Icon::Loader, colors.accent, "Play starts as soon as a debugger attaches."}
                            : std::tuple{Icon::Bug, colors.warning, "No debugger is attached."};
    scene().get<scene::UiImage>(statusIcon).texture = kit.icon(glyph);
    scene().get<scene::UiImage>(statusIcon).color = linearColor(color);
    scene().get<scene::UiText>(statusText).text = message;
    scene().get<scene::UiText>(process).text = std::format("devex-editor, process {}", debugger.processId);
    scene().get<scene::UiToggle>(wait).value = state.waitForDebugger;
    scene().get<UiRect>(stopWaiting.entity).visible = debugger.waiting;
    layoutCards();
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();

    const ui::UiWorld& world = panel.world();
    if (world.wasClicked(copy.entity))
    {
        state.platform.setClipboardText(std::to_string(debugger.processId).c_str());
    }
    if (world.wasChanged(wait))
    {
        state.waitForDebugger = scene().get<scene::UiToggle>(wait).value;
    }
    if (world.wasClicked(stopWaiting.entity))
    {
        state.requests.stop = true;
    }
}

void drawDebuggingWindow(ToolsState& state)
{
    if (!state.showDebugging)
    {
        return;
    }
    EditorUiKit& kit = editorUiKit(state);
    if (!state.debuggingUi)
    {
        state.debuggingUi = std::make_shared<DebuggingUi>();
    }
    if (beginFormWindow(state, debuggingWindow, &state.showDebugging, 36.0f, 23.0f))
    {
        state.debuggingUi->update(state, kit, core::Duration(state.input.delta()));
        endFormWindow(state);
    }
}

void renderDebuggingWindow(ToolsState& state, render::RenderWorld& world)
{
    if (state.debuggingUi && state.uiKit)
    {
        state.debuggingUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

void updatePendingScript(ToolsState& state, scene::Scene& scene)
{
    if (state.pendingScript.empty())
    {
        return;
    }
    if (!scene.findEntity(state.pendingScriptEntity).isValid())
    {
        state.pendingScript.clear();
        return;
    }
    // The component appears once its file is compiled and its code loaded.
    if (scene::componentRegistry().find(state.pendingScript) != nullptr)
    {
        state.pendingCommand = makeAddComponentCommand(state.pendingScriptEntity, std::exchange(state.pendingScript, {}));
    }
}

} // namespace devex::tools::detail
