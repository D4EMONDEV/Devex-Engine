#pragma once

#include <devex/serialization/Text.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace devex::runtime {

// What the player chose for the game, kept from one game to the next: the volumes, the window, and
// the values the game keeps by name, such as a language or the sensitivity of the mouse. The engine
// applies the volumes and the window itself; the game reads and writes the rest.
class PlayerSettings
{
public:
    // The name of the volume of every sound, over those of the groups.
    static constexpr std::string_view master = "Master";

    // Nothing while the player has not chosen, so that the project decides.
    [[nodiscard]] std::optional<bool> fullscreen() const noexcept;
    void setFullscreen(bool fullscreen);
    [[nodiscard]] std::optional<bool> vsync() const noexcept;
    void setVsync(bool vsync);

    // From 0 to 1, for Master or an audio group; 1 while the player has not chosen.
    [[nodiscard]] float volume(std::string_view group) const;
    void setVolume(std::string_view group, float volume);
    [[nodiscard]] const std::map<std::string, float, std::less<>>& volumes() const noexcept;

    // The values of the game, which a key names: booleans, integers, numbers and texts.
    [[nodiscard]] const serialization::TextValue* value(std::string_view key) const;
    void setValue(std::string_view key, serialization::TextValue value);
    bool removeValue(std::string_view key);
    [[nodiscard]] bool boolValue(std::string_view key, bool fallback) const;
    [[nodiscard]] std::int64_t integerValue(std::string_view key, std::int64_t fallback) const;
    [[nodiscard]] double numberValue(std::string_view key, double fallback) const;
    [[nodiscard]] std::string stringValue(std::string_view key, std::string_view fallback) const;

    // Whether anything changed since the last call, and the settings should be written.
    [[nodiscard]] bool takeChanges() noexcept;
    // The text of the settings file, and back; what cannot be read is left out with a warning.
    [[nodiscard]] std::string write() const;
    void read(std::string_view text);

private:
    std::optional<bool> m_fullscreen;
    std::optional<bool> m_vsync;
    std::map<std::string, float, std::less<>> m_volumes;
    std::map<std::string, serialization::TextValue, std::less<>> m_values;
    bool m_changed = false;
};

} // namespace devex::runtime
