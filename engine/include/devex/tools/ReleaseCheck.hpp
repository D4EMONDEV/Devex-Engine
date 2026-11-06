#pragma once

// Whether a newer Devex is out, which the project list says, as Godot does beside its version. The list asks GitHub at most
// once a day, on a thread of its own, and says nothing when it gets no answer. The program gives the way to read the
// internet (ApplicationConfig::fetchText): the engine and the games have no use for it.
#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>
#include <devex/serialization/Text.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace devex::tools {

// Reads a page of the internet as text, and gives up when asked to stop.
using TextFetcher = std::function<core::Result<std::string>(const std::string& address, std::stop_token stop)>;

// Kept with the settings of the editor.
struct UpdateSettings
{
    bool check = true;
    // When GitHub last answered, in seconds since 1970, and the newest version it told of; empty when none was newer.
    std::int64_t checkedAt = 0;
    std::string newest;

    bool operator==(const UpdateSettings&) const = default;
};

[[nodiscard]] DEVEX_API UpdateSettings readUpdateSettings(const serialization::TextSection& section);
[[nodiscard]] DEVEX_API serialization::TextSection writeUpdateSettings(const UpdateSettings& settings);

// Where GitHub lists the releases of Devex, as JSON.
inline constexpr std::string_view releasesAddress = "https://api.github.com/repos/D4EMONDEV/Devex-Engine/releases?per_page=30";
// Where a person reads about a version and downloads it.
[[nodiscard]] DEVEX_API std::string releasePage(std::string_view version);

// The newest version in that list that comes after the current one. Drafts do not count, nor does a tag that is not a plain
// version, and only a version before 1.0 hears of previews.
[[nodiscard]] DEVEX_API std::optional<std::string> newestRelease(std::string_view answer, std::string_view current);

[[nodiscard]] DEVEX_API bool updateCheckDue(std::int64_t checkedAt, std::int64_t now) noexcept;

// The version to announce to someone on the current one; empty when there is none.
[[nodiscard]] DEVEX_API std::string announcedRelease(const UpdateSettings& settings, std::string_view current);

} // namespace devex::tools
