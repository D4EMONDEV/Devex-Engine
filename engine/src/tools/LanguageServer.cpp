#include "LanguageServer.hpp"
#include "../runtime/GameCodeBuilder.hpp"

#include <devex/core/BuildInfo.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/String.hpp>
#include <devex/platform/Process.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <deque>
#include <format>
#include <map>
#include <utility>

namespace devex::tools::detail
{
namespace
{
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
const Json& field(const Json& value, std::string_view key)
{
    static const Json empty;
    const auto found = value.is_object() ? value.find(key) : value.end();
    return found != value.end() ? *found : empty;
}
std::string string(const Json& value)
{
    return value.is_string() ? value.get<std::string>() : std::string{};
}
std::string string(const Json& value, std::string_view key) { return string(field(value, key)); }
int number(const Json& value, int fallback = 0)
{
    if (!value.is_number_integer())
        return fallback;
    const auto n = value.get<std::int64_t>();
    return n >= 0 && n <= 0x7fffffff ? static_cast<int>(n) : fallback;
}
LspPosition position(const Json& value)
{
    return {number(field(value, "line")), number(field(value, "character"))};
}
Json json(LspPosition p) { return {{"line", p.line}, {"character", p.character}}; }
bool provided(const Json& value)
{
    return value.is_object() || (value.is_boolean() && value.get<bool>());
}
std::optional<std::size_t> category(const std::filesystem::path& path)
{
    const auto language = languageOf(path);
    if (language == CodeLanguage::CSharp)
        return 0;
    if (language == CodeLanguage::Cpp)
        return 1;
    return std::nullopt;
}
std::string markup(const Json& value)
{
    if (value.is_string())
        return value.get<std::string>().substr(0, 8192);
    if (value.is_object())
    {
        const auto source = string(value, "value").substr(0, 8192);
        if (string(value, "kind") != "markdown")
            return source;
        std::string plain;
        for (std::size_t at = 0; at < source.size();)
        {
            const auto end = source.find('\n', at);
            const auto line = std::string_view(source).substr(
                at, end == std::string::npos ? source.size() - at : end - at);
            if (!core::trimmed(line).starts_with("```"))
            {
                for (const auto ch : line)
                    if (ch != '`')
                        plain += ch;
                plain += '\n';
            }
            at = end == std::string::npos ? source.size() : end + 1;
        }
        return std::string(core::trimmed(plain, " \t\r\n", " \t\r\n"));
    }
    std::string text;
    if (value.is_array())
        for (const auto& item : value)
        {
            if (!text.empty())
                text += "\n\n";
            text += markup(item);
        }
    return text.substr(0, 8192);
}
std::vector<CodeTextEdit> edits(const Json& value, std::string_view text)
{
    std::vector<CodeTextEdit> result;
    if (!value.is_array())
        return result;
    for (const auto& edit : value)
    {
        const auto& range = field(edit, "range");
        if (!range.is_object() || !field(edit, "newText").is_string())
            continue;
        const auto begin = lspOffset(text, position(field(range, "start")));
        const auto end = lspOffset(text, position(field(range, "end")));
        if (begin > end)
            continue;
        result.push_back({static_cast<int>(begin), static_cast<int>(end), string(edit, "newText")});
    }
    std::ranges::sort(result, [](const auto& a, const auto& b) { return a.begin > b.begin; });
    for (std::size_t i = 1; i < result.size(); ++i)
        if (result[i].end > result[i - 1].begin)
            return {};
    return result;
}
std::filesystem::path compileCommands(const std::filesystem::path& project)
{
    // Follow the same active cache as the native builder instead of traversing every old
    // build and its generated files on the UI thread.
    const auto directory = runtime::detail::GameCodeBuilder::libraryPath(
                               asset::Project{.root = project}, core::buildType())
                               .parent_path()
                               .parent_path();
    std::error_code error;
    return std::filesystem::is_regular_file(directory / "compile_commands.json", error)
               ? directory
               : std::filesystem::path{};
}

struct Request
{
    LanguageFeature feature;
    std::filesystem::path path;
    std::string text;
    int cursor = 0;
    Clock::time_point sent = Clock::now();
};
struct Buffer
{
    std::string text;
    std::string saved;
    std::vector<LspDiagnostic> diagnostics;
    int version = 0;
    Clock::time_point changed = Clock::now();
    bool dirty = false;
};

class Session
{
  public:
    std::optional<platform::Process> process;
    LspFramer framer;
    std::string output;
    std::size_t sent = 0;
    std::string state;
    bool initialized = false;
    bool incremental = false;
    Json capabilities;
    int nextId = 1;
    std::map<int, Request> requests;
    std::map<std::string, Buffer> buffers;
    std::deque<LanguageReply> replies;
    Clock::time_point started = Clock::now();
    std::size_t language = 0;
    std::filesystem::path project;
    std::filesystem::path database;
    Clock::time_point checkedDatabase = Clock::now();

