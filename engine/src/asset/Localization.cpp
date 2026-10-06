#include <devex/asset/Localization.hpp>

#include <algorithm>

namespace devex::asset {

void Localization::setTables(std::span<const std::shared_ptr<const TranslationData>> tables)
{
    m_languages.clear();
    for (const std::shared_ptr<const TranslationData>& table : tables)
    {
        if (table == nullptr)
        {
            continue;
        }
        for (const std::string& language : table->languages)
        {
            if (std::ranges::find(m_languages, language) == m_languages.end())
            {
                m_languages.push_back(language);
            }
        }
    }
    std::ranges::sort(m_languages);

    m_keys.clear();
    m_messages.clear();
    const std::size_t width = m_languages.size();
    for (const std::shared_ptr<const TranslationData>& table : tables)
    {
        if (table == nullptr)
        {
            continue;
        }
        std::vector<std::size_t> columns;
        for (const std::string& language : table->languages)
        {
            columns.push_back(static_cast<std::size_t>(std::ranges::find(m_languages, language) - m_languages.begin()));
        }
        for (std::size_t key = 0; key < table->keys.size(); ++key)
        {
            const auto [found, added] = m_keys.try_emplace(table->keys[key], m_keys.size());
            if (added)
            {
                m_messages.resize(m_messages.size() + width);
            }
            for (std::size_t language = 0; language < columns.size(); ++language)
            {
                std::string& message = m_messages[found->second * width + columns[language]];
                if (message.empty())
                {
                    message = table->message(key, language);
                }
            }
        }
    }
    // The language asked for may now be closer to one of the new tables.
    if (!m_language.empty())
    {
        m_language = closestLanguage(m_language).value_or(m_language);
    }
    updateChain();
}

void Localization::setFallbackLanguage(std::string_view language)
{
    m_fallback = normalizeLanguage(language);
    updateChain();
}

const std::string& Localization::fallbackLanguage() const noexcept
{
    return m_fallback;
}

const std::vector<std::string>& Localization::languages() const noexcept
{
    return m_languages;
}

std::optional<std::string> Localization::closestLanguage(std::string_view wanted) const
{
    const std::string language = normalizeLanguage(wanted);
    if (language.empty())
    {
        return std::nullopt;
    }
    if (std::ranges::find(m_languages, language) != m_languages.end())
    {
        return language;
    }
    const std::string_view base = baseLanguage(language);
    for (const std::string& candidate : m_languages)
    {
        if (candidate == base)
        {
            return candidate;
        }
    }
    for (const std::string& candidate : m_languages)
    {
        if (baseLanguage(candidate) == base)
        {
            return candidate;
        }
    }
    return std::nullopt;
}

void Localization::setLanguage(std::string_view language)
{
    m_language = language.empty() ? std::string{} : closestLanguage(language).value_or(normalizeLanguage(language));
    updateChain();
}

const std::string& Localization::language() const noexcept
{
    return m_language;
}

const std::string* Localization::find(std::string_view key) const
{
    if (m_chain.empty())
    {
        return nullptr;
    }
    const auto found = m_keys.find(key);
    if (found == m_keys.end())
    {
        return nullptr;
    }
    for (const std::size_t column : m_chain)
    {
        const std::string& message = m_messages[found->second * m_languages.size() + column];
        if (!message.empty())
        {
            return &message;
        }
    }
    return nullptr;
}

std::string_view Localization::translate(std::string_view key) const
{
    const std::string* const message = find(key);
    return message != nullptr ? std::string_view(*message) : key;
}

std::string Localization::translate(std::string_view key, std::span<const TranslationValue> values) const
{
    const std::string_view text = translate(key);
    std::string result;
    result.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const char character = text[index];
        if ((character == '{' || character == '}') && index + 1 < text.size() && text[index + 1] == character)
        {
            result += character;
            ++index;
            continue;
        }
        if (character == '{')
        {
            const std::size_t end = text.find('}', index + 1);
            if (end != std::string_view::npos)
            {
                const std::string_view name = text.substr(index + 1, end - index - 1);
                const auto value = std::ranges::find(values, name, &TranslationValue::name);
                if (value != values.end())
                {
                    result += value->value;
                    index = end;
                    continue;
                }
            }
        }
        result += character;
    }
    return result;
}

std::uint64_t Localization::revision() const noexcept
{
    return m_revision;
}

std::optional<std::size_t> Localization::columnOf(std::string_view language) const
{
    const auto found = std::ranges::find(m_languages, language);
    return found != m_languages.end() ? std::optional(static_cast<std::size_t>(found - m_languages.begin())) : std::nullopt;
}

void Localization::updateChain()
{
    m_chain.clear();
    if (!m_language.empty())
    {
        for (const std::string& wanted : {m_language, m_fallback})
        {
            const std::optional<std::string> closest = closestLanguage(wanted);
            const std::optional<std::size_t> column = closest ? columnOf(*closest) : std::nullopt;
            if (column && std::ranges::find(m_chain, *column) == m_chain.end())
            {
                m_chain.push_back(*column);
            }
        }
    }
    ++m_revision;
}

} // namespace devex::asset
