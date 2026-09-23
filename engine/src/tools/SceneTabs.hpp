#pragma once

#include "EditorCamera.hpp"
#include "Selection.hpp"

#include <devex/core/Uuid.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/tools/CommandHistory.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <unordered_set>
#include <vector>

namespace devex::tools::detail {

// What the editor keeps of a scene it edits: the scene, its file, its undo history and the view on it.
struct SceneDocument
{
    // Empty until the scene is saved.
    std::filesystem::path path;
    scene::Scene scene;
    CommandHistory history;
    // The history state when the scene was last saved or opened.
    std::uint64_t savedState = 0;
    Selection selection;
    EditorCamera camera;
    // The entities hidden in the viewport.
    std::unordered_set<core::Uuid> hidden;
};

// The document on screen, whose parts live where the rest of the editor uses them: the scene of the
// application, the history of the tools, and so on.
struct ActiveDocument
{
    std::filesystem::path& path;
    scene::Scene& scene;
    CommandHistory& history;
    std::uint64_t& savedState;
    Selection& selection;
    EditorCamera& camera;
    std::unordered_set<core::Uuid>& hidden;
};

// The scenes open in the editor, one per tab. The active tab's document lives in the editor itself,
// the others wait in their tab: switching tabs swaps them.
class SceneTabs
{
public:
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::optional<std::size_t> active() const noexcept;
    // A stable identifier of the tab at an index.
    [[nodiscard]] std::uint64_t id(std::size_t index) const noexcept;
    [[nodiscard]] std::optional<std::size_t> findById(std::uint64_t id) const noexcept;
    [[nodiscard]] std::optional<std::size_t> findByPath(const std::filesystem::path& path,
                                                        const ActiveDocument& live) const;

    [[nodiscard]] const std::filesystem::path& path(std::size_t index, const ActiveDocument& live) const noexcept;
    [[nodiscard]] bool isModified(std::size_t index, const ActiveDocument& live) const noexcept;
    // The document of a background tab; the active tab's is the live one.
    [[nodiscard]] SceneDocument& background(std::size_t index) noexcept;

    // Adds a tab after the others, in the background, and returns its index.
    std::size_t add(SceneDocument document);
    // Brings a tab to the screen, sending the active one to the background.
    void activate(std::size_t index, const ActiveDocument& live);
    // Closes a tab and drops its document. When it was active, the tab after it (or before) comes
    // to the screen, or the live document is emptied when no tab is left.
    void close(std::size_t index, const ActiveDocument& live);
    // Moves a tab to another index, keeping the active tab active.
    void move(std::size_t from, std::size_t to);
    // Drops every tab and empties the live document.
    void clear(const ActiveDocument& live);

    // Calls the function with the scene of every background tab.
    void forEachBackgroundScene(const std::function<void(scene::Scene&)>& function);

private:
    struct Tab
    {
        std::uint64_t id = 0;
        // A placeholder while the tab is active.
        SceneDocument document;
    };

    static void swapDocument(SceneDocument& stored, const ActiveDocument& live);

    std::vector<Tab> m_tabs;
    std::optional<std::size_t> m_active;
    std::uint64_t m_nextId = 1;
};

} // namespace devex::tools::detail
