#include <devex/core/Uuid.hpp>

#include <random>

namespace devex::core {
namespace {

constexpr std::string_view hexDigits = "0123456789abcdef";

// Positions of the dashes in the canonical form.
[[nodiscard]] constexpr bool isDashPosition(std::size_t position) noexcept
{
    return position == 8 || position == 13 || position == 18 || position == 23;
}

[[nodiscard]] std::optional<std::uint8_t> hexValue(char character) noexcept
{
    if (character >= '0' && character <= '9')
    {
        return static_cast<std::uint8_t>(character - '0');
    }
    if (character >= 'a' && character <= 'f')
    {
        return static_cast<std::uint8_t>(character - 'a' + 10);
    }
    if (character >= 'A' && character <= 'F')
    {
        return static_cast<std::uint8_t>(character - 'A' + 10);
    }
    return std::nullopt;
}

} // namespace

Uuid Uuid::generate()
{
    thread_local std::mt19937_64 engine = [] {
        std::random_device device;
        std::seed_seq seed{device(), device(), device(), device()};
        return std::mt19937_64(seed);
    }();

    // Version 4 (random) with the RFC 4122 variant.
    const std::uint64_t high = (engine() & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    const std::uint64_t low = (engine() & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    return fromParts(high, low);
}

std::optional<Uuid> Uuid::parse(std::string_view text) noexcept
{
    if (text.size() != 36)
    {
        return std::nullopt;
    }

    Uuid uuid;
    std::size_t byte = 0;
    for (std::size_t position = 0; position < text.size();)
    {
        if (isDashPosition(position))
        {
            if (text[position] != '-')
            {
                return std::nullopt;
            }
            ++position;
            continue;
        }
        const std::optional<std::uint8_t> high = hexValue(text[position]);
        const std::optional<std::uint8_t> low = hexValue(text[position + 1]);
        if (!high || !low || isDashPosition(position + 1))
        {
            return std::nullopt;
        }
        uuid.m_bytes[byte++] = static_cast<std::uint8_t>(*high << 4 | *low);
        position += 2;
    }
    return uuid;
}

std::string Uuid::toString() const
{
    std::string text;
    text.reserve(36);
    for (std::size_t byte = 0; byte < m_bytes.size(); ++byte)
    {
        if (byte == 4 || byte == 6 || byte == 8 || byte == 10)
        {
            text.push_back('-');
        }
        text.push_back(hexDigits[m_bytes[byte] >> 4]);
        text.push_back(hexDigits[m_bytes[byte] & 0x0F]);
    }
    return text;
}

} // namespace devex::core
