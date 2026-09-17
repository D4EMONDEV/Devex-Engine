#pragma once

#include <devex/serialization/Text.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace devex::tools::detail {

// A project known to the project manager.
struct ProjectEntry
{
    // The .dvxproj file.
    std::filesystem::path file;
    bool favorite = false;
    // When the editor last opened it, in seconds since 1970; 0 when never.
    std::int64_t lastOpened = 0;
};

enum class ProjectSort : std::uint8_t
{
    LastOpened,
    Name,
    Path,
};

// The projects of the project manager, kept in the editor's settings of the user.
class ProjectList
{
public:
    // Reads the [project] sections of a document, and the [recent_project] sections of older
    // versions of the editor.
    [[nodiscard]] static ProjectList read(const serialization::TextDocument& document);
    // Appends one [project] section per project.
    void write(serialization::TextDocument& document) const;

    [[nodiscard]] const std::vector<ProjectEntry>& entries() const noexcept;
    [[nodiscard]] const ProjectEntry* find(const std::filesystem::path& file) const;

    // Adds the project if needed; a time other than 0 records that it was just opened.
    void add(const std::filesystem::path& file, std::int64_t openedAt = 0);
    void remove(const std::filesystem::path& file);
    void setFavorite(const std::filesystem::path& file, bool favorite);
    // Forgets the projects whose file no longer exists and returns how many.
    std::size_t removeMissing();

    // The projects whose name or path contains the filter, ignoring case, favorites first, then
    // in the order asked for. Names come from the function, which reads them from the projects.
    [[nodiscard]] std::vector<ProjectEntry> sorted(ProjectSort sort, std::string_view filter,
                                                   const std::function<std::string(const ProjectEntry&)>& nameOf) const;

private:
    std::vector<ProjectEntry> m_entries;
};

// The canonical form of a project file path, so that one project is listed once.
[[nodiscard]] std::filesystem::path normalProjectPath(const std::filesystem::path& file);

} // namespace devex::tools::detail
