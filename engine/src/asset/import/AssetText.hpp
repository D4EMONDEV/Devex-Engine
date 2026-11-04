#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/serialization/Text.hpp>

#include <optional>
#include <string>

namespace devex::asset {

// An asset as text files name it: asset("<uuid>").
[[nodiscard]] inline std::optional<AssetId> readAsset(const serialization::TextValue& value)
{
    const serialization::TextCall* const call = serialization::asCall(value, "asset");
    if (call == nullptr || call->arguments.size() != 1)
    {
        return std::nullopt;
    }
    const std::string* const text = serialization::asString(call->arguments.front());
    const std::optional<core::Uuid> uuid = text != nullptr ? core::Uuid::parse(*text) : std::nullopt;
    return uuid ? std::optional(AssetId{*uuid}) : std::nullopt;
}

} // namespace devex::asset
