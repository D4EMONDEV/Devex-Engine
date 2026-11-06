#include <devex/tools/ReleaseCheck.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using devex::tools::newestRelease;

namespace {

// As GitHub answers, with only what is read.
constexpr const char* releases = R"([
    {"tag_name": "v0.12.0", "draft": true, "prerelease": true},
    {"tag_name": "v0.10.0", "draft": false, "prerelease": true},
    {"tag_name": "v0.11.0", "draft": false, "prerelease": true},
    {"tag_name": "v0.9.0", "draft": false, "prerelease": true},
    {"tag_name": "nightly", "draft": false, "prerelease": true},
    {"tag_name": "v0.13.0-rc1", "draft": false, "prerelease": true},
    {"tag_name": "v1.0.0", "draft": false, "prerelease": false},
    {"tag_name": "v1.1.0", "draft": false, "prerelease": true}
])";

} // namespace

TEST_CASE("The newest release is the one after the current version", "[tools][updates]")
{
    // Before 1.0, previews count, and drafts and tags that are not versions never do.
    CHECK(newestRelease(releases, "0.9.0") == "1.1.0");
    // Once on a stable version, previews no longer count.
    CHECK(newestRelease(releases, "1.0.0") == std::nullopt);
    CHECK(newestRelease(releases, "0.99.0") == "1.1.0");
    CHECK(newestRelease(R"([{"tag_name": "v1.0.0"}, {"tag_name": "v1.2.0", "prerelease": true}])", "0.10.0") == "1.2.0");

    // The same version and older ones are nothing new.
    CHECK(newestRelease(R"([{"tag_name": "v0.10.0"}, {"tag_name": "v0.9.0"}])", "0.10.0") == std::nullopt);
    // A version compares by numbers, not by letters.
    CHECK(newestRelease(R"([{"tag_name": "v0.9.0"}, {"tag_name": "v0.10.0"}])", "0.8.0") == "0.10.0");
}

TEST_CASE("A strange answer about the releases means none", "[tools][updates]")
{
    for (const char* const answer : {"", "not json", "{}", "[1, 2]", R"([{"tag_name": 3}])", R"([{"draft": "yes", "tag_name": "../../evil"}])",
                                     R"({"message": "API rate limit exceeded"})", R"([{"tag_name": "v9.9.9/../evil"}])"})
    {
        CHECK(newestRelease(answer, "0.9.0") == std::nullopt);
    }
}

TEST_CASE("A check waits a day after the last answer", "[tools][updates]")
{
    constexpr std::int64_t day = 24 * 60 * 60;
    const std::int64_t now = 1'800'000'000;
    CHECK(devex::tools::updateCheckDue(0, now));
    CHECK(devex::tools::updateCheckDue(now - day, now));
    CHECK_FALSE(devex::tools::updateCheckDue(now - day + 1, now));
    CHECK_FALSE(devex::tools::updateCheckDue(now, now));
    // A clock put back does not wait for the date it had.
    CHECK(devex::tools::updateCheckDue(now + 1, now));
}

TEST_CASE("What is announced is newer than what runs, and only if checks are on", "[tools][updates]")
{
    devex::tools::UpdateSettings settings{.check = true, .checkedAt = 5, .newest = "0.11.0"};
    CHECK(devex::tools::announcedRelease(settings, "0.10.0") == "0.11.0");
    // Once updated, the version it remembers is nothing to announce.
    CHECK(devex::tools::announcedRelease(settings, "0.11.0").empty());
    settings.check = false;
    CHECK(devex::tools::announcedRelease(settings, "0.10.0").empty());
    settings = {.check = true, .newest = ""};
    CHECK(devex::tools::announcedRelease(settings, "0.10.0").empty());
    // A settings file edited by hand does not choose the page that opens.
    settings.newest = "9.9/../../evil";
    CHECK(devex::tools::announcedRelease(settings, "0.10.0").empty());

    CHECK(devex::tools::releasePage("0.11.0") == "https://github.com/D4EMONDEV/Devex-Engine/releases/tag/v0.11.0");
}

TEST_CASE("The settings of the checks are saved and read back", "[tools][updates]")
{
    const devex::tools::UpdateSettings settings{.check = false, .checkedAt = 1'800'000'000, .newest = "0.11.0"};
    CHECK(devex::tools::readUpdateSettings(devex::tools::writeUpdateSettings(settings)) == settings);
    // Checks are on until someone turns them off.
    CHECK(devex::tools::readUpdateSettings({}).check);
}
