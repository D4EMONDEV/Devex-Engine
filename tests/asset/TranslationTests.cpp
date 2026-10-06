#include <devex/asset/Artifact.hpp>
#include <devex/asset/Localization.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/TranslationData.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/asset/import/Importer.hpp>
#include <devex/core/File.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

using devex::asset::CsvTable;
using devex::asset::Localization;
using devex::asset::TranslationData;
using devex::asset::TranslationValue;

namespace {

[[nodiscard]] std::shared_ptr<const TranslationData> table(std::string_view csv)
{
    auto data = devex::asset::parseTranslationCsv(csv);
    REQUIRE(data.has_value());
    return std::make_shared<const TranslationData>(std::move(*data));
}

[[nodiscard]] bool hasGlyph(const devex::asset::FontData& font, std::uint32_t codepoint)
{
    return devex::asset::findGlyph(font, codepoint) != nullptr;
}

} // namespace

TEST_CASE("CSV text reads as spreadsheets write it and writes back the same", "[asset][translation]")
{
    // Quotes hold the delimiter, line breaks and doubled quotes; CRLF and a last line without its
    // line break end rows too.
    const auto read = devex::asset::parseCsv("keys,en,fr\r\nGREETING,\"Hello, you\",\"Il dit \"\"oui\"\"\nencore\"\r\nEMPTY,,\nLAST,a,b");
    REQUIRE(read.has_value());
    CHECK(read->delimiter == ',');
    CHECK_FALSE(read->byteOrderMark);
    REQUIRE(read->rows.size() == 4);
    CHECK(read->rows[1] == std::vector<std::string>{"GREETING", "Hello, you", "Il dit \"oui\"\nencore"});
    CHECK(read->rows[2] == std::vector<std::string>{"EMPTY", "", ""});
    CHECK(read->rows[3] == std::vector<std::string>{"LAST", "a", "b"});

    const auto again = devex::asset::parseCsv(devex::asset::writeCsv(*read));
    REQUIRE(again.has_value());
    CHECK(*again == *read);

    // What spreadsheets write in French: semicolons, and the mark of UTF-8 first.
    const auto french = devex::asset::parseCsv("\xEF\xBB\xBFkeys;en;fr\nPRICE;1,50 $;1,50 €\n");
    REQUIRE(french.has_value());
    CHECK(french->delimiter == ';');
    CHECK(french->byteOrderMark);
    CHECK(french->rows[1][2] == "1,50 €");
    CHECK(devex::asset::writeCsv(*french).starts_with("\xEF\xBB\xBFkeys;en;fr\n"));

    const auto tabs = devex::asset::parseCsv("keys\ten\nA\tb\n");
    REQUIRE(tabs.has_value());
    CHECK(tabs->delimiter == '\t');

    const auto open = devex::asset::parseCsv("keys,en\nA,\"never closed\n");
    REQUIRE_FALSE(open.has_value());
    CHECK(open.error().message.find("line 2") != std::string::npos);
}

TEST_CASE("A table of translations reads its languages, keys and messages as Godot does", "[asset][translation]")
{
    const auto data = devex::asset::parseTranslationCsv(
        "keys,en,_notes,fr-fr,pt_br\n"
        "PLAY,Play,the main button,Jouer,Jogar\n"
        ",ignored,,,\n"
        "LINES,One\\nTwo,,Un\\tdeux \\\\ trois,\n"
        "PLAY,Twice,,,\n");
    REQUIRE(data.has_value());
    // The column of notes is left out, and the codes are written one way.
    CHECK(data->languages == std::vector<std::string>{"en", "fr_FR", "pt_BR"});
    // A row without a key is skipped, and a key written twice keeps its first row.
    CHECK(data->keys == std::vector<std::string>{"PLAY", "LINES"});
    CHECK(data->message(0, 1) == "Jouer");
    CHECK(data->message(1, 0) == "One\nTwo");
    CHECK(data->message(1, 1) == "Un\tdeux \\ trois");
    // A missing translation is empty.
    CHECK(data->message(1, 2).empty());
    CHECK(data->message(5, 0).empty());

    const auto decoded = devex::asset::decodeTranslation(devex::asset::encodeTranslation(*data));
    REQUIRE(decoded.has_value());
    CHECK(*decoded == *data);

    // A column that names no language is refused rather than read as one.
    const auto wrong = devex::asset::parseTranslationCsv("id,name,hit points\nGOBLIN,Goblin,12\n");
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().message.find("'name'") != std::string::npos);
    CHECK_FALSE(devex::asset::parseTranslationCsv("keys\nA\n").has_value());
    CHECK_FALSE(devex::asset::parseTranslationCsv("keys,en,EN\nA,a,b\n").has_value());
}

