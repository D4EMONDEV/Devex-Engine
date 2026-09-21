#include "tools/CodeArea.hpp"
#include "tools/CodeCompletion.hpp"
#include "tools/CodeHighlight.hpp"
#include "tools/CodeOutline.hpp"
#include "tools/TextDocument.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using devex::tools::detail::CodeLanguage;
using devex::tools::detail::CompletionItem;
using devex::tools::detail::HighlightState;
using devex::tools::detail::Token;
using devex::tools::detail::TokenKind;

namespace {

// The text a token covers, to read the results without counting characters.
[[nodiscard]] std::string_view textOf(std::string_view line, const Token& token)
{
    return line.substr(token.begin, token.end - token.begin);
}

[[nodiscard]] std::vector<Token> tokensOf(std::string_view line, CodeLanguage language)
{
    HighlightState state;
    return devex::tools::detail::highlightLine(line, language, state);
}

[[nodiscard]] bool hasToken(std::string_view line, const std::vector<Token>& tokens,
                            std::string_view text, TokenKind kind)
{
    return std::ranges::any_of(tokens, [&](const Token& token) {
        return token.kind == kind && textOf(line, token) == text;
    });
}

} // namespace

TEST_CASE("Files are recognized by their name and extension", "[tools][code]")
{
    using devex::tools::detail::languageOf;
    CHECK(languageOf("Game.cpp") == CodeLanguage::Cpp);
    CHECK(languageOf("Game.HPP") == CodeLanguage::Cpp);
    CHECK(languageOf("Mover.cs") == CodeLanguage::CSharp);
    CHECK(languageOf("code/CMakeLists.txt") == CodeLanguage::CMake);
    CHECK(languageOf("settings.json") == CodeLanguage::Json);
    CHECK(languageOf("arena.dvxscene") == CodeLanguage::DevexText);
    CHECK(languageOf("notes.txt") == CodeLanguage::PlainText);
}

TEST_CASE("C++ lines are split into keywords, strings, numbers and comments", "[tools][code]")
{
    const std::string_view line = "const int count = 42; // the answer";
    const std::vector<Token> tokens = tokensOf(line, CodeLanguage::Cpp);
    CHECK(hasToken(line, tokens, "const", TokenKind::Keyword));
    CHECK(hasToken(line, tokens, "int", TokenKind::Keyword));
    CHECK(hasToken(line, tokens, "42", TokenKind::Number));
    CHECK(hasToken(line, tokens, "// the answer", TokenKind::Comment));

    const std::string_view directive = "#include <devex/core/Log.hpp>";
    CHECK(hasToken(directive, tokensOf(directive, CodeLanguage::Cpp), directive, TokenKind::Directive));

    const std::string_view text = R"(auto name = "a \" quote"; 1.5e-3f)";
    const std::vector<Token> mixed = tokensOf(text, CodeLanguage::Cpp);
    CHECK(hasToken(text, mixed, R"("a \" quote")", TokenKind::String));
    CHECK(hasToken(text, mixed, "1.5e-3f", TokenKind::Number));
}

TEST_CASE("Block comments carry over to the next lines", "[tools][code]")
{
    HighlightState state;
    const std::string_view opening = "int value = 0; /* explains";
    std::vector<Token> tokens = devex::tools::detail::highlightLine(opening, CodeLanguage::Cpp, state);
    CHECK(hasToken(opening, tokens, "/* explains", TokenKind::Comment));
    CHECK(state.inBlockComment);

    const std::string_view middle = "   the value";
    tokens = devex::tools::detail::highlightLine(middle, CodeLanguage::Cpp, state);
    CHECK(hasToken(middle, tokens, middle, TokenKind::Comment));
    CHECK(state.inBlockComment);

    const std::string_view closing = "  */ int after = 1;";
    tokens = devex::tools::detail::highlightLine(closing, CodeLanguage::Cpp, state);
    CHECK(hasToken(closing, tokens, "  */", TokenKind::Comment));
    CHECK_FALSE(state.inBlockComment);
    CHECK(hasToken(closing, tokens, "int", TokenKind::Keyword));

    // A comment that opens and closes on one line leaves nothing behind.
    HighlightState single;
    const std::string_view whole = "float x = 0.0f; /* here */ bool ready = true;";
    tokens = devex::tools::detail::highlightLine(whole, CodeLanguage::Cpp, single);
    CHECK(hasToken(whole, tokens, "/* here */", TokenKind::Comment));
    CHECK_FALSE(single.inBlockComment);
}

