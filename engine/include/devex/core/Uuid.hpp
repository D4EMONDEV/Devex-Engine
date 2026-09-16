#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace devex::core {

// 128-bit universally unique identifier, written as "6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23".
class Uuid
{
public:
    constexpr Uuid() noexcept = default;

    // Builds an identifier from its two halves, most significant first. Meant for identifiers
    // reserved by the engine; use generate() otherwise.
    [[nodiscard]] static constexpr Uuid fromParts(std::uint64_t high, std::uint64_t low) noexcept
    {
        Uuid uuid;
        for (std::size_t index = 0; index < 8; ++index)
        {
            uuid.m_bytes[index] = static_cast<std::uint8_t>(high >> (56 - 8 * index));
            uuid.m_bytes[8 + index] = static_cast<std::uint8_t>(low >> (56 - 8 * index));
        }
        return uuid;
    }

    // Returns a random version 4 identifier.
    [[nodiscard]] static Uuid generate();

    // Accepts the canonical 36-character form, in lower or upper case.
    [[nodiscard]] static std::optional<Uuid> parse(std::string_view text) noexcept;

    [[nodiscard]] std::string toString() const;

    [[nodiscard]] constexpr bool isNil() const noexcept
    {
        return *this == Uuid{};
    }

    [[nodiscard]] constexpr const std::array<std::uint8_t, 16>& bytes() const noexcept
    {
        return m_bytes;
    }

    constexpr auto operator<=>(const Uuid&) const noexcept = default;

private:
    std::array<std::uint8_t, 16> m_bytes{};
};

} // namespace devex::core

template <>
struct std::hash<devex::core::Uuid>
{
    [[nodiscard]] std::size_t operator()(const devex::core::Uuid& uuid) const noexcept
    {
        // The bytes are already uniformly distributed for generated identifiers.
        std::uint64_t value = 0;
        for (const std::uint8_t byte : uuid.bytes())
        {
            value = value * 1099511628211ULL ^ byte;
        }
        return static_cast<std::size_t>(value);
    }
};

template <>
struct std::formatter<devex::core::Uuid> : std::formatter<std::string_view>
{
    auto format(const devex::core::Uuid& uuid, std::format_context& context) const
    {
        return std::formatter<std::string_view>::format(uuid.toString(), context);
    }
};
