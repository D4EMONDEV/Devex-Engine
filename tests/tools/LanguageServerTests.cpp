#include "tools/CodeArea.hpp"
#include "tools/LanguageServer.hpp"
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Process.hpp>

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <thread>

using namespace devex;
using namespace devex::tools::detail;

TEST_CASE("LSP frames survive arbitrary byte splits and reject invalid headers", "[tools][lsp]")
{
    const std::string json = "{\"text\":\"é😀\"}";
    const auto frame = lspFrame(json);
    LspFramer reader;
    for (std::size_t at = 0; at < frame.size(); ++at)
    {
        REQUIRE(reader.append(frame.substr(at, 1)));
        auto result = reader.take();
        REQUIRE(result);
        if (at + 1 < frame.size())
            CHECK_FALSE(*result);
        else
            REQUIRE(**result == json);
    }
    REQUIRE(reader.append(lspFrame("{}") + lspFrame("[]")));
    REQUIRE(**reader.take() == "{}");
    REQUIRE(**reader.take() == "[]");
    LspFramer invalid;
    REQUIRE(invalid.append("Content-Length: 5\r\nContent-Length: 2\r\n\r\n{}"));
    CHECK_FALSE(invalid.take());
    LspFramer huge;
    REQUIRE(huge.append("Content-Length: 99999999999\r\n\r\n"));
    CHECK_FALSE(huge.take());
}

TEST_CASE("LSP positions and file URIs preserve Unicode spaces and reserved characters",
          "[tools][lsp]")
{
    const std::string text = "é😀x\nhello";
    CHECK(lspPosition(text, 6) == LspPosition{0, 3});
    CHECK(lspOffset(text, {0, 3}) == 6);
    CHECK(lspOffset(text, {0, 2}) == 2);
    CHECK(lspPosition(text, 8) == LspPosition{1, 0});
    CHECK(lspOffset(text, {999, 999}) == text.size());
    const auto path =
        std::filesystem::temp_directory_path() / core::pathFromUtf8("é #? game/code.cs");
    const auto uri = lspUri(path);
    CHECK(uri.find("%20") != std::string::npos);
    CHECK(uri.find("%23") != std::string::npos);
    REQUIRE(lspPath(uri));
    CHECK(lspPath(uri)->lexically_normal() == path.lexically_normal());
    CHECK_FALSE(lspPath("https://example.org/script.cs"));
    CHECK_FALSE(lspPath("file:///tmp/%00"));
    CHECK(plainSnippet("Method(${1:value}, ${2:${3:other}})$0") == "Method(value, other)");
}

TEST_CASE("Server completions replace a full range and apply imports in one ordered change",
          "[tools][lsp]")
{
    TextEditState edit;
    edit.cursor = 13;
    edit.completing = true;
    edit.completions = {{"Player", "type", "Player", 10, 16, {{0, 0, "using Game;\n"}}}};
    acceptCompletion(edit);
    REQUIRE(edit.edits.size() == 2);
    CHECK(edit.edits[0] == CodeTextEdit{10, 16, "Player"});
    CHECK(edit.edits[1] == CodeTextEdit{0, 0, "using Game;\n"});
    CHECK(edit.pendingCursor == std::pair{28, 28});
}

namespace
{
struct Project
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() / ("devex-lsp-" + core::Uuid::generate().toString());
    Project() { std::filesystem::create_directories(root / "code"); }
    ~Project()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
template <class Predicate>
bool until(ScriptLanguageServices& service, const Project& project, const ScriptSettings& settings,
           const std::vector<TextDocument>& documents, Predicate predicate, int seconds = 15)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (std::chrono::steady_clock::now() < deadline)
    {
        service.update(project.root, settings, documents);
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}
} // namespace

TEST_CASE("The script client initializes synchronizes unsaved buffers and survives server restarts",
          "[tools][lsp][process]")
{
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    Project project;
    const auto path = project.root / "code/Test.cs";
    REQUIRE(core::writeTextFile(path, "before"));
    auto document = TextDocument::open(path);
    REQUIRE(document);
    std::vector<TextDocument> documents{std::move(*document)};
    ScriptSettings settings;
    settings.languageServers[0].executable = DEVEX_TEST_LSP_PROBE;
    ScriptLanguageServices service;
    REQUIRE(until(service, project, settings, documents,
                  [&] { return !service.diagnostics(path).empty(); }));
    CHECK(service.ready(path));
    CHECK(service.diagnostics(path).front().message == "live buffer warning");
    documents[0].text = "unsaved";
    REQUIRE(service.request(LanguageFeature::Completion, documents[0], 7));
    std::vector<LanguageReply> replies;
    REQUIRE(until(service, project, settings, documents,
                  [&]
                  {
                      replies = service.takeReplies();
                      return !replies.empty();
                  }));
    REQUIRE(replies.front().completions.size() == 1);
    CHECK(replies.front().completions.front().insertion == "unsaved");
    CHECK(core::readTextFile(path).value() == "before");
    CHECK(service.diagnostics(path).empty());
    // The unchanged prefix/suffix are not resent; changing a four-byte codepoint must not
    // split its UTF-8 sequence, and its range is expressed in UTF-16 units.
    for (const auto* face : {"🙂", "😀"})
    {
        documents[0].text = std::string("un") + face + "saved";
        REQUIRE(service.request(LanguageFeature::Completion, documents[0], 0));
        REQUIRE(until(service, project, settings, documents,
                      [&]
                      {
                          replies = service.takeReplies();
                          return !replies.empty();
                      }));
        REQUIRE(replies.front().completions.size() == 1);
        CHECK(replies.front().completions.front().insertion == face);
    }
    for (const auto feature : {LanguageFeature::Definition, LanguageFeature::References,
                               LanguageFeature::Hover, LanguageFeature::Format})
    {
        REQUIRE(service.request(feature, documents[0], 2));
        REQUIRE(until(service, project, settings, documents,
                      [&]
                      {
                          replies = service.takeReplies();
                          return !replies.empty();
                      }));
        CHECK(replies[0].feature == feature);
        if (feature == LanguageFeature::Format)
            CHECK(replies[0].edits.size() == 1);
        else if (feature == LanguageFeature::Hover)
            CHECK(replies[0].message == "Member information");
        else
            CHECK(replies[0].locations[0].path == path);
    }
    service.restart();
    REQUIRE(until(service, project, settings, documents, [&] { return service.ready(path); }));
    settings.languageServers[0].enabled = false;
    service.update(project.root, settings, documents);
    CHECK_FALSE(service.ready(path));
}