TEST_CASE("C#, CMake and Devex files have their own colors", "[tools][code]")
{
    const std::string_view csharp = "[AssetType(\"audio\")] public AssetId Clip; // sound";
    const std::vector<Token> tokens = tokensOf(csharp, CodeLanguage::CSharp);
    CHECK(hasToken(csharp, tokens, "[AssetType(\"audio\")]", TokenKind::Directive));
    CHECK(hasToken(csharp, tokens, "public", TokenKind::Keyword));
    CHECK(hasToken(csharp, tokens, "AssetId", TokenKind::Type));
    CHECK(hasToken(csharp, tokens, "// sound", TokenKind::Comment));

    const std::string_view cmake = "target_sources(devex_tools PRIVATE ${SOURCES}) # files";
    const std::vector<Token> cmakeTokens = tokensOf(cmake, CodeLanguage::CMake);
    CHECK(hasToken(cmake, cmakeTokens, "target_sources", TokenKind::Directive));
    CHECK(hasToken(cmake, cmakeTokens, "PRIVATE", TokenKind::Keyword));
    CHECK(hasToken(cmake, cmakeTokens, "${SOURCES}", TokenKind::Type));
    CHECK(hasToken(cmake, cmakeTokens, "# files", TokenKind::Comment));

    const std::string_view devex = "[entity uuid=\"1234\" name=\"Robot\"]";
    const std::vector<Token> devexTokens = tokensOf(devex, CodeLanguage::DevexText);
    CHECK(hasToken(devex, devexTokens, "entity", TokenKind::Type));
    CHECK(hasToken(devex, devexTokens, "\"Robot\"", TokenKind::String));

    const std::string_view value = "position = vec3(0, 1.5, -2)";
    const std::vector<Token> valueTokens = tokensOf(value, CodeLanguage::DevexText);
    CHECK(hasToken(value, valueTokens, "vec3", TokenKind::Directive));
    CHECK(hasToken(value, valueTokens, "1.5", TokenKind::Number));
    CHECK(hasToken(value, valueTokens, "-2", TokenKind::Number));

    CHECK(devex::tools::detail::lineCommentOf(CodeLanguage::Cpp) == "//");
    CHECK(devex::tools::detail::lineCommentOf(CodeLanguage::CMake) == "#");
    CHECK(devex::tools::detail::lineCommentOf(CodeLanguage::PlainText).empty());
}

