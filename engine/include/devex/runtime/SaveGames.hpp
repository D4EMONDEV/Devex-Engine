#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/reflection/Reflection.hpp>

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::scene {
class Scene;
} // namespace devex::scene

namespace devex::runtime {

class AssetManager;

struct SaveOptions
{
    // Shown in the list of saves: "Chapter 2", the name of a place.
    std::string label;
    // Keeps the scene as it plays: its entities, where they are and what their components hold.
    // Loading the save brings it back.
    bool scene = true;
    // Keeps a small picture of the next frame beside the save.
    bool thumbnail = true;
    // The version of what the game saves, for it to convert older saves once it changes.
    std::uint32_t version = 0;
};

// A save as a list of saves shows it.
struct SaveSlot
{
    std::string name;
    std::string label;
    // When it was written, in seconds since 1970 (UTC).
    std::int64_t time = 0;
    // Seconds played until then.
    double playTime = 0.0;
    asset::AssetId scene;
    std::string sceneName;
    std::uint32_t version = 0;
    bool hasScene = false;
    // The picture of the save, or nothing while there is none.
    std::optional<std::filesystem::path> thumbnail;
    // The type of the data, as its reflection names it.
    std::string type;
};

// The saves of a game: objects of a reflected type, described like components, written by their
// fields into a text file of the folder of the user, with the scene that plays and a picture of it.
// A slot has a name ("1", "auto", "quick"); saving into it again keeps the previous save beside it,
// which loading falls back on when the file is damaged. Fields the data no longer has are skipped
// and new ones keep their defaults, so that older saves still load; the version tells the game
// what else changed.
class SaveGames
{
public:
    // Empty for a game whose saves cannot be kept, which then fail.
    explicit SaveGames(std::filesystem::path directory);

    [[nodiscard]] const std::filesystem::path& directory() const noexcept;

    // What the saves take from the running game: the scene that plays and where it came from. The
    // scene must outlive its use here.
    void setScene(const scene::Scene* scene, asset::AssetId asset, std::string name);
    // The pictures of saves become textures there.
    void setAssets(AssetManager* assets) noexcept;
    // Seconds played, which a loaded save carries on from.
    void addPlayTime(double seconds) noexcept;
    [[nodiscard]] double playTime() const noexcept;

    [[nodiscard]] core::Result<void> save(std::string_view slot, const reflection::TypeInfo& type, const void* data,
                                          const SaveOptions& options = {});
    template <typename T>
    [[nodiscard]] core::Result<void> save(std::string_view slot, const T& data, const SaveOptions& options = {})
    {
        return save(slot, reflection::typeInfo<T>(), &data, options);
    }

    // Reads the data of the save into the object, whose fields keep their value where the save has
    // none. With restoreScene, the scene of the save replaces the one that plays at the end of the
    // frame, and the time played carries on from the save. A missing slot is NotFound.
    [[nodiscard]] core::Result<SaveSlot> load(std::string_view slot, const reflection::TypeInfo& type, void* data,
                                              bool restoreScene = true);
    template <typename T>
    [[nodiscard]] core::Result<T> load(std::string_view slot, bool restoreScene = true)
    {
        T data{};
        core::Result<SaveSlot> loaded = load(slot, reflection::typeInfo<T>(), &data, restoreScene);
        if (!loaded)
        {
            return std::unexpected(loaded.error());
        }
        return data;
    }

    // Every save, the most recent first.
    [[nodiscard]] std::vector<SaveSlot> slots() const;
    [[nodiscard]] std::optional<SaveSlot> find(std::string_view slot) const;
    [[nodiscard]] bool exists(std::string_view slot) const;
    // Removes the save, its picture and the previous save it kept.
    [[nodiscard]] core::Result<void> remove(std::string_view slot);
    // The picture of the save as a texture an interface shows (UiImage), invalid while there is
    // none. It is made again when the save changes.
    [[nodiscard]] asset::AssetId thumbnail(std::string_view slot);

    // The slot the scene that plays was restored from, while its Start systems run; empty
    // otherwise.
    [[nodiscard]] const std::string& restoredSlot() const noexcept;

    // For the application. A scene to put in place of the one that plays, with the slot it comes
    // from.
    struct Restore
    {
        std::string slot;
        std::string sceneText;
        asset::AssetId scene;
    };
    [[nodiscard]] std::optional<Restore> takeRestore();
    void setRestoredSlot(std::string slot);
    // The files the pictures of the saves of this frame go to, once the frame is drawn.
    [[nodiscard]] std::vector<std::filesystem::path> takeThumbnailRequests();

    // Slot names are letters, digits, spaces, dashes, dots and underscores, 64 at most.
    [[nodiscard]] static bool isValidSlot(std::string_view slot) noexcept;

private:
    [[nodiscard]] std::filesystem::path fileOf(std::string_view slot) const;

    std::filesystem::path m_directory;
    const scene::Scene* m_scene = nullptr;
    asset::AssetId m_sceneAsset;
    std::string m_sceneName;
    AssetManager* m_assets = nullptr;
    double m_playTime = 0.0;
    std::optional<Restore> m_restore;
    std::string m_restoredSlot;
    std::vector<std::filesystem::path> m_thumbnailRequests;
    // The textures made of the pictures, with the time of the file they were made from.
    struct Thumbnail
    {
        asset::AssetId texture;
        std::filesystem::file_time_type written;
    };
    std::map<std::string, Thumbnail, std::less<>> m_thumbnails;
};

} // namespace devex::runtime