TEST_CASE("Language codes are recognised, written one way and named", "[asset][translation]")
{
    CHECK(devex::asset::isLanguageCode("fr"));
    CHECK(devex::asset::isLanguageCode("pt-BR"));
    CHECK(devex::asset::isLanguageCode("zh_Hant"));
    CHECK(devex::asset::isLanguageCode("es_419"));
    CHECK_FALSE(devex::asset::isLanguageCode("f"));
    CHECK_FALSE(devex::asset::isLanguageCode("french"));
    CHECK_FALSE(devex::asset::isLanguageCode("_notes"));
    CHECK_FALSE(devex::asset::isLanguageCode("en__US"));

    CHECK(devex::asset::normalizeLanguage("FR-ca") == "fr_CA");
    CHECK(devex::asset::normalizeLanguage("zh-hant") == "zh_Hant");
    CHECK(devex::asset::baseLanguage("fr_CA") == "fr");
    CHECK(devex::asset::baseLanguage("de") == "de");

    CHECK(devex::asset::languageName("fr") == "French");
    CHECK(devex::asset::languageName("pt_BR") == "Portuguese (Brazil)");
    CHECK(devex::asset::languageName("zh_Hans") == "Chinese (Simplified)");
    CHECK(devex::asset::languageName("xx") == "xx");
    CHECK(devex::asset::nativeLanguageName("fr") == "Français");
    CHECK(devex::asset::nativeLanguageName("ru") == "Русский");
    CHECK(devex::asset::nativeLanguageName("pt_BR") == "Português (BR)");
    CHECK(std::ranges::find(devex::asset::commonLanguages(), "ja") != devex::asset::commonLanguages().end());
}

TEST_CASE("The characters of a table are those of its keys, messages and language names", "[asset][translation]")
{
    const auto data = table("keys,en,ru\nHELLO,Hi,Привет\n");
    const std::vector<std::uint32_t> characters = devex::asset::translationCharacters(*data);
    CHECK(std::ranges::is_sorted(characters));
    CHECK(std::ranges::adjacent_find(characters) == characters.end());
    // П of the message, у of "Русский", the name a menu of languages shows.
    CHECK(std::ranges::binary_search(characters, 0x041Fu));
    CHECK(std::ranges::binary_search(characters, 0x0443u));
    CHECK(std::ranges::binary_search(characters, static_cast<std::uint32_t>('H')));
}

TEST_CASE("A key shows in the language, then the fallback one, then as it is written", "[asset][translation]")
{
    Localization localization;
    const std::vector tables{table("keys,en,fr,de\nPLAY,Play,Jouer,Spielen\nQUIT,Quit,,Beenden\nSCORE,{points} points,{points} points,\n"),
                             table("keys,en,fr_CA\nPLAY,Ignored,Jouer!\nBACK,Back,Retour\n")};
    localization.setTables(tables);
    CHECK(localization.languages() == std::vector<std::string>{"de", "en", "fr", "fr_CA"});

    // No language: the keys themselves, as the editor shows them before a preview.
    CHECK(localization.translate("PLAY") == "PLAY");
    CHECK(localization.find("PLAY") == nullptr);

    localization.setLanguage("fr");
    CHECK(localization.language() == "fr");
    // The first table keeps the keys it has.
    CHECK(localization.translate("PLAY") == "Jouer");
    // Missing in French: the fallback language, English.
    CHECK(localization.translate("QUIT") == "Quit");
    // Not a key at all: the text stays as it is.
    CHECK(localization.translate("Level 3") == "Level 3");

    // A region the tables have, and one they do not: its base language.
    localization.setLanguage("fr-ca");
    CHECK(localization.language() == "fr_CA");
    CHECK(localization.translate("BACK") == "Retour");
    localization.setLanguage("de_AT");
    CHECK(localization.language() == "de");
    CHECK(localization.translate("QUIT") == "Beenden");
    // A language without a column: the fallback one.
    localization.setLanguage("ja");
    CHECK(localization.language() == "ja");
    CHECK(localization.translate("PLAY") == "Play");
    CHECK(localization.closestLanguage("pt") == std::nullopt);
    CHECK(localization.closestLanguage("en_GB") == "en");

    localization.setFallbackLanguage("de");
    CHECK(localization.translate("PLAY") == "Spielen");

    const std::uint64_t before = localization.revision();
    localization.setLanguage("en");
    CHECK(localization.revision() != before);
    const std::array values{TranslationValue{.name = "points", .value = "12"}};
    CHECK(localization.translate("SCORE", values) == "12 points");
    CHECK(localization.translate("{{literal}} {points} {missing}", values) == "{literal} 12 {missing}");
}

