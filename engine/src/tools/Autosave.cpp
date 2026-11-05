// The copies of what the editor has not saved, which come back after a crash. They live in the cache of the project
// and never touch its files, which only saving writes. Leaving normally removes them: finding some on opening a
// project says that the editor did not get to.

#include "ToolsState.hpp"

#include <devex/asset/Artifact.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/TerrainMaps.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace devex::tools::detail {

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t indexFormat = 1;
constexpr std::string_view indexName = "index.dvx";
// How often what was saved or undone loses its copy.
constexpr std::chrono::seconds sweepInterval{1};

[[nodiscard]] fs::path autosaveFolder(const asset::Project& project)
{
    return project.cacheDirectory() / "autosave";
}

// Set aside rather than deleted, until the next time: a copy that was refused can still be needed.
[[nodiscard]] fs::path setAsideFolder(const asset::Project& project)
{
    return project.cacheDirectory() / "backups" / "discarded-autosave";
}

[[nodiscard]] std::string_view kindName(AutosaveKind kind) noexcept
{
    switch (kind)
    {
    case AutosaveKind::Scene:
        return "scene";
    case AutosaveKind::Text:
        return "text";
    case AutosaveKind::Terrain:
        return "terrain";
    }
    return "scene";
}

[[nodiscard]] std::optional<AutosaveKind> kindOf(std::string_view name) noexcept
{
    for (const AutosaveKind kind : {AutosaveKind::Scene, AutosaveKind::Text, AutosaveKind::Terrain})
    {
        if (kindName(kind) == name)
        {
            return kind;
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::byte> bytesOf(std::string_view text)
{
    const auto bytes = std::as_bytes(std::span(text));
    return {bytes.begin(), bytes.end()};
}

void writeIndex(const asset::Project& project, const std::vector<AutosaveEntry>& entries)
{
    const fs::path folder = autosaveFolder(project);
    if (entries.empty())
    {
        std::error_code error;
        fs::remove_all(folder, error);
        return;
    }
    serialization::TextDocument document;
    serialization::TextSection& header = document.sections.emplace_back();
    header.type = "autosave";
    header.attributes.push_back({"format", serialization::TextValue(indexFormat)});
    for (const AutosaveEntry& entry : entries)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "copy";
        section.attributes.push_back({"kind", serialization::TextValue(std::string(kindName(entry.kind)))});
        section.attributes.push_back({"file", serialization::TextValue(core::toUtf8(entry.file))});
        section.attributes.push_back({"original", serialization::TextValue(core::toUtf8(entry.original))});
    }
    // The copies are written before the index that names them, which never names what is not there.
    if (core::Result<void> written = core::writeTextFile(folder / indexName, serialization::writeText(document)); !written)
    {
        DEVEX_LOG_WARNING("Cannot keep the copies of unsaved work: {}", written.error());
    }
}

// One pass over what is open. With copy, what changed is copied; every time, what no longer differs from its file loses
// its copy.
void syncCopies(ToolsState& state, scene::Scene& scene, bool copy)
{
    const asset::Project& project = state.database->project();
    const fs::path folder = autosaveFolder(project);
    AutosaveState& autosave = state.autosave;
    std::vector<AutosaveEntry> kept;
    bool changed = false;

    const auto visit = [&](AutosaveKind kind, std::uint64_t tab, const fs::path& original, bool modified, std::uint64_t stamp,
                           const std::function<std::vector<std::byte>()>& contents) {
        const auto found = std::ranges::find_if(autosave.entries, [&](const AutosaveEntry& entry) {
            return entry.kind == kind && (kind == AutosaveKind::Scene ? entry.tab == tab : entry.original == original);
        });
        const AutosaveEntry* const existing = found != autosave.entries.end() ? &*found : nullptr;
        if (!modified)
        {
            return;
        }
        if (existing != nullptr && (!copy || existing->stamp == stamp))
        {
            kept.push_back(*existing);
            changed = changed || kept.back().original != original;
            kept.back().original = original;
            return;
        }
        if (!copy)
        {
            return;
        }
        const std::string extension = kind == AutosaveKind::Text ? std::string(".txt") : core::toUtf8(original.extension());
        AutosaveEntry entry = existing != nullptr
                                  ? *existing
                                  : AutosaveEntry{.kind = kind,
                                                  .tab = tab,
                                                  .original = original,
                                                  .file = core::Uuid::generate().toString() + (extension.empty() ? std::string(".dvxscene") : extension),
                                                  .stamp = stamp};
        entry.original = original;
        entry.stamp = stamp;
        const std::vector<std::byte> bytes = contents();
        if (core::Result<void> written = core::writeFileAtomically(folder / entry.file, bytes); written)
        {
            kept.push_back(std::move(entry));
            changed = true;
        }
        else
        {
            DEVEX_LOG_WARNING("Cannot keep a copy of {}: {}", core::toUtf8(original.filename()), written.error());
            if (existing != nullptr)
            {
                kept.push_back(*existing);
            }
        }
    };

    const ActiveDocument live = activeDocument(state, scene);
    for (std::size_t index = 0; index < state.tabs.size(); ++index)
    {
        const bool active = state.tabs.active() == index;
        const scene::Scene& document = active ? scene : state.tabs.background(index).scene;
        const std::uint64_t stamp = active ? state.history.stateId() : state.tabs.background(index).history.stateId();
        visit(AutosaveKind::Scene, state.tabs.id(index), state.tabs.path(index, live), state.tabs.isModified(index, live), stamp,
              [&document] { return bytesOf(scene::saveScene(document)); });
    }
    for (const TextDocument& document : state.textDocuments)
    {
        visit(AutosaveKind::Text, 0, document.path, document.modified(), std::hash<std::string>{}(document.text), [&document] { return bytesOf(document.text); });
    }
    if (state.terrains && state.terrains->maps && state.terrains->edited)
    {
        // Nothing tells that a terrain changed since its last copy: each pass copies it again.
        const std::uint64_t pass = ++autosave.pass;
        const auto visitTerrains = [&](const scene::Scene& document) {
            for ([[maybe_unused]] auto [entity, terrain] : document.view<scene::Terrain>())
            {
                const std::optional<asset::SourceFile> source = terrain.data.isValid() ? state.database->sourceOf(terrain.data) : std::nullopt;
                const std::optional<fs::path> file = source ? project.absolutePath(source->path) : std::nullopt;
                if (file)
                {
                    const asset::AssetId data = terrain.data;
                    visit(AutosaveKind::Terrain, 0, *file, state.terrains->edited(data), pass, [&state, data] {
                        const asset::TerrainMaps* const maps = state.terrains->maps(data);
                        return maps != nullptr ? asset::encodeTerrain(*maps) : std::vector<std::byte>{};
                    });
                }
            }
        };
        visitTerrains(scene);
        state.tabs.forEachBackgroundScene(visitTerrains);
    }

    for (const AutosaveEntry& entry : autosave.entries)
    {
        if (std::ranges::none_of(kept, [&](const AutosaveEntry& other) { return other.file == entry.file; }))
        {
            std::error_code error;
            fs::remove(folder / entry.file, error);
            changed = true;
        }
    }
    autosave.entries = std::move(kept);
    if (changed)
    {
        writeIndex(project, autosave.entries);
    }
}

} // namespace

void updateAutosave(ToolsState& state, scene::Scene& scene)
{
    AutosaveState& autosave = state.autosave;
    if (state.mode != ToolsMode::Editor || state.database == nullptr || state.playState != PlayState::Editing || state.scenesWaitForCode ||
        !autosave.recoverable.empty() || state.tabs.empty())
    {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (autosave.copied == std::chrono::steady_clock::time_point{})
    {
        autosave.copied = now;
    }
    // The game is about to run, whose code can bring everything down.
    const int minutes = state.theme.autosaveMinutes;
    const bool due = minutes > 0 && (state.requests.play || now - autosave.copied >= std::chrono::minutes(minutes));
    if (due)
    {
        autosave.copied = now;
        syncCopies(state, scene, true);
    }
    else if (!autosave.entries.empty() && now - autosave.swept >= sweepInterval)
    {
        autosave.swept = now;
        syncCopies(state, scene, false);
    }
}

void writeAutosaveCopies(ToolsState& state, scene::Scene& scene)
{
    state.autosave.copied = std::chrono::steady_clock::now();
    syncCopies(state, scene, true);
}

void removeAutosave(const asset::Project& project)
{
    std::error_code error;
    fs::remove_all(autosaveFolder(project), error);
}

void setAsideAutosave(const asset::Project& project)
{
    const fs::path folder = autosaveFolder(project);
    const fs::path aside = setAsideFolder(project);
    std::error_code error;
    fs::remove_all(aside, error);
    fs::create_directories(aside.parent_path(), error);
    fs::rename(folder, aside, error);
    if (error)
    {
        fs::remove_all(folder, error);
    }
}

std::vector<AutosaveCopy> findAutosaveCopies(const asset::Project& project)
{
    std::vector<AutosaveCopy> copies;
    const fs::path folder = autosaveFolder(project);
    const core::Result<std::string> text = core::readTextFile(folder / indexName);
    if (!text)
    {
        return copies;
    }
    const core::Result<serialization::TextDocument> document = serialization::parseText(*text);
    if (!document)
    {
        DEVEX_LOG_WARNING("Cannot read the copies of unsaved work: {}", document.error());
        return copies;
    }
    for (const serialization::TextSection& section : document->sections)
    {
        const std::string* const kindText = serialization::stringAttribute(section, "kind");
        const std::string* const file = serialization::stringAttribute(section, "file");
        const std::string* const original = serialization::stringAttribute(section, "original");
        const std::optional<AutosaveKind> kind = kindText != nullptr ? kindOf(*kindText) : std::nullopt;
        if (section.type != "copy" || !kind || file == nullptr || original == nullptr)
        {
            continue;
        }
        const fs::path copy = folder / core::pathFromUtf8(*file).filename();
        const fs::path source = core::pathFromUtf8(*original);
        const core::Result<std::string> saved = core::readTextFile(copy);
        if (!saved)
        {
            continue;
        }
        // What was saved in the end has nothing left to give.
        if (!source.empty())
        {
            if (const core::Result<std::string> current = core::readTextFile(source); current && *current == *saved)
            {
                continue;
            }
        }
        copies.push_back({.kind = *kind, .copy = copy, .original = source});
    }
    return copies;
}

void recoverAutosave(ToolsState& state, scene::Scene& scene)
{
    const std::vector<AutosaveCopy> copies = std::exchange(state.autosave.recoverable, {});
    bool terrainWritten = false;
    // The terrains first: the scenes that open hold them.
    for (const AutosaveCopy& copy : copies)
    {
        if (copy.kind != AutosaveKind::Terrain)
        {
            continue;
        }
        std::error_code error;
        fs::copy_file(copy.copy, copy.original, fs::copy_options::overwrite_existing, error);
        if (error)
        {
            DEVEX_LOG_ERROR("Cannot recover {}: {}", core::toUtf8(copy.original.filename()), error.message());
        }
        terrainWritten = terrainWritten || !error;
    }
    if (terrainWritten)
    {
        state.database->refresh();
    }
    for (const AutosaveCopy& copy : copies)
    {
        if (copy.kind == AutosaveKind::Scene)
        {
            core::Result<scene::Scene> loaded = scene::loadSceneFile(copy.copy);
            if (!loaded)
            {
                DEVEX_LOG_ERROR("Cannot recover {}: {}", core::toUtf8(copy.original.filename()), loaded.error());
                continue;
            }
            openRecoveredScene(state, scene, copy.original, std::move(*loaded));
        }
        else if (copy.kind == AutosaveKind::Text)
        {
            core::Result<std::string> text = core::readTextFile(copy.copy);
            openTextFile(state, copy.original);
            const auto document = std::ranges::find_if(state.textDocuments, [&](const TextDocument& open) { return sameTextPath(open.path, copy.original); });
            if (!text || document == state.textDocuments.end())
            {
                DEVEX_LOG_ERROR("Cannot recover {}", core::toUtf8(copy.original.filename()));
                continue;
            }
            document->text = std::move(*text);
            ++document->revision;
        }
    }
    DEVEX_LOG_INFO("Recovered {} unsaved file{}", copies.size(), copies.size() == 1 ? "" : "s");
    // What was recovered is unsaved work like any other, and the copies it came from stay for a while.
    const asset::Project& project = state.database->project();
    setAsideAutosave(project);
    state.autosave.entries.clear();
    writeAutosaveCopies(state, scene);
}

void discardAutosave(ToolsState& state)
{
    state.autosave.recoverable.clear();
    setAsideAutosave(state.database->project());
    DEVEX_LOG_INFO("Set the unsaved work aside in {}", core::toUtf8(setAsideFolder(state.database->project())));
}

} // namespace devex::tools::detail
