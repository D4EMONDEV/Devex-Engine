#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace devex::core {

// 64-bit XXH64 hash, identical to the reference implementation. Fast and well distributed, but not
// cryptographic: it detects changes, it does not authenticate content.
[[nodiscard]] std::uint64_t hash64(std::span<const std::byte> bytes, std::uint64_t seed = 0) noexcept;

[[nodiscard]] inline std::uint64_t hash64(std::string_view text, std::uint64_t seed = 0) noexcept
{
    return hash64(std::as_bytes(std::span(text.data(), text.size())), seed);
}

// Sixteen lowercase hexadecimal digits, as written in text files.
[[nodiscard]] std::string toHex(std::uint64_t value);

} // namespace devex::core
