#pragma once

#include "CodeCompletion.hpp"
#include "LspProtocol.hpp"
#include "ScriptSettings.hpp"
#include "TextDocument.hpp"

#include <memory>
#include <span>
#include <vector>

namespace devex::tools::detail
{

enum class LanguageFeature
{
    Completion,
    Definition,
    References,
    Hover,
    Format
};
struct LspDiagnostic
{
    LspPosition start;
    LspPosition end;
    int severity = 1;
    std::string message;
};
struct LspLocation
{
    std::filesystem::path path;
    LspPosition position;
};
struct LanguageReply
{
    LanguageFeature feature = LanguageFeature::Hover;
    std::filesystem::path path;
    std::string text;
    int cursor = 0;
    std::vector<CompletionItem> completions;
    std::vector<CodeTextEdit> edits;
    std::vector<LspLocation> locations;
    std::string message;
};

// Each language owns one supervised process per project. Polling and pipe transfers are bounded;
// no compiler, build or save runs when an unsaved buffer changes.
class DEVEX_API ScriptLanguageServices
{
  public:
    explicit ScriptLanguageServices(std::filesystem::path programDirectory = {});
    ~ScriptLanguageServices();
    ScriptLanguageServices(const ScriptLanguageServices&) = delete;
    ScriptLanguageServices& operator=(const ScriptLanguageServices&) = delete;
    void update(const std::filesystem::path& project, const ScriptSettings& settings,
                std::span<const TextDocument> documents);
    bool request(LanguageFeature feature, const TextDocument& document, int cursor);
    [[nodiscard]] std::vector<LanguageReply> takeReplies();
    [[nodiscard]] const std::vector<LspDiagnostic>&
    diagnostics(const std::filesystem::path& path) const;
    [[nodiscard]] std::string status(const std::filesystem::path& path) const;
    [[nodiscard]] bool ready(const std::filesystem::path& path) const;
    void restart();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

[[nodiscard]] DEVEX_API std::filesystem::path
detectLanguageServer(std::size_t language, const std::filesystem::path& programDirectory = {});

} // namespace devex::tools::detail
