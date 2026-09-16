#include <devex/asset/MaterialData.hpp>

namespace devex::asset {

std::string_view toString(AlphaMode mode) noexcept
{
    switch (mode)
    {
    case AlphaMode::Opaque:
        return "opaque";
    case AlphaMode::Mask:
        return "mask";
    case AlphaMode::Blend:
        return "blend";
    }
    return "unknown";
}

std::optional<AlphaMode> parseAlphaMode(std::string_view text) noexcept
{
    for (const AlphaMode mode : {AlphaMode::Opaque, AlphaMode::Mask, AlphaMode::Blend})
    {
        if (toString(mode) == text)
        {
            return mode;
        }
    }
    return std::nullopt;
}

} // namespace devex::asset
