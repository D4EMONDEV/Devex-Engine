#pragma once

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Base64 (RFC 4648, with padding): bytes written as text, for binary data kept in text files.
namespace devex::core {

[[nodiscard]] std::string encodeBase64(std::span<const std::byte> bytes);
// Nothing when the text is not base64: a character outside the alphabet, or a wrong length.
[[nodiscard]] std::optional<std::vector<std::byte>> decodeBase64(std::string_view text);

} // namespace devex::core
