#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using Json = nlohmann::json;
void send(Json value)
{
    const auto text = value.dump();
    const std::string frame = "Content-Length: " + std::to_string(text.size()) + "\r\n\r\n" + text;
    // Deliberately fragment headers and JSON; stderr must never reach the protocol parser.
    std::cerr << "probe diagnostic on stderr\n";
    for (std::size_t at = 0; at < frame.size(); at += 7)
        std::cout.write(frame.data() + at,
                        static_cast<std::streamsize>(std::min<std::size_t>(7, frame.size() - at)));
    std::cout.flush();
}
int main()
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string uri;
    std::string current;
    int version = 0;
    for (std::string header; std::getline(std::cin, header);)
    {
        if (!header.starts_with("Content-Length: "))
            continue;
        const auto length = static_cast<std::size_t>(std::stoul(header.substr(16)));
        std::getline(std::cin, header);
        std::string body(length, '\0');
        std::cin.read(body.data(), static_cast<std::streamsize>(length));
        const auto message = Json::parse(body);
        const std::string method = message.value("method", "");
        const auto params = message.value("params", Json::object());
        const auto doc = params.value("textDocument", Json::object());
        if (method == "initialize")
        {
            send({{"jsonrpc", "2.0"},
                  {"id", message["id"]},
                  {"result",
                   {{"capabilities",
                     {{"textDocumentSync", 2},
                      {"completionProvider", Json::object()},
                      {"definitionProvider", true},
                      {"referencesProvider", true},
                      {"hoverProvider", true},
                      {"documentFormattingProvider", true}}}}}});
            send({{"jsonrpc", "2.0"},
                  {"id", "configuration"},
                  {"method", "workspace/configuration"},
                  {"params", {{"items", Json::array({Json::object()})}}}});
        }
        else if (method == "textDocument/didOpen")
        {
            uri = doc.value("uri", "");
            current = doc.value("text", "");
            version = doc.value("version", 0);
            send({{"jsonrpc", "2.0"},
                  {"method", "textDocument/publishDiagnostics"},
                  {"params",
                   {{"uri", uri},
                    {"version", version},
                    {"diagnostics", Json::array({{{"range",
                                                   {{"start", {{"line", 0}, {"character", 0}}},
                                                    {"end", {{"line", 0}, {"character", 2}}}}},
                                                  {"severity", 2},
                                                  {"message", "live buffer warning"}}})}}}});
        }
        else if (method == "textDocument/didChange")
        {
            current = params["contentChanges"][0].value("text", "");
            version = doc.value("version", 0);
            send({{"jsonrpc", "2.0"},
                  {"method", "textDocument/publishDiagnostics"},
                  {"params",
                   {{"uri", uri},
                    {"version", version - 1},
                    {"diagnostics",
                     Json::array({{{"message", "stale warning"}, {"severity", 1}}})}}}});
            send(
                {{"jsonrpc", "2.0"},
                 {"method", "textDocument/publishDiagnostics"},
                 {"params", {{"uri", uri}, {"version", version}, {"diagnostics", Json::array()}}}});
        }
        else if (message.contains("id") && !method.empty())
        {
            Json result;
            if (method == "textDocument/completion")
                result = Json::array({{{"label", current + "Completed"},
                                       {"insertText", current},
                                       {"detail", "probe member"}}});
            else if (method == "textDocument/hover")
                result = {
                    {"contents",
                     {{"kind", "markdown"}, {"value", "```csharp\nMember information\n```"}}}};
            else if (method == "textDocument/definition" || method == "textDocument/references")
                result = Json::array({{{"uri", uri},
                                       {"range",
                                        {{"start", {{"line", 0}, {"character", 1}}},
                                         {"end", {{"line", 0}, {"character", 2}}}}}}});
            else if (method == "textDocument/formatting")
                result = Json::array({{{"range",
                                        {{"start", {{"line", 0}, {"character", 0}}},
                                         {"end", {{"line", 0}, {"character", 1}}}}},
                                       {"newText", "F"}}});
            else if (method == "shutdown")
            {
                send({{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", nullptr}});
                continue;
            }
            send({{"jsonrpc", "2.0"}, {"id", message["id"]}, {"result", result}});
        }
        else if (method == "exit")
            return 0;
    }
    return 0;
}