TEST_CASE("Completion offers keywords, engine names and words of the file", "[tools][code]")
{
    using devex::tools::detail::completionsFor;
    using devex::tools::detail::identifierBefore;

    const std::string text = "public class Target : Component\n{\n    public float FlashTime;\n    Fla";
    CHECK(identifierBefore(text, text.size()) == "Fla");
    CHECK(identifierBefore("value = 3", 9).empty());
    CHECK(identifierBefore("a.b", 2).empty());

    const std::vector<CompletionItem> names{{"Transform", "type"}, {"FlashIntensity", "field"}};
    const std::vector<CompletionItem> matches =
        completionsFor("Fla", CodeLanguage::CSharp, text, names);
    REQUIRE_FALSE(matches.empty());
    const auto has = [&matches](std::string_view name) {
        return std::ranges::any_of(matches, [name](const CompletionItem& item) { return item.text == name; });
    };
    CHECK(has("FlashIntensity"));
    // The name written in the file itself is offered too.
    CHECK(has("FlashTime"));
    // The prefix alone is never offered, and other names are left out.
    CHECK_FALSE(has("Fla"));
    CHECK_FALSE(has("Transform"));

    // Matching ignores case, but names written like the prefix come first.
    const std::vector<CompletionItem> loose =
        completionsFor("fla", CodeLanguage::CSharp, text, names);
    CHECK_FALSE(loose.empty());

    // Keywords of the language are offered as well, and an empty prefix offers nothing.
    CHECK_FALSE(completionsFor("publ", CodeLanguage::CSharp, text, {}).empty());
    CHECK(completionsFor("", CodeLanguage::CSharp, text, names).empty());
    CHECK(completionsFor("Fla", CodeLanguage::PlainText, "", {}).empty());

    // The registry gives the components of the engine.
    const std::vector<CompletionItem> engine = devex::tools::detail::engineNames(CodeLanguage::CSharp);
    CHECK(std::ranges::any_of(engine, [](const CompletionItem& item) {
        return item.text == "MeshRenderer" && item.detail == "component";
    }));
    CHECK(devex::tools::detail::engineNames(CodeLanguage::PlainText).empty());
}

namespace {

// A document of the editor, written in a temporary file.
[[nodiscard]] devex::tools::detail::TextDocument documentOf(std::string_view contents)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                       ("devex-code-" + devex::core::Uuid::generate().toString() + ".cs");
    REQUIRE(devex::core::writeTextFile(path, contents).has_value());
    auto opened = devex::tools::detail::TextDocument::open(path);
    REQUIRE(opened.has_value());
    std::filesystem::remove(path);
    return std::move(*opened);
}

} // namespace

TEST_CASE("Searching walks the matches and replaces them", "[tools][code]")
{
    using namespace devex::tools::detail;
    const TextDocument document = documentOf("float speed = 1;\nfloat Speed = 2;\nint other = 3;\n");
    TextEditState edit;
    edit.find = "speed";

    searchDocument(edit, document);
    // Case is ignored unless asked for.
    CHECK(edit.matches.size() == 2);
    edit.matchCase = true;
    searchDocument(edit, document);
    REQUIRE(edit.matches.size() == 1);

    selectMatch(edit, document, 1);
    CHECK(edit.currentMatch == 0);
    REQUIRE(edit.pendingCursor.has_value());
    CHECK(edit.pendingCursor->first == edit.matches[0]);
    CHECK(edit.pendingCursor->second == edit.matches[0] + 5);
    CHECK(edit.revealLine == 1);

    edit.replace = "velocity";
    replaceMatch(edit, document, false);
    REQUIRE(edit.edits.size() == 1);
    CHECK(edit.edits[0].begin == edit.matches[0]);
    CHECK(edit.edits[0].end == edit.matches[0] + 5);
    CHECK(edit.edits[0].text == "velocity");

    // Replacing everything works from the end, so that the offsets stay valid.
    edit.edits.clear();
    edit.matchCase = false;
    searchDocument(edit, document);
    replaceMatch(edit, document, true);
    REQUIRE(edit.edits.size() == 2);
    CHECK(edit.edits[0].begin > edit.edits[1].begin);
}

TEST_CASE("Commenting a selection adds and removes the marker", "[tools][code]")
{
    using namespace devex::tools::detail;
    const TextDocument document = documentOf("int a = 1;\n    int b = 2;\n\nint c = 3;\n");
    TextEditState edit;
    edit.selectionBegin = 0;
    edit.selectionEnd = 20;

    commentSelection(edit, document, CodeLanguage::CSharp);
    REQUIRE(edit.edits.size() == 1);
    CHECK(edit.edits[0].text == "// int a = 1;\n    // int b = 2;");

    // Lines already commented lose their marker, keeping their indentation.
    const TextDocument commented = documentOf("// int a = 1;\n    // int b = 2;\n");
    TextEditState second;
    second.selectionEnd = 25;
    commentSelection(second, commented, CodeLanguage::CSharp);
    REQUIRE(second.edits.size() == 1);
    CHECK(second.edits[0].text == "int a = 1;\n    int b = 2;");

    // A language without line comments is left alone.
    TextEditState plain;
    commentSelection(plain, document, CodeLanguage::Json);
    CHECK(plain.edits.empty());
}

