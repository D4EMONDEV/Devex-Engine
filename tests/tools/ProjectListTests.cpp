#include "tools/ProjectList.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

using devex::tools::detail::ProjectEntry;
using devex::tools::detail::ProjectList;
using devex::tools::detail::ProjectSort;

namespace {

[[nodiscard]] std::string nameOf(const ProjectEntry& entry)
{
    return entry.file.stem().string();
}

} // namespace

TEST_CASE("The project list reads older recent projects and writes favorites and dates", "[tools][projects]")
{
    const auto document = devex::serialization::parseText(R"([recent_project path="C:/Games/Alpha/Alpha.dvxproj"]
[project path="C:/Games/Beta/Beta.dvxproj" favorite=true last_opened=1700000000]
[project path="C:/Games/Beta/Beta.dvxproj"]
[theme preset="light"]
)");
    REQUIRE(document.has_value());
    const ProjectList list = ProjectList::read(*document);
    REQUIRE(list.entries().size() == 2);
    CHECK_FALSE(list.entries()[0].favorite);
    CHECK(list.entries()[1].favorite);
    CHECK(list.entries()[1].lastOpened == 1700000000);

    devex::serialization::TextDocument written;
    list.write(written);
    const ProjectList reread = ProjectList::read(written);
    REQUIRE(reread.entries().size() == 2);
    CHECK(reread.entries()[1].favorite);
    CHECK(reread.entries()[1].lastOpened == 1700000000);
    CHECK(written.sections[0].type == "project");
}

TEST_CASE("Projects sort with favorites first and filter by name or path", "[tools][projects]")
{
    ProjectList list;
    list.add("C:/Games/Zeta/Zeta.dvxproj", 100);
    list.add("C:/Games/Alpha/Alpha.dvxproj", 300);
    list.add("C:/Work/Mid/Mid.dvxproj", 200);
    // Adding a known project again updates it rather than listing it twice.
    list.add("C:/Games/Zeta/Zeta.dvxproj", 400);
    REQUIRE(list.entries().size() == 3);

    auto sorted = list.sorted(ProjectSort::LastOpened, "", nameOf);
    REQUIRE(sorted.size() == 3);
    CHECK(nameOf(sorted[0]) == "Zeta");
    CHECK(nameOf(sorted[1]) == "Alpha");

    list.setFavorite(list.entries()[2].file, true);
    sorted = list.sorted(ProjectSort::Name, "", nameOf);
    CHECK(nameOf(sorted[0]) == "Mid");
    CHECK(nameOf(sorted[1]) == "Alpha");
    CHECK(nameOf(sorted[2]) == "Zeta");

    sorted = list.sorted(ProjectSort::Name, "GAMES", nameOf);
    CHECK(sorted.size() == 2);
    sorted = list.sorted(ProjectSort::Path, "alp", nameOf);
    REQUIRE(sorted.size() == 1);
    CHECK(nameOf(sorted[0]) == "Alpha");

    list.remove(sorted[0].file);
    CHECK(list.entries().size() == 2);
}

TEST_CASE("Missing projects can be forgotten", "[tools][projects]")
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-projects-" + devex::core::Uuid::generate().toString());
    const std::filesystem::path existing = root / "Kept" / "Kept.dvxproj";
    REQUIRE(devex::core::writeTextFile(existing, "[project format=1 name=\"Kept\"]\n"));

    ProjectList list;
    list.add(existing);
    list.add(root / "Gone" / "Gone.dvxproj");
    CHECK(list.find(root / "Gone" / "Gone.dvxproj") != nullptr);
    CHECK(list.removeMissing() == 1);
    REQUIRE(list.entries().size() == 1);
    CHECK(list.entries()[0].file.filename() == "Kept.dvxproj");

    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