TEST_CASE("Installed clangd understands unsaved C++ buffers", "[.lsp-live][tools][lsp]")
{
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    const auto clangd = detectLanguageServer(1);
    if (clangd.empty())
        SKIP("clangd is not installed");
    Project project;
    const auto path = project.root / "code/Test.cpp";
    REQUIRE(core::writeTextFile(
        path, "struct Player { int speed; };\nint main() { Player player; player. }\n"));
    auto document = TextDocument::open(path);
    REQUIRE(document);
    std::vector<TextDocument> documents{std::move(*document)};
    ScriptSettings settings;
    settings.languageServers[1].executable = core::toUtf8(clangd);
    ScriptLanguageServices service;
    REQUIRE(until(service, project, settings, documents, [&] { return service.ready(path); }));
    const int cursor = static_cast<int>(documents[0].text.find("player. ") + 7);
    REQUIRE(service.request(LanguageFeature::Completion, documents[0], cursor));
    std::vector<LanguageReply> replies;
    REQUIRE(until(
        service, project, settings, documents,
        [&]
        {
            replies = service.takeReplies();
            return !replies.empty();
        },
        30));
    REQUIRE_FALSE(replies[0].completions.empty());
    CHECK(std::ranges::any_of(replies[0].completions, [](const auto& item)
                              { return item.text.find("speed") != std::string::npos; }));
}

TEST_CASE("Installed C# language servers resolve the Devex API through Game.csproj",
          "[.lsp-live][tools][lsp]")
{
    auto platform = platform::Platform::create();
    REQUIRE(platform);
    const auto bin = std::filesystem::path(DEVEX_TEST_RESOURCES_DIRECTORY).parent_path();
    auto server = detectLanguageServer(0, bin);
    if (server.empty())
        server = detectLanguageServer(0, bin.parent_path().parent_path() / "x64-debug/bin");
    if (server.empty())
        SKIP("csharp-ls is not installed");
    Project project;
    const auto managed = bin / "managed/Devex.Managed.dll";
    const auto csproj = project.root / "code/Game.csproj";
    REQUIRE(core::writeTextFile(
        csproj,
        "<Project "
        "Sdk=\"Microsoft.NET.Sdk\"><PropertyGroup><TargetFramework>net10.0</TargetFramework></"
        "PropertyGroup><ItemGroup><Reference Include=\"Devex.Managed\"><HintPath>" +
            core::toUtf8(managed) + "</HintPath></Reference></ItemGroup></Project>"));
    REQUIRE(platform::runAndLog({"dotnet", "restore", core::toUtf8(csproj)}, project.root,
                                "restore the LSP test project"));
    const auto path = project.root / "code/Player.cs";
    REQUIRE(core::writeTextFile(
        path, "using Devex; class Player { void Test() { Entity e = default; e. } }\n"));
    auto document = TextDocument::open(path);
    REQUIRE(document);
    std::vector<TextDocument> documents{std::move(*document)};
    ScriptSettings settings;
    settings.languageServers[0].executable = core::toUtf8(server);
    ScriptLanguageServices service;
    REQUIRE(until(service, project, settings, documents, [&] { return service.ready(path); }, 30));
    const int cursor = static_cast<int>(documents[0].text.find("e. ") + 2);
    std::vector<LanguageReply> replies;
    bool found = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!found && std::chrono::steady_clock::now() < deadline)
    {
        REQUIRE(service.request(LanguageFeature::Completion, documents[0], cursor));
        REQUIRE(until(
            service, project, settings, documents,
            [&]
            {
                replies = service.takeReplies();
                return !replies.empty();
            },
            20));
        found = std::ranges::any_of(replies[0].completions, [](const auto& item)
                                    { return item.text.find("IsValid") != std::string::npos; });
        if (!found)
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }
    CHECK(found);
    documents[0].text =
        "using Devex; class Player { void Test() { Entity e = default; e.DoesNotExist(); } }\n";
    REQUIRE(until(
        service, project, settings, documents,
        [&]
        {
            return std::ranges::any_of(service.diagnostics(path),
                                       [](const auto& diagnostic)
                                       {
                                           return diagnostic.severity == 1 &&
                                                  diagnostic.message.find("DoesNotExist") !=
                                                      std::string::npos;
                                       });
        },
        30));
    CHECK(core::readTextFile(path).value().find("DoesNotExist") == std::string::npos);
}
