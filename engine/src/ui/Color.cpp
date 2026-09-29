#include <devex/ui/Color.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

namespace devex::ui {

float srgbFromLinear(float linear) noexcept
{
    const float value = std::max(linear, 0.0f);
    return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
}

float linearFromSrgb(float srgb) noexcept
{
    const float value = std::max(srgb, 0.0f);
    return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

math::Vec4 srgbFromLinear(math::Vec4 linear) noexcept
{
    return math::Vec4{srgbFromLinear(linear.x), srgbFromLinear(linear.y), srgbFromLinear(linear.z), linear.w};
}

math::Vec4 linearFromSrgb(math::Vec4 srgb) noexcept
{
    return math::Vec4{linearFromSrgb(srgb.x), linearFromSrgb(srgb.y), linearFromSrgb(srgb.z), srgb.w};
}

math::Vec3 hsvFromRgb(math::Vec3 rgb) noexcept
{
    const float high = std::max({rgb.x, rgb.y, rgb.z});
    const float low = std::min({rgb.x, rgb.y, rgb.z});
    const float range = high - low;
    float hue = 0.0f;
    if (range > 0.0f)
    {
        if (high == rgb.x)
        {
            hue = std::fmod((rgb.y - rgb.z) / range + 6.0f, 6.0f);
        }
        else if (high == rgb.y)
        {
            hue = (rgb.z - rgb.x) / range + 2.0f;
        }
        else
        {
            hue = (rgb.x - rgb.y) / range + 4.0f;
        }
        hue /= 6.0f;
    }
    return math::Vec3{hue, high > 0.0f ? range / high : 0.0f, high};
}

math::Vec3 rgbFromHsv(math::Vec3 hsv) noexcept
{
    const float hue = (hsv.x - std::floor(hsv.x)) * 6.0f;
    const float saturation = std::clamp(hsv.y, 0.0f, 1.0f);
    const float value = std::max(hsv.z, 0.0f);
    const auto channel = [&](float offset) {
        const float k = std::fmod(offset + hue, 6.0f);
        return value - value * saturation * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f);
    };
    return math::Vec3{channel(5.0f), channel(3.0f), channel(1.0f)};
}

std::string hexFromColor(math::Vec4 srgb, bool alpha)
{
    const auto byte = [](float value) { return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
    return alpha ? std::format("{:02X}{:02X}{:02X}{:02X}", byte(srgb.x), byte(srgb.y), byte(srgb.z), byte(srgb.w))
                 : std::format("{:02X}{:02X}{:02X}", byte(srgb.x), byte(srgb.y), byte(srgb.z));
}

std::optional<math::Vec4> colorFromHex(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '#'))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == ' ')
    {
        text.remove_suffix(1);
    }
    const auto digit = [](char letter) -> int {
        if (letter >= '0' && letter <= '9')
        {
            return letter - '0';
        }
        if (letter >= 'a' && letter <= 'f')
        {
            return letter - 'a' + 10;
        }
        if (letter >= 'A' && letter <= 'F')
        {
            return letter - 'A' + 10;
        }
        return -1;
    };
    if (std::ranges::any_of(text, [&](char letter) { return digit(letter) < 0; }))
    {
        return std::nullopt;
    }
    std::array<float, 4> channels{0.0f, 0.0f, 0.0f, 1.0f};
    if (text.size() == 3 || text.size() == 4)
    {
        // One digit a channel, written twice: F80 is FF8800.
        for (std::size_t index = 0; index < text.size(); ++index)
        {
            channels[index] = static_cast<float>(digit(text[index]) * 17) / 255.0f;
        }
    }
    else if (text.size() == 6 || text.size() == 8)
    {
        for (std::size_t index = 0; index < text.size() / 2; ++index)
        {
            channels[index] = static_cast<float>(digit(text[index * 2]) * 16 + digit(text[index * 2 + 1])) / 255.0f;
        }
    }
    else
    {
        return std::nullopt;
    }
    return math::Vec4{channels[0], channels[1], channels[2], channels[3]};
}

} // namespace devex::ui
