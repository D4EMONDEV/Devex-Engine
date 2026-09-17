#include "ProjectList.hpp"

#include <devex/core/Path.hpp>

#include <algorithm>
#include <cctype>
#include <string>

namespace devex::tools::detail {
namespace {

using serialization::TextValue;

[[nodiscard]] std::string lowercase(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

} // namespace

std::filesystem::path normalProjectPath(const std::filesystem::path& file)
{
    std::error_code error;
    const std::filesystem::path normal = std::filesystem::weakly_canonical(file, error);
    return error ? file.lexically_normal() : normal;
}

ProjectList ProjectList::read(const serialization::TextDocument& document)
{
    ProjectList list;
    for (const serialization::TextSection& section : document.sections)
    {
        if (section.type != "project" && section.type != "recent_project")
        {
            continue;
        }
        const TextValue* const pathValue = section.findAttribute("path");
        const std::string* const path = pathValue != nullptr ? serialization::asString(*pathValue) : nullptr;
        if (path == nullptr || path->empty())
        {
            continue;
        }
        ProjectEntry entry{.file = core::pathFromUtf8(*path)};
        if (list.find(entry.file) != nullptr)
        {
            continue;
        }
        if (const TextValue* const favorite = section.findAttribute("favorite"))
        {
            entry.favorite = serialization::asBool(*favorite).value_or(false);
        }
        if (const TextValue* const opened = section.findAttribute("last_opened"))
        {
            entry.lastOpened = serialization::asInteger(*opened).value_or(0);
        }
        list.m_entries.push_back(std::move(entry));
    }
    return list;
}

void ProjectList::write(serialization::TextDocument& document) const
{
    for (const ProjectEntry& entry : m_entries)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "project";
        section.attributes.push_back({"path", TextValue(core::toUtf8(entry.file))});
        if (entry.favorite)
        {
            section.attributes.push_back({"favorite", TextValue(true)});
        }
        if (entry.lastOpened != 0)
        {
            section.attributes.push_back({"last_opened", TextValue(entry.lastOpened)});
        }
    }
}

const std::vector<ProjectEntry>& ProjectList::entries() const noexcept
{
    return m_entries;
}

const ProjectEntry* ProjectList::find(const std::filesystem::path& file) const
{
    const auto found = std::ranges::find(m_entries, file.lexically_normal(),
                                         [](const ProjectEntry& entry) { return entry.file.lexically_normal(); });
    return found != m_entries.end() ? &*found : nullptr;
}

void ProjectList::add(const std::filesystem::path& file, std::int64_t openedAt)
{
    const std::filesystem::path normal = normalProjectPath(file);
    auto found = std::ranges::find(m_entries, normal.lexically_normal(),
                                   [](const ProjectEntry& entry) { return entry.file.lexically_normal(); });
    if (found == m_entries.end())
    {
        m_entries.push_back({.file = normal});
        found = std::prev(m_entries.end());
    }
    if (openedAt != 0)
    {
        found->lastOpened = openedAt;
    }
}

void ProjectList::remove(const std::filesystem::path& file)
{
    std::erase_if(m_entries, [&](const ProjectEntry& entry) {
        return entry.file.lexically_normal() == file.lexically_normal();
    });
}

void ProjectList::setFavorite(const std::filesystem::path& file, bool favorite)
{
    for (ProjectEntry& entry : m_entries)
    {
        if (entry.file.lexically_normal() == file.lexically_normal())
        {
            entry.favorite = favorite;
        }
    }
}

std::size_t ProjectList::removeMissing()
{
    return std::erase_if(m_entries, [](const ProjectEntry& entry) {
        std::error_code error;
        return !std::filesystem::is_regular_file(entry.file, error);
    });
}

std::vector<ProjectEntry> ProjectList::sorted(ProjectSort sort, std::string_view filter,
                                              const std::function<std::string(const ProjectEntry&)>& nameOf) const
{
    const std::string needle = lowercase(filter);
    std::vector<std::pair<ProjectEntry, std::string>> matches;
    for (const ProjectEntry& entry : m_entries)
    {
        std::string name = nameOf(entry);
        if (needle.empty() || lowercase(name).contains(needle) || lowercase(core::toUtf8(entry.file)).contains(needle))
        {
            matches.emplace_back(entry, std::move(name));
        }
    }
    std::ranges::stable_sort(matches, [sort](const auto& left, const auto& right) {
        if (left.first.favorite != right.first.favorite)
        {
            return left.first.favorite;
        }
        switch (sort)
        {
        case ProjectSort::LastOpened:
            return left.first.lastOpened > right.first.lastOpened;
        case ProjectSort::Name:
            return lowercase(left.second) < lowercase(right.second);
        case ProjectSort::Path:
            return left.first.file < right.first.file;
        }
        return false;
    });
    std::vector<ProjectEntry> result;
    result.reserve(matches.size());
    for (auto& [entry, name] : matches)
    {
        result.push_back(std::move(entry));
    }
    return result;
}

} // namespace devex::tools::detail
