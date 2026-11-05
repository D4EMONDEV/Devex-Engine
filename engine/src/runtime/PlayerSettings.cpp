#include <devex/runtime/PlayerSettings.hpp>

#include <devex/core/Log.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace devex::runtime {

std::optional<bool> PlayerSettings::fullscreen() const noexcept
{
    return m_windowMode ? std::optional(*m_windowMode != platform::WindowMode::Windowed) : std::nullopt;
}

void PlayerSettings::setFullscreen(bool fullscreen)
{
    if (fullscreen != this->fullscreen())
    {
        setWindowMode(fullscreen ? platform::WindowMode::Borderless : platform::WindowMode::Windowed);
    }
}

std::optional<platform::WindowMode> PlayerSettings::windowMode() const noexcept
{
    return m_windowMode;
}

void PlayerSettings::setWindowMode(platform::WindowMode mode)
{
    m_changed = m_changed || m_windowMode != mode;
    m_windowMode = mode;
}

std::optional<std::uint32_t> PlayerSettings::maxFrameRate() const noexcept
{
    return m_maxFrameRate;
}

void PlayerSettings::setMaxFrameRate(std::uint32_t framesPerSecond)
{
    m_changed = m_changed || m_maxFrameRate != framesPerSecond;
    m_maxFrameRate = framesPerSecond;
}

std::optional<math::Extent2D> PlayerSettings::windowSize() const noexcept
{
    return m_windowSize;
}

void PlayerSettings::setWindowSize(math::Extent2D size)
{
    m_changed = m_changed || !m_windowSize || m_windowSize->width != size.width || m_windowSize->height != size.height;
    m_windowSize = size;
}

std::optional<bool> PlayerSettings::vsync() const noexcept
{
    return m_vsync;
}

void PlayerSettings::setVsync(bool vsync)
{
    m_changed = m_changed || m_vsync != vsync;
    m_vsync = vsync;
}

const std::optional<std::string>& PlayerSettings::language() const noexcept
{
    return m_language;
}

void PlayerSettings::setLanguage(std::string language)
{
    m_changed = m_changed || m_language != language;
    m_language = std::move(language);
}

float PlayerSettings::volume(std::string_view group) const
{
    const auto found = m_volumes.find(group);
    return found != m_volumes.end() ? found->second : 1.0f;
}

void PlayerSettings::setVolume(std::string_view group, float volume)
{
    if (group.empty())
    {
        return;
    }
    volume = std::clamp(std::isfinite(volume) ? volume : 1.0f, 0.0f, 1.0f);
    const auto found = m_volumes.find(group);
    if (found != m_volumes.end() && found->second == volume)
    {
        return;
    }
    m_volumes.insert_or_assign(std::string(group), volume);
    m_changed = true;
}

const std::map<std::string, float, std::less<>>& PlayerSettings::volumes() const noexcept
{
    return m_volumes;
}

const serialization::TextValue* PlayerSettings::value(std::string_view key) const
{
    const auto found = m_values.find(key);
    return found != m_values.end() ? &found->second : nullptr;
}

void PlayerSettings::setValue(std::string_view key, serialization::TextValue value)
{
    if (key.empty() || std::holds_alternative<serialization::TextCall>(value))
    {
        return;
    }
    const auto found = m_values.find(key);
    if (found != m_values.end() && found->second == value)
    {
        return;
    }
    m_values.insert_or_assign(std::string(key), std::move(value));
    m_changed = true;
}

bool PlayerSettings::removeValue(std::string_view key)
{
    const auto found = m_values.find(key);
    if (found == m_values.end())
    {
        return false;
    }
    m_values.erase(found);
    m_changed = true;
    return true;
}

bool PlayerSettings::boolValue(std::string_view key, bool fallback) const
{
    const serialization::TextValue* const found = value(key);
    return found != nullptr ? serialization::asBool(*found).value_or(fallback) : fallback;
}

std::int64_t PlayerSettings::integerValue(std::string_view key, std::int64_t fallback) const
{
    const serialization::TextValue* const found = value(key);
    if (found == nullptr)
    {
        return fallback;
    }
    if (const std::optional<std::int64_t> integer = serialization::asInteger(*found))
    {
        return *integer;
    }
    const std::optional<double> number = serialization::asNumber(*found);
    return number && std::isfinite(*number) ? static_cast<std::int64_t>(std::llround(*number)) : fallback;
}

double PlayerSettings::numberValue(std::string_view key, double fallback) const
{
    const serialization::TextValue* const found = value(key);
    return found != nullptr ? serialization::asNumber(*found).value_or(fallback) : fallback;
}

std::string PlayerSettings::stringValue(std::string_view key, std::string_view fallback) const
{
    const serialization::TextValue* const found = value(key);
    const std::string* const text = found != nullptr ? serialization::asString(*found) : nullptr;
    return text != nullptr ? *text : std::string(fallback);
}

bool PlayerSettings::takeChanges() noexcept
{
    return std::exchange(m_changed, false);
}