TEST_CASE("Going to a line puts the cursor at its start", "[tools][code]")
{
    using namespace devex::tools::detail;
    const TextDocument document = documentOf("one\ntwo\nthree\n");
    TextEditState edit;
    goToLine(edit, document, 3);
    REQUIRE(edit.pendingCursor.has_value());
    CHECK(edit.pendingCursor->first == 8);
    CHECK(edit.revealLine == 3);

    // A line past the end stops at the end of the text.
    goToLine(edit, document, 99);
    CHECK(edit.pendingCursor->first == static_cast<int>(document.text.size()));
}

TEST_CASE("Saving removes the spaces at the end of lines", "[tools][code]")
{
    using devex::tools::detail::trimTrailingSpaces;
    std::string text = "int a = 1;   \n  kept\t\nlast  ";
    CHECK(trimTrailingSpaces(text) == 6);
    CHECK(text == "int a = 1;\n  kept\nlast");

    std::string clean = "nothing to trim\n";
    CHECK(trimTrailingSpaces(clean) == 0);
    CHECK(clean == "nothing to trim\n");
}


TEST_CASE("The outline lists what a file declares", "[tools][code]")
{
    using devex::tools::detail::CodeSymbol;
    using devex::tools::detail::outlineOf;

    const std::string csharp =
        "using Devex;\n\n// A comment with class in it\npublic class RobotGuide : Component\n{\n"
        "    public float Speed = 1.6f;\n\n    public override void Start()\n    {\n"
        "        PlayClip(Idle);\n    }\n\n    private void Face(Vec3 direction, float delta)\n"
        "    {\n    }\n}\n";
    const std::vector<CodeSymbol> symbols = outlineOf(csharp, CodeLanguage::CSharp);
    const auto find = [&symbols](std::string_view name) {
        return std::ranges::find(symbols, name, &CodeSymbol::name);
    };
    REQUIRE(find("RobotGuide") != symbols.end());
    CHECK(find("RobotGuide")->line == 4);
    CHECK(find("RobotGuide")->type);
    REQUIRE(find("Start") != symbols.end());
    CHECK(find("Start")->line == 8);
    CHECK_FALSE(find("Start")->type);
    CHECK(find("Face") != symbols.end());
    // Calls and comments are not declarations.
    CHECK(find("PlayClip") == symbols.end());

    const std::string cpp =
        "#include <devex/core/Log.hpp>\n\nstruct Player\n{\n    float speed = 1.0f;\n};\n\n"
        "void movePlayers(SystemContext& context)\n{\n    if (context.physics == nullptr)\n"
        "    {\n        return;\n    }\n}\n";
    const std::vector<CodeSymbol> cppSymbols = outlineOf(cpp, CodeLanguage::Cpp);
    CHECK(std::ranges::find(cppSymbols, "Player", &CodeSymbol::name) != cppSymbols.end());
    CHECK(std::ranges::find(cppSymbols, "movePlayers", &CodeSymbol::name) != cppSymbols.end());
    CHECK(std::ranges::find(cppSymbols, "if", &CodeSymbol::name) == cppSymbols.end());

    // Devex files list their sections, named when they carry a name.
    const std::string devex =
        "[scene format=1]\n\n[entity uuid=\"1\" name=\"Robot\"]\n\n[component type=\"Animator\"]\n";
    const std::vector<CodeSymbol> sections = outlineOf(devex, CodeLanguage::DevexText);
    CHECK(std::ranges::find(sections, "Robot", &CodeSymbol::name) != sections.end());
    CHECK(std::ranges::find(sections, "component", &CodeSymbol::name) != sections.end());

    // A language the reader does not know lists nothing.
    CHECK(outlineOf("anything at all", CodeLanguage::PlainText).empty());
}
