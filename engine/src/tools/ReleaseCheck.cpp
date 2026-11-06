#include <devex/tools/ReleaseCheck.hpp>

#include "ToolsState.hpp"

#include <devex/core/BuildInfo.hpp>
#include <devex/core/Log.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <format>
#include <mutex>
#include <thread>
#include <utility>

namespace devex::tools {

namespace {

constexpr std::int64_t secondsBetweenChecks = std::int64_t{24} * 60 * 60;

[[nodiscard]] std::int64_t secondsNow() noexcept
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// Digits and dots, the only thing the address of a page is made from.
[[nodiscard]] bool isPlainVersion(std::string_view version) noexcept
{
    const bool plain = !version.empty() && std::ranges::all_of(version, [](char each) { return (each >= '0' && each <= '9') || each == '.'; });
    return plain && core::compareVersions(version, "0") >= 0;
}

[[nodiscard]] bool isTrue(const nlohmann::json& release, const char* name)
{
    const auto found = release.find(name);
    return found != release.end() && found->is_boolean() && found->get<bool>();
}

} // namespace

UpdateSettings readUpdateSettings(const serialization::TextSection& section)
{
    UpdateSettings settings;
    if (const auto* value = section.findAttribute("check"))
    {
        settings.check = serialization::asBool(*value).value_or(true);
    }
    if (const auto* value = section.findAttribute("checked_at"))
    {
        settings.checkedAt = serialization::asInteger(*value).value_or(0);
    }
    if (const std::string* const newest = serialization::stringAttribute(section, "newest"))
    {
        settings.newest = *newest;
    }
    return settings;
}

serialization::TextSection writeUpdateSettings(const UpdateSettings& settings)
{
    serialization::TextSection section;
    section.type = "updates";
    section.attributes.push_back({"check", settings.check});
    section.attributes.push_back({"checked_at", settings.checkedAt});
    section.attributes.push_back({"newest", settings.newest});
    return section;
}

std::string releasePage(std::string_view version)
{
    return std::format("https://github.com/D4EMONDEV/Devex-Engine/releases/tag/v{}", version);
}

std::optional<std::string> newestRelease(std::string_view answer, std::string_view current)
{
    const nlohmann::json releases = nlohmann::json::parse(answer, nullptr, false);
    if (!releases.is_array())
    {
        return std::nullopt;
    }
    const bool onPreview = core::compareVersions(current, "1") < 0;
    std::optional<std::string> newest;
    for (const nlohmann::json& release : releases)
    {
        if (!release.is_object() || isTrue(release, "draft") || (!onPreview && isTrue(release, "prerelease")))
        {
            continue;
        }
        const auto tag = release.find("tag_name");
        if (tag == release.end() || !tag->is_string())
        {
            continue;
        }
        std::string_view version = tag->get_ref<const std::string&>();
        if (!version.starts_with('v'))
        {
            continue;
        }
        version.remove_prefix(1);
        if (isPlainVersion(version) && core::compareVersions(version, current) > 0 && (!newest || core::compareVersions(version, *newest) > 0))
        {
            newest = std::string(version);
        }
    }
    return newest;
}

bool updateCheckDue(std::int64_t checkedAt, std::int64_t now) noexcept
{
    // A clock put back does not wait for the date it had.
    return now < checkedAt || now - checkedAt >= secondsBetweenChecks;
}

std::string announcedRelease(const UpdateSettings& settings, std::string_view current)
{
    const bool announced = settings.check && isPlainVersion(settings.newest) && core::compareVersions(settings.newest, current) > 0;
    return announced ? settings.newest : std::string{};
}

namespace detail {

struct ReleaseCheck
{
    std::mutex mutex;
    std::optional<core::Result<std::string>> answer;
    // Last, to stop before what it writes to goes.
    std::jthread worker;
};

void updateReleaseCheck(ToolsState& state)
{
    if (!state.releaseCheck)
    {
        if (state.fetchText && state.updates.check && !state.releaseAnswered && updateCheckDue(state.updates.checkedAt, secondsNow()))
        {
            state.releaseCheck = std::make_shared<ReleaseCheck>();
            state.releaseCheck->worker = std::jthread([check = state.releaseCheck.get(), fetch = state.fetchText](std::stop_token stop) {
                core::Result<std::string> answer = fetch(std::string(releasesAddress), stop);
                const std::scoped_lock lock(check->mutex);
                check->answer = std::move(answer);
            });
        }
        return;
    }

    std::optional<core::Result<std::string>> answer;
    {
        const std::scoped_lock lock(state.releaseCheck->mutex);
        answer.swap(state.releaseCheck->answer);
    }
    if (!answer)
    {
        return;
    }
    state.releaseCheck.reset();
    state.releaseAnswered = true;
    if (!*answer)
    {
        // Without a connection, nothing is wrong: the next start asks again.
        DEVEX_LOG_DEBUG("Cannot tell whether a newer Devex is out: {}", answer->error());
        return;
    }
    const std::optional<std::string> newest = newestRelease(**answer, core::version());
    state.updates.checkedAt = secondsNow();
    state.updates.newest = newest.value_or(std::string{});
    saveUserSettings(state);
    if (newest)
    {
        DEVEX_LOG_INFO("Devex {} is available: {}", *newest, releasePage(*newest));
    }
}

} // namespace detail

} // namespace devex::tools