std::string PlayerSettings::write() const
{
    serialization::TextDocument document;
    serialization::TextSection& header = document.sections.emplace_back();
    header.type = "settings";
    header.attributes.push_back({"format", serialization::TextValue(std::int64_t{1})});
    if (m_windowMode)
    {
        const std::string_view name = *m_windowMode == platform::WindowMode::Fullscreen ? "fullscreen" : (*m_windowMode == platform::WindowMode::Borderless ? "borderless" : "windowed");
        header.attributes.push_back({"window_mode", serialization::TextValue(std::string(name))});
    }
    if (m_vsync)
    {
        header.attributes.push_back({"vsync", serialization::TextValue(*m_vsync)});
    }
    if (m_maxFrameRate)
    {
        header.attributes.push_back({"max_frame_rate", serialization::TextValue(static_cast<std::int64_t>(*m_maxFrameRate))});
    }
    if (m_windowSize)
    {
        header.attributes.push_back({"window_width", serialization::TextValue(static_cast<std::int64_t>(m_windowSize->width))});
        header.attributes.push_back({"window_height", serialization::TextValue(static_cast<std::int64_t>(m_windowSize->height))});
    }
    if (m_language)
    {
        header.attributes.push_back({"language", serialization::TextValue(*m_language)});
    }
    for (const auto& [group, volume] : m_volumes)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "volume";
        section.attributes.push_back({"group", serialization::TextValue(group)});
        // The shortest text of the float, not of the double it widens to: 0.28 rather than 0.2800000011.
        section.attributes.push_back({"value", serialization::TextValue(std::stod(std::format("{}", volume)))});
    }
    for (const auto& [key, value] : m_values)
    {
        serialization::TextSection& section = document.sections.emplace_back();
        section.type = "value";
        section.attributes.push_back({"key", serialization::TextValue(key)});
        section.attributes.push_back({"value", value});
    }
    return serialization::writeText(document);
}

void PlayerSettings::read(std::string_view text)
{
    const core::Result<serialization::TextDocument> document = serialization::parseText(text);
    if (!document || document->sections.empty() || document->sections.front().type != "settings")
    {
        DEVEX_LOG_WARNING("The settings of the player cannot be read: the game starts with its own");
        return;
    }
    const serialization::TextSection& header = document->sections.front();
    // Before there were three modes, full screen meant what is now borderless.
    if (const serialization::TextValue* const fullscreen = header.findAttribute("fullscreen"))
    {
        if (const std::optional<bool> chosen = serialization::asBool(*fullscreen))
        {
            m_windowMode = *chosen ? platform::WindowMode::Borderless : platform::WindowMode::Windowed;
        }
    }
    if (const std::string* const mode = serialization::stringAttribute(header, "window_mode"))
    {
        m_windowMode = *mode == "fullscreen" ? platform::WindowMode::Fullscreen : (*mode == "borderless" ? platform::WindowMode::Borderless : platform::WindowMode::Windowed);
    }
    if (const serialization::TextValue* const vsync = header.findAttribute("vsync"))
    {
        m_vsync = serialization::asBool(*vsync);
    }
    if (const serialization::TextValue* const rate = header.findAttribute("max_frame_rate"))
    {
        if (const std::optional<std::int64_t> value = serialization::asInteger(*rate); value && *value >= 0)
        {
            m_maxFrameRate = static_cast<std::uint32_t>(*value);
        }
    }
    const serialization::TextValue* const width = header.findAttribute("window_width");
    const serialization::TextValue* const height = header.findAttribute("window_height");
    if (width != nullptr && height != nullptr)
    {
        const std::optional<std::int64_t> wide = serialization::asInteger(*width);
        const std::optional<std::int64_t> high = serialization::asInteger(*height);
        if (wide && high && *wide > 0 && *high > 0)
        {
            m_windowSize = math::Extent2D{static_cast<std::uint32_t>(*wide), static_cast<std::uint32_t>(*high)};
        }
    }
    if (const serialization::TextValue* const language = header.findAttribute("language"))
    {
        if (const std::string* const code = serialization::asString(*language); code != nullptr && !code->empty())
        {
            m_language = *code;
        }
    }
    for (const serialization::TextSection& section : document->sections)
    {
        if (section.type == "volume")
        {
            const serialization::TextValue* const group = section.findAttribute("group");
            const serialization::TextValue* const volume = section.findAttribute("value");
            const std::string* const name = group != nullptr ? serialization::asString(*group) : nullptr;
            const std::optional<double> amount = volume != nullptr ? serialization::asNumber(*volume) : std::nullopt;
            if (name != nullptr && !name->empty() && amount)
            {
                m_volumes.insert_or_assign(*name, std::clamp(static_cast<float>(*amount), 0.0f, 1.0f));
            }
        }
        else if (section.type == "value")
        {
            const serialization::TextValue* const key = section.findAttribute("key");
            const serialization::TextValue* const value = section.findAttribute("value");
            const std::string* const name = key != nullptr ? serialization::asString(*key) : nullptr;
            if (name != nullptr && !name->empty() && value != nullptr && !std::holds_alternative<serialization::TextCall>(*value))
            {
                m_values.insert_or_assign(*name, *value);
            }
        }
    }
}

} // namespace devex::runtime
