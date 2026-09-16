#include <devex/core/Hash.hpp>

#include <bit>
#include <cstring>
#include <format>

namespace devex::core {
namespace {

constexpr std::uint64_t prime1 = 0x9E3779B185EBCA87ULL;
constexpr std::uint64_t prime2 = 0xC2B2AE3D27D4EB4FULL;
constexpr std::uint64_t prime3 = 0x165667B19E3779F9ULL;
constexpr std::uint64_t prime4 = 0x85EBCA77C2B2AE63ULL;
constexpr std::uint64_t prime5 = 0x27D4EB2F165667C5ULL;

// The reference implementation reads little-endian words.
static_assert(std::endian::native == std::endian::little);

[[nodiscard]] std::uint64_t read64(const std::byte* data) noexcept
{
    std::uint64_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

[[nodiscard]] std::uint32_t read32(const std::byte* data) noexcept
{
    std::uint32_t value = 0;
    std::memcpy(&value, data, sizeof(value));
    return value;
}

[[nodiscard]] constexpr std::uint64_t round(std::uint64_t accumulator, std::uint64_t input) noexcept
{
    accumulator += input * prime2;
    accumulator = std::rotl(accumulator, 31);
    return accumulator * prime1;
}

[[nodiscard]] constexpr std::uint64_t mergeRound(std::uint64_t hash, std::uint64_t value) noexcept
{
    hash ^= round(0, value);
    return hash * prime1 + prime4;
}

} // namespace

std::uint64_t hash64(std::span<const std::byte> bytes, std::uint64_t seed) noexcept
{
    const std::byte* data = bytes.data();
    std::size_t remaining = bytes.size();
    std::uint64_t hash = 0;

    if (remaining >= 32)
    {
        std::uint64_t v1 = seed + prime1 + prime2;
        std::uint64_t v2 = seed + prime2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - prime1;
        do
        {
            v1 = round(v1, read64(data));
            v2 = round(v2, read64(data + 8));
            v3 = round(v3, read64(data + 16));
            v4 = round(v4, read64(data + 24));
            data += 32;
            remaining -= 32;
        } while (remaining >= 32);

        hash = std::rotl(v1, 1) + std::rotl(v2, 7) + std::rotl(v3, 12) + std::rotl(v4, 18);
        hash = mergeRound(hash, v1);
        hash = mergeRound(hash, v2);
        hash = mergeRound(hash, v3);
        hash = mergeRound(hash, v4);
    }
    else
    {
        hash = seed + prime5;
    }

    hash += static_cast<std::uint64_t>(bytes.size());
    for (; remaining >= 8; remaining -= 8, data += 8)
    {
        hash ^= round(0, read64(data));
        hash = std::rotl(hash, 27) * prime1 + prime4;
    }
    if (remaining >= 4)
    {
        hash ^= static_cast<std::uint64_t>(read32(data)) * prime1;
        hash = std::rotl(hash, 23) * prime2 + prime3;
        data += 4;
        remaining -= 4;
    }
    for (; remaining > 0; --remaining, ++data)
    {
        hash ^= static_cast<std::uint64_t>(*data) * prime5;
        hash = std::rotl(hash, 11) * prime1;
    }

    hash ^= hash >> 33;
    hash *= prime2;
    hash ^= hash >> 29;
    hash *= prime3;
    hash ^= hash >> 32;
    return hash;
}

std::string toHex(std::uint64_t value)
{
    return std::format("{:016x}", value);
}

} // namespace devex::core