    void fail(std::string message)
    {
        state = std::move(message);
        process.reset();
        initialized = false;
        requests.clear();
        for (auto& [uri, buffer] : buffers)
        {
            static_cast<void>(uri);
            buffer.diagnostics.clear();
        }
    }
    void send(Json message)
    {
        const std::string encoded =
            lspFrame(message.dump(-1, ' ', false, Json::error_handler_t::replace));
        if (output.size() - sent + encoded.size() > LspFramer::maximumBytes)
        {
            fail("Language server queue is full; restart it");
            return;
        }
        if (sent > 0)
        {
            output.erase(0, sent);
            sent = 0;
        }
        output += encoded;
    }
    void notify(std::string_view method, Json params)
    {
        send({{"jsonrpc", "2.0"}, {"method", method}, {"params", std::move(params)}});
    }
    void begin(const std::filesystem::path& root, std::size_t kind,
               const LanguageServerSettings& setting, const std::filesystem::path& program)
    {
        project = root;
        language = kind;
        if (!setting.enabled)
        {
            state = "Language server disabled";
            return;
        }
        const auto executable = setting.executable.empty() ? detectLanguageServer(kind, program)
                                                           : core::pathFromUtf8(setting.executable);
        if (executable.empty())
        {
            state = kind == 0 ? "C# LSP not found: install csharp-ls or choose its executable in "
                                "Script Editors"
                              : "C++ LSP not found: install clangd or choose its executable in "
                                "Script Editors";
            return;
        }
        std::vector<std::string> command;
        if (setting.arguments.empty())
            command = {core::toUtf8(executable)};
        else
        {
            auto parsed = scriptEditorCommand(
                {ScriptEditor::Custom, core::toUtf8(executable), setting.arguments}, {}, root);
            if (!parsed)
            {
                fail(parsed.error().message);
                return;
            }
            command = std::move(*parsed);
            if (!command.empty() && command.back().empty())
                command.pop_back();
        }
        std::string executableName = core::toUtf8(executable.filename());
        std::ranges::transform(executableName, executableName.begin(), [](unsigned char ch)
                               { return static_cast<char>(std::tolower(ch)); });
        if (kind == 1)
            database = compileCommands(root);
        if (kind == 1 && executableName.starts_with("clangd"))
        {
            command.push_back("--background-index");
            command.push_back("--header-insertion=never");
            command.push_back("--log=error");
            if (!database.empty())
                command.push_back("--compile-commands-dir=" + core::toUtf8(database));
        }
        auto launched = platform::Process::startProtocol(command, root / "code");
        if (!launched)
        {
            fail(launched.error().message);
            return;
        }
        process = std::move(*launched);
        state = "Starting " + core::toUtf8(executable.filename());
        Json params = {
            {"processId", platform::currentProcessId()},
            {"clientInfo", {{"name", "Devex Script"}, {"version", "0.1"}}},
            {"rootUri", lspUri(root / "code")},
            {"workspaceFolders", Json::array({{{"uri", lspUri(root / "code")},
                                               {"name", core::toUtf8(root.filename())}}})},
            {"capabilities",
             {{"general", {{"positionEncodings", Json::array({"utf-16"})}}},
              {"textDocument",
               {{"synchronization", {{"didSave", true}}},
                {"completion",
                 {{"completionItem", {{"snippetSupport", false}, {"insertReplaceSupport", true}}}}},
                {"hover", {{"contentFormat", Json::array({"plaintext"})}}},
                {"definition", {{"linkSupport", true}}},
                {"publishDiagnostics", {{"versionSupport", true}}}}},
              {"workspace",
               {{"configuration", true}, {"workspaceFolders", true}, {"applyEdit", false}}}}}};
        if (kind == 1 && executableName.starts_with("clangd"))
        {
            Json flags = Json::array({"-std=c++23", "-I" + core::toUtf8(root / "code")});
            std::error_code error;
            auto directory = program;
            for (int parent = 0; parent < 5 && !directory.empty();
                 ++parent, directory = directory.parent_path())
                for (const auto& include : {directory / "include", directory / "engine/include"})
                    if (std::filesystem::is_regular_file(include / "devex/runtime/Game.hpp", error))
                        flags.push_back("-I" + core::toUtf8(include));
#ifdef _WIN32
            const auto dependencies = program.parent_path() / "vcpkg_installed/x64-windows/include";
#else
            const auto dependencies = program.parent_path() / "vcpkg_installed/x64-linux/include";
#endif
            if (std::filesystem::is_directory(dependencies, error))
                flags.push_back("-I" + core::toUtf8(dependencies));
            params["initializationOptions"] = {{"fallbackFlags", std::move(flags)}};
        }
        send({{"jsonrpc", "2.0"},
              {"id", 0},
              {"method", "initialize"},
              {"params", std::move(params)}});
    }
    void synchronize(const TextDocument& document, bool force = false)
    {
        if (!initialized)
            return;
        const auto uri = lspUri(document.path);
        auto [entry, inserted] = buffers.try_emplace(uri);
        auto& buffer = entry->second;
        if (inserted)
        {
            buffer.text = document.text;
            buffer.saved = document.modified() ? std::string{} : document.text;
            buffer.version = 1;
            notify("textDocument/didOpen",
                   {{"textDocument",
                     {{"uri", uri},
                      {"languageId", language == 0                       ? "csharp"
                                     : document.path.extension() == ".c" ? "c"
                                                                         : "cpp"},
                      {"version", 1},
                      {"text", document.text}}}});
        }
        if (buffer.text != document.text)
        {
            if (!buffer.dirty)
            {
                buffer.changed = Clock::now();
                buffer.dirty = true;
                buffer.diagnostics.clear();
            }
            if (!force && Clock::now() - buffer.changed < 250ms)
                return;
            Json change = {{"text", document.text}};
            if (incremental)
            {
                std::size_t first = 0;
                while (first < buffer.text.size() && first < document.text.size() &&
                       buffer.text[first] == document.text[first])
                    ++first;
                while (first > 0 && first < buffer.text.size() &&
                       (static_cast<unsigned char>(buffer.text[first]) & 0xC0) == 0x80)
                    --first;
                auto oldEnd = buffer.text.size();
                auto newEnd = document.text.size();
                while (oldEnd > first && newEnd > first &&
                       buffer.text[oldEnd - 1] == document.text[newEnd - 1])
                {
                    --oldEnd;
                    --newEnd;
                }
                while (oldEnd < buffer.text.size() &&
                       (static_cast<unsigned char>(buffer.text[oldEnd]) & 0xC0) == 0x80)
                {
                    ++oldEnd;
                    ++newEnd;
                }
                change["text"] = document.text.substr(first, newEnd - first);
                change["range"] = {{"start", json(lspPosition(buffer.text, first))},
                                   {"end", json(lspPosition(buffer.text, oldEnd))}};
            }
            buffer.text = document.text;
            buffer.dirty = false;
            ++buffer.version;
            notify("textDocument/didChange",
                   {{"textDocument", {{"uri", uri}, {"version", buffer.version}}},
                    {"contentChanges", Json::array({change})}});
        }
        else
            buffer.dirty = false;
        if (!document.modified() && buffer.saved != document.text)
        {
            buffer.saved = document.text;
            notify("textDocument/didSave",
                   {{"textDocument", {{"uri", uri}}}, {"text", document.text}});
        }
    }
    bool request(LanguageFeature feature, const TextDocument& document, int cursor)
    {
        if (!initialized || requests.size() >= 16)
            return false;
        constexpr std::array providers{"completionProvider", "definitionProvider",
                                       "referencesProvider", "hoverProvider",
                                       "documentFormattingProvider"};
        if (!provided(field(capabilities, providers[static_cast<std::size_t>(feature)])))
            return false;
        std::erase_if(requests,
                      [&](const auto& entry)
                      {
                          if (entry.second.feature != feature)
                              return false;
                          notify("$/cancelRequest", {{"id", entry.first}});
                          return true;
                      });
        synchronize(document, true);
        constexpr std::array methods{"textDocument/completion", "textDocument/definition",
                                     "textDocument/references", "textDocument/hover",
                                     "textDocument/formatting"};
        Json params = {
            {"textDocument", {{"uri", lspUri(document.path)}}},
            {"position",
             json(lspPosition(document.text, static_cast<std::size_t>(std::max(cursor, 0))))}};
        if (feature == LanguageFeature::References)
            params["context"] = {{"includeDeclaration", true}};
        if (feature == LanguageFeature::Format)
        {
            params.erase("position");
            params["options"] = {{"tabSize", 4}, {"insertSpaces", true}};
        }
        const int id = nextId++;
        requests.emplace(id, Request{feature, document.path, document.text, cursor});
        send({{"jsonrpc", "2.0"},
              {"id", id},
              {"method", methods[static_cast<std::size_t>(feature)]},
              {"params", std::move(params)}});
        return true;
    }
    void answer(const Json& message)
    {
        const auto& id = field(message, "id");
        const auto method = string(message, "method");
        const auto& params = field(message, "params");
        if (!method.empty() && !id.is_null())
        {
            Json response;
            if (method == "workspace/configuration")
            {
                response = Json::array();
                const auto& items = field(params, "items");
                if (items.is_array())
                    for (const auto& item : items)
                    {
                        static_cast<void>(item);
                        response.push_back(Json::object());
                    }
            }
            else if (method == "workspace/workspaceFolders")
                response = Json::array({{{"uri", lspUri(project / "code")},
                                         {"name", core::toUtf8(project.filename())}}});
            else if (method == "workspace/applyEdit")
                response = {{"applied", false},
                            {"failureReason", "Use an explicit editor action to apply edits"}};
            if (method == "workspace/configuration" || method == "workspace/workspaceFolders" ||
                method == "workspace/applyEdit" || method == "client/registerCapability" ||
                method == "client/unregisterCapability" ||
                method == "window/workDoneProgress/create")
                send({{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(response)}});
            else
                send({{"jsonrpc", "2.0"},
                      {"id", id},
                      {"error", {{"code", -32601}, {"message", "Method not supported"}}}});
            return;
        }
        if (method == "textDocument/publishDiagnostics")
        {
            const auto found = buffers.find(string(params, "uri"));
            if (found == buffers.end() || found->second.dirty)
                return;
            auto& buffer = found->second;
            if (!field(params, "version").is_null() &&
                number(field(params, "version")) != buffer.version)
                return;
            buffer.diagnostics.clear();
            const auto& diagnostics = field(params, "diagnostics");
            if (diagnostics.is_array())
                for (const auto& diagnostic : diagnostics)
                {
                    if (buffer.diagnostics.size() >= 1000)
                        break;
                    const auto& range = field(diagnostic, "range");
                    buffer.diagnostics.push_back(
                        {position(field(range, "start")), position(field(range, "end")),
                         number(field(diagnostic, "severity"), 1), string(diagnostic, "message")});
                }
            return;
        }
        if (!id.is_number_integer())
            return;
        if (id == 0)
        {
            if (!field(message, "error").is_null())
            {
                fail("Language server initialization failed: " +
                     string(field(message, "error"), "message"));
                return;
            }
            capabilities = field(field(message, "result"), "capabilities");
            const auto& sync = field(capabilities, "textDocumentSync");
            incremental = number(sync.is_object() ? field(sync, "change") : sync) == 2;
            const auto encoding = string(capabilities, "positionEncoding");
            if (!encoding.empty() && encoding != "utf-16")
            {
                fail("Language server must use UTF-16 positions");
                return;
            }
            initialized = true;
            state = language == 0 ? "C# LSP ready" : "C++ LSP ready";
            notify("initialized", Json::object());
            return;
        }
        const auto found = requests.find(number(id, -1));
        if (found == requests.end())
            return;
        const auto request = std::move(found->second);
        requests.erase(found);
        LanguageReply reply{request.feature, request.path, request.text, request.cursor};
        const auto& result = field(message, "result");
        if (!field(message, "error").is_null())
            reply.message = string(field(message, "error"), "message");
        else if (request.feature == LanguageFeature::Completion)
        {
            const auto& items = result.is_array() ? result : field(result, "items");
            if (items.is_array())
                for (const auto& item : items)
                {
                    if (reply.completions.size() >= 100)
                        break;
                    std::string prefix(identifierBefore(
                        request.text, static_cast<std::size_t>(std::max(request.cursor, 0))));
                    auto filter = string(item, "filterText");
                    if (filter.empty())
                        filter = string(item, "label");
                    const auto lower = [](unsigned char ch)
                    { return static_cast<char>(std::tolower(ch)); };
                    std::ranges::transform(prefix, prefix.begin(), lower);
                    std::ranges::transform(filter, filter.begin(), lower);
                    if (!prefix.empty() && filter.find(prefix) == std::string::npos)
                        continue;
                    CompletionItem completion{string(item, "label"), string(item, "detail")};
                    const auto& edit = field(item, "textEdit");
                    completion.insertion =
                        edit.is_object() ? string(edit, "newText") : string(item, "insertText");
                    if (completion.insertion.empty())
                        completion.insertion = completion.text;
                    if (number(field(item, "insertTextFormat")) == 2)
                        completion.insertion = plainSnippet(completion.insertion);
                    if (edit.is_object())
                    {
                        const auto& range = field(edit, "range").is_object()
                                                ? field(edit, "range")
                                                : field(edit, "replace");
                        if (range.is_object())
                        {
                            completion.replaceBegin = static_cast<int>(
                                lspOffset(request.text, position(field(range, "start"))));
                            completion.replaceEnd = static_cast<int>(
                                lspOffset(request.text, position(field(range, "end"))));
                        }
                    }
                    completion.additionalEdits =
                        edits(field(item, "additionalTextEdits"), request.text);
                    if (!completion.text.empty())
                        reply.completions.push_back(std::move(completion));
                }
        }
        else if (request.feature == LanguageFeature::Hover)
            reply.message = markup(field(result, "contents"));
        else if (request.feature == LanguageFeature::Format)
            reply.edits = edits(result, request.text);
        else
        {
            const auto add = [&](const Json& location)
            {
                const auto path =
                    lspPath(string(location, "uri").empty() ? string(location, "targetUri")
                                                            : string(location, "uri"));
                const auto& range = field(location, "range").is_object()
                                        ? field(location, "range")
                                        : field(location, "targetSelectionRange");
                if (path && reply.locations.size() < 200)
                    reply.locations.push_back({*path, position(field(range, "start"))});
            };
            if (result.is_array())
                for (const auto& location : result)
                    add(location);
            else if (result.is_object())
                add(result);
        }
        if (replies.size() >= 32)
            replies.pop_front();
        replies.push_back(std::move(reply));
    }
    void poll()
    {
        if (!process)
            return;
        if (!initialized && Clock::now() - started > 30s)
        {
            fail("Language server initialization timed out; restart it in Script Editors");
            return;
        }
        if (sent < output.size())
        {
            auto written = process->writeBytes(std::string_view(output).substr(
                sent, std::min<std::size_t>(output.size() - sent, 16384)));
            if (!written)
            {
                fail(written.error().message);
                return;
            }
            sent += *written;
            if (sent == output.size())
            {
                output.clear();
                sent = 0;
            }
        }
        if (auto appended = framer.append(process->readBytes()); !appended)
        {
            fail(appended.error().message);
            return;
        }
        for (int batch = 0; batch < 32; ++batch)
        {
            auto packet = framer.take();
            if (!packet)
            {
                fail(packet.error().message);
                return;
            }
            if (!*packet)
                break;
            const auto message = Json::parse(**packet, nullptr, false);
            if (message.is_discarded())
            {
                fail("Invalid JSON from language server");
                return;
            }
            answer(message);
            if (!process)
                return;
        }
        std::erase_if(requests,
                      [](const auto& entry) { return Clock::now() - entry.second.sent > 15s; });
        if (const auto code = process->exitCode())
            fail(std::format("Language server stopped (code {}); restart it in Script Editors",
                             *code));
    }
};
} // namespace

std::filesystem::path detectLanguageServer(std::size_t language,
                                           const std::filesystem::path& program)
{
#ifdef _WIN32
    const std::string name = language == 0 ? "csharp-ls.exe" : "clangd.exe";
    constexpr char separator = ';';
    const auto home = core::pathFromUtf8(platform::environmentVariable("USERPROFILE"));
#else
    const std::string name = language == 0 ? "csharp-ls" : "clangd";
    constexpr char separator = ':';
    const auto home = core::pathFromUtf8(platform::environmentVariable("HOME"));
#endif
    std::vector<std::filesystem::path> candidates;
    if (!program.empty())
        candidates.push_back(program / "language-servers" / name);
    if (!home.empty())
        candidates.push_back(home / ".dotnet/tools" / name);
#ifdef _WIN32
    candidates.push_back(core::pathFromUtf8(platform::environmentVariable("ProgramFiles")) /
                         "LLVM/bin" / name);
#endif
    const auto path = platform::environmentVariable("PATH");
    for (std::size_t at = 0; at < path.size();)
    {
        const auto end = path.find(separator, at);
        const auto directory =
            std::string_view(path).substr(at, (end == std::string::npos ? path.size() : end) - at);
        if (!directory.empty())
            candidates.push_back(core::pathFromUtf8(directory) / name);
        at = end == std::string::npos ? path.size() : end + 1;
    }
    std::error_code error;
    for (const auto& file : candidates)
        if (std::filesystem::is_regular_file(file, error))
            return file;
    return {};
}

struct ScriptLanguageServices::Impl
{
    std::filesystem::path program;
    std::filesystem::path project;
    std::array<LanguageServerSettings, 2> settings;
    std::array<std::unique_ptr<Session>, 2> sessions;
    bool configured = false;
};
ScriptLanguageServices::ScriptLanguageServices(std::filesystem::path program)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->program = std::move(program);
}
ScriptLanguageServices::~ScriptLanguageServices() = default;
void ScriptLanguageServices::restart() { m_impl->configured = false; }
void ScriptLanguageServices::update(const std::filesystem::path& project,
                                    const ScriptSettings& settings,
                                    std::span<const TextDocument> documents)
{
    auto& impl = *m_impl;
    if (!impl.configured || impl.project != project || impl.settings != settings.languageServers)
    {
        impl.sessions = {};
        impl.project = project;
        impl.settings = settings.languageServers;
        impl.configured = true;
    }
    if (project.empty())
        return;
    for (std::size_t i = 0; i < impl.sessions.size(); ++i)
    {
        bool needed = false;
        for (const auto& doc : documents)
            needed = needed || category(doc.path) == i;
        if (!needed && !impl.sessions[i])
            continue;
        if (!impl.sessions[i])
        {
            impl.sessions[i] = std::make_unique<Session>();
            impl.sessions[i]->begin(project, i, impl.settings[i], impl.program);
        }
        if (i == 1 && impl.sessions[i]->initialized &&
            Clock::now() - impl.sessions[i]->checkedDatabase > 2s)
        {
            impl.sessions[i]->checkedDatabase = Clock::now();
            if (compileCommands(project) != impl.sessions[i]->database)
            {
                impl.sessions[i] = std::make_unique<Session>();
                impl.sessions[i]->begin(project, i, impl.settings[i], impl.program);
            }
        }
        auto& session = *impl.sessions[i];
        session.poll();
        for (const auto& doc : documents)
            if (category(doc.path) == i)
                session.synchronize(doc);
        std::erase_if(session.buffers,
                      [&](const auto& entry)
                      {
                          const bool opened =
                              std::ranges::any_of(documents, [&](const auto& doc)
                                                  { return lspUri(doc.path) == entry.first; });
                          if (!opened && session.initialized)
                              session.notify("textDocument/didClose",
                                             {{"textDocument", {{"uri", entry.first}}}});
                          return !opened;
                      });
    }
}
bool ScriptLanguageServices::request(LanguageFeature feature, const TextDocument& document,
                                     int cursor)
{
    const auto kind = category(document.path);
    auto* session = kind ? m_impl->sessions[*kind].get() : nullptr;
    return session && session->request(feature, document, cursor);
}
std::vector<LanguageReply> ScriptLanguageServices::takeReplies()
{
    std::vector<LanguageReply> result;
    for (auto& session : m_impl->sessions)
        if (session)
            while (!session->replies.empty())
            {
                result.push_back(std::move(session->replies.front()));
                session->replies.pop_front();
            }
    return result;
}
const std::vector<LspDiagnostic>&
ScriptLanguageServices::diagnostics(const std::filesystem::path& path) const
{
    static const std::vector<LspDiagnostic> empty;
    const auto kind = category(path);
    const auto* session = kind ? m_impl->sessions[*kind].get() : nullptr;
    if (!session)
        return empty;
    const auto found = session->buffers.find(lspUri(path));
    return found == session->buffers.end() || found->second.dirty ? empty
                                                                  : found->second.diagnostics;
}
std::string ScriptLanguageServices::status(const std::filesystem::path& path) const
{
    const auto kind = category(path);
    const auto* session = kind ? m_impl->sessions[*kind].get() : nullptr;
    return session ? session->state : std::string{};
}
bool ScriptLanguageServices::ready(const std::filesystem::path& path) const
{
    const auto kind = category(path);
    const auto* session = kind ? m_impl->sessions[*kind].get() : nullptr;
    return session && session->initialized;
}

} // namespace devex::tools::detail