TEST_CASE("Projects keep the fallback and test languages of their translations", "[asset][project][translation]")
{
    devex::asset::Project project{.name = "Game", .root = "D:/game", .file = "D:/game/Game.dvxproj"};
    CHECK(devex::asset::writeProjectText(project).find("[localization") == std::string::npos);
    project.localization = {.fallbackLanguage = "fr", .testLanguage = "pt_BR"};
    const auto parsed = devex::asset::parseProject(devex::asset::writeProjectText(project), project.file);
    REQUIRE(parsed.has_value());
    CHECK(parsed->localization == project.localization);
}

TEST_CASE("Fonts bake the characters of the translations of the project", "[asset][database][translation][font]")
{
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-translations-" + devex::core::Uuid::generate().toString());
    {
        const devex::asset::Project project = *devex::asset::createProject(root, "Translated");
        const std::filesystem::path assets = project.assetsDirectory();
        std::filesystem::create_directories(assets / "fonts");
        std::filesystem::copy_file(std::filesystem::path{DEVEX_TEST_DATA_DIRECTORY} / "fonts" / "NotoSans-Regular.ttf",
                                   assets / "fonts" / "NotoSans-Regular.ttf");
        devex::core::JobSystem jobs(2);
        auto database = devex::asset::AssetDatabase::open(project, jobs, {.watchFiles = false});
        REQUIRE(database.has_value());
        const auto settle = [&] {
            (*database)->waitForImports();
            return (*database)->update();
        };
        static_cast<void>(settle());
        const auto font = (*database)->findByPath("res://assets/fonts/NotoSans-Regular.ttf");
        REQUIRE(font.has_value());
        const auto baked = [&] {
            const auto bytes = (*database)->loadArtifact(*font);
            REQUIRE(bytes.has_value());
            const auto data = devex::asset::decodeFont(*bytes);
            REQUIRE(data.has_value());
            return *data;
        };
        CHECK_FALSE(hasGlyph(baked(), 0x041F));

        // A new table: the font imports again with its Cyrillic letters.
        REQUIRE(devex::core::writeTextFile(assets / "menu.csv", "keys,en,ru\nHELLO,Hello,Привет\n"));
        (*database)->refresh();
        std::vector<devex::asset::AssetEvent> events = settle();
        CHECK(std::ranges::any_of(events, [&](const devex::asset::AssetEvent& event) { return event.id == *font; }));
        const auto translation = (*database)->findByPath("res://assets/menu.csv");
        REQUIRE(translation.has_value());
        CHECK((*database)->find(*translation)->type == devex::asset::AssetType::Translation);
        CHECK(hasGlyph(baked(), 0x041F));
        CHECK_FALSE(hasGlyph(baked(), 0x03A9));

        // A changed table brings its new letters; the font does not import again otherwise.
        REQUIRE(devex::core::writeTextFile(assets / "menu.csv", "keys,en,ru,el\nHELLO,Hello,Привет,Ωμέγα\n"));
        (*database)->refresh();
        static_cast<void>(settle());
        CHECK(hasGlyph(baked(), 0x03A9));
        (*database)->refresh();
        CHECK((*database)->pendingImports() == 0);
        // Another message written with the same characters: the table imports, the font stays.
        REQUIRE(devex::core::writeTextFile(assets / "menu.csv", "keys,en,ru,el\nHELLO,Helloo,Приивет,Ωμέγαα\n"));
        (*database)->refresh();
        events = settle();
        CHECK(std::ranges::any_of(events, [&](const devex::asset::AssetEvent& event) { return event.id == *translation; }));
        CHECK_FALSE(std::ranges::any_of(events, [&](const devex::asset::AssetEvent& event) { return event.id == *font; }));
        (*database)->refresh();
        CHECK((*database)->pendingImports() == 0);

        // A removed table takes them away.
        std::filesystem::remove(assets / "menu.csv");
        (*database)->refresh();
        static_cast<void>(settle());
        CHECK_FALSE(hasGlyph(baked(), 0x041F));
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}
