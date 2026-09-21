#include "tools/TextDocument.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {
struct TextFile
{
    std::filesystem::path root = std::filesystem::temp_directory_path() / ("devex-text-" + devex::core::Uuid::generate().toString());
    std::filesystem::path path = root / "test.cs";
    ~TextFile() { std::error_code error; std::filesystem::remove_all(root, error); }
};
}

using devex::tools::detail::TextDocument;
using devex::core::readTextFile;
using devex::core::writeTextFile;

TEST_CASE("Text editing preserves UTF-8 BOM and Windows newlines", "[tools][text]")
{
    TextFile file;
    const std::string initial = "\xEF\xBB\xBF// caf\xC3\xA9\r\nclass Test {}\r\n";
    REQUIRE(writeTextFile(file.path, initial));
    auto document = TextDocument::open(file.path);
    REQUIRE(document);
    CHECK(document->bom());
    CHECK(document->crlf());
    CHECK_FALSE(document->modified());
    CHECK(document->text == "// caf\xC3\xA9\nclass Test {}\n");
    REQUIRE(document->save());
    CHECK(*readTextFile(file.path) == initial);
    document->text += "// end\n";
    CHECK(document->modified());
    REQUIRE(document->save());
    CHECK(*readTextFile(file.path) == initial + "// end\r\n");
    CHECK_FALSE(document->modified());
    CHECK(devex::tools::detail::sameTextPath(file.path, file.root / "." / "test.cs"));
}

TEST_CASE("Text saves reject external changes and reload is explicit", "[tools][text]")
{
    TextFile file;
    REQUIRE(writeTextFile(file.path, "original\n"));
    auto document = TextDocument::open(file.path);
    REQUIRE(document);
    document->text = "local\n";
    REQUIRE(writeTextFile(file.path, "external\n"));
    CHECK_FALSE(document->save());
    CHECK(document->modified());
    CHECK(document->text == "local\n");
    CHECK(*readTextFile(file.path) == "external\n");
    REQUIRE(document->reload());
    CHECK(document->revision == 1);
    CHECK(document->text == "external\n");
    CHECK_FALSE(document->modified());
    document->text = "";
    REQUIRE(document->save());
    CHECK(readTextFile(file.path)->empty());
}

TEST_CASE("Failed text reads never drop local edits or recreate a deleted file", "[tools][text]")
{
    TextFile file;
    REQUIRE(writeTextFile(file.path, "before"));
    auto document = TextDocument::open(file.path);
    REQUIRE(document);
    document->text = "unsaved";
    REQUIRE(std::filesystem::remove(file.path));
    CHECK_FALSE(document->save());
    CHECK_FALSE(document->reload());
    CHECK(document->modified());
    CHECK(document->text == "unsaved");
    CHECK_FALSE(std::filesystem::exists(file.path));
}

TEST_CASE("Text editor rejects binary, malformed UTF-8 and oversized files", "[tools][text]")
{
    TextFile file;
    for (const std::string bytes : {std::string("a\0b", 3), std::string("\xFF\xFE"), std::string("\xC0\xAF"),
                                    std::string("\xED\xA0\x80"), std::string("\xF4\x90\x80\x80"),
                                    std::string("\xE2\x82"), std::string(TextDocument::maximumBytes + 1, 'a')})
    {
        REQUIRE(writeTextFile(file.path, bytes));
        CHECK_FALSE(TextDocument::open(file.path));
    }
    REQUIRE(writeTextFile(file.path, ""));
    auto document = TextDocument::open(file.path);
    REQUIRE(document);
    document->text = "\xF0\x9F\x8E\xAE";
    REQUIRE(document->save());
    CHECK(*readTextFile(file.path) == document->text);
}
