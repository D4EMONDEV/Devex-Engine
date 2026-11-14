#include "BlockDecoding.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace devex::render::webgpu {
namespace {

// The tables of the BC7 specification: the subset of each pixel for the 64 partitions of two and three subsets, and
// the pixels whose index has one bit less, the anchors of the second and third subsets.
constexpr std::array<std::uint8_t, 1024> partitions2{
    0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
    0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1,
    0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1,
    0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 1, 1,
    0, 0, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1,
    0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 1,
    0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1,
    0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1,
    0, 1, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0,
    0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0,
    0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 1,
    0, 0, 1, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0,
    0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 0, 0,
    0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0,
    0, 0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1, 1, 0, 0,
    0, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    0, 1, 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0,
    0, 0, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
    0, 1, 0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0,
    0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0,
    0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 0, 0, 1,
    0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1,
    0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0,
    0, 0, 0, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 0, 0, 0,
    0, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1, 0, 0,
    0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 0, 0,
    0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0,
    0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1,
    0, 1, 1, 0, 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1,
    0, 0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0, 0,
    0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0,
    0, 0, 0, 0, 0, 1, 0, 0, 1, 1, 1, 0, 0, 1, 0, 0,
    0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1, 1,
    0, 0, 1, 1, 0, 1, 1, 0, 1, 1, 0, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0,
    0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 0,
    0, 1, 1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 1,
    0, 1, 1, 0, 0, 0, 1, 1, 0, 0, 1, 1, 1, 0, 0, 1,
    0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1,
    0, 0, 0, 1, 1, 0, 0, 0, 1, 1, 1, 0, 0, 1, 1, 1,
    0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1,
    0, 0, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
    0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0,
    0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1,
};

constexpr std::array<std::uint8_t, 1024> partitions3{
    0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 1, 2, 2, 2, 2,
    0, 0, 0, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 2, 1,
    0, 0, 0, 0, 2, 0, 0, 1, 2, 2, 1, 1, 2, 2, 1, 1,
    0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 1, 0, 1, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2,
    0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 2, 2,
    0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1,
    0, 0, 1, 1, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2,
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2,
    0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2, 0, 1, 1, 2,
    0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2, 0, 1, 2, 2,
    0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2, 1, 2, 2, 2,
    0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0, 2, 2, 2, 0,
    0, 0, 0, 1, 0, 0, 1, 1, 0, 1, 1, 2, 1, 1, 2, 2,
    0, 1, 1, 1, 0, 0, 1, 1, 2, 0, 0, 1, 2, 2, 0, 0,
    0, 0, 0, 0, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2,
    0, 0, 2, 2, 0, 0, 2, 2, 0, 0, 2, 2, 1, 1, 1, 1,
    0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2, 0, 2, 2, 2,
    0, 0, 0, 1, 0, 0, 0, 1, 2, 2, 2, 1, 2, 2, 2, 1,
    0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2,
    0, 0, 0, 0, 1, 1, 0, 0, 2, 2, 1, 0, 2, 2, 1, 0,
    0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1, 0, 0, 0, 0,
    0, 0, 1, 2, 0, 0, 1, 2, 1, 1, 2, 2, 2, 2, 2, 2,
    0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1, 0, 1, 1, 0,
    0, 0, 0, 0, 0, 1, 1, 0, 1, 2, 2, 1, 1, 2, 2, 1,
    0, 0, 2, 2, 1, 1, 0, 2, 1, 1, 0, 2, 0, 0, 2, 2,
    0, 1, 1, 0, 0, 1, 1, 0, 2, 0, 0, 2, 2, 2, 2, 2,
    0, 0, 1, 1, 0, 1, 2, 2, 0, 1, 2, 2, 0, 0, 1, 1,
    0, 0, 0, 0, 2, 0, 0, 0, 2, 2, 1, 1, 2, 2, 2, 1,
    0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 2, 2, 2,
    0, 2, 2, 2, 0, 0, 2, 2, 0, 0, 1, 2, 0, 0, 1, 1,
    0, 0, 1, 1, 0, 0, 1, 2, 0, 0, 2, 2, 0, 2, 2, 2,
    0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0, 0, 1, 2, 0,
    0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0,
    0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0,
    0, 1, 2, 0, 2, 0, 1, 2, 1, 2, 0, 1, 0, 1, 2, 0,
    0, 0, 1, 1, 2, 2, 0, 0, 1, 1, 2, 2, 0, 0, 1, 1,
    0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 0, 0, 0, 0, 1, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2,
    0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1,
    0, 0, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2, 1, 1, 2, 2,
    0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 2, 2, 0, 0, 1, 1,
    0, 2, 2, 0, 1, 2, 2, 1, 0, 2, 2, 0, 1, 2, 2, 1,
    0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 0, 1, 0, 1,
    0, 0, 0, 0, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1, 2, 1,
    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2,
    0, 2, 2, 2, 0, 1, 1, 1, 0, 2, 2, 2, 0, 1, 1, 1,
    0, 0, 0, 2, 1, 1, 1, 2, 0, 0, 0, 2, 1, 1, 1, 2,
    0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 2,
    0, 2, 2, 2, 0, 1, 1, 1, 0, 1, 1, 1, 0, 2, 2, 2,
    0, 0, 0, 2, 1, 1, 1, 2, 1, 1, 1, 2, 0, 0, 0, 2,
    0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2,
    0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2, 2, 1, 1, 2,
    0, 1, 1, 0, 0, 1, 1, 0, 2, 2, 2, 2, 2, 2, 2, 2,
    0, 0, 2, 2, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 2, 2,
    0, 0, 2, 2, 1, 1, 2, 2, 1, 1, 2, 2, 0, 0, 2, 2,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 1, 1, 2,
    0, 0, 0, 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0, 1,
    0, 2, 2, 2, 1, 2, 2, 2, 0, 2, 2, 2, 1, 2, 2, 2,
    0, 1, 0, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    0, 1, 1, 1, 2, 0, 1, 1, 2, 2, 0, 1, 2, 2, 2, 0,
};

constexpr std::array<std::uint8_t, 64> anchors2{
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 2, 8, 2, 2, 8, 8, 15, 2, 8, 2, 2, 8, 8, 2, 2,
    15, 15, 6, 8, 2, 8, 15, 15, 2, 8, 2, 2, 2, 15, 15, 6,
    6, 2, 6, 8, 15, 15, 2, 2, 15, 15, 15, 15, 15, 2, 2, 15,
};

constexpr std::array<std::uint8_t, 64> anchors3Second{
    3, 3, 15, 15, 8, 3, 15, 15, 8, 8, 6, 6, 6, 5, 3, 3,
    3, 3, 8, 15, 3, 3, 6, 10, 5, 8, 8, 6, 8, 5, 15, 15,
    8, 15, 3, 5, 6, 10, 8, 15, 15, 3, 15, 5, 15, 15, 15, 15,
    3, 15, 5, 5, 5, 8, 5, 10, 5, 10, 8, 13, 15, 12, 3, 3,
};

constexpr std::array<std::uint8_t, 64> anchors3Third{
    15, 8, 8, 3, 15, 15, 3, 8, 15, 15, 15, 15, 15, 15, 15, 8,
    15, 8, 15, 3, 15, 8, 15, 8, 3, 15, 6, 10, 15, 15, 10, 8,
    15, 3, 15, 10, 10, 8, 9, 10, 6, 15, 8, 15, 3, 6, 6, 8,
    15, 3, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3, 15, 15, 8,
};
// What each of the 8 modes of BC7 stores: subsets, bits of the partition, of the rotation and of the index selection,
// of each colour and alpha channel, whether each endpoint or each subset has a p-bit, and the bits of the two indices.
struct Bc7Mode
{
    std::uint8_t subsets;
    std::uint8_t partitionBits;
    std::uint8_t rotationBits;
    std::uint8_t selectionBits;
    std::uint8_t colorBits;
    std::uint8_t alphaBits;
    bool endpointPBits;
    bool sharedPBits;
    std::uint8_t indexBits;
    std::uint8_t secondIndexBits;
};
constexpr std::array<Bc7Mode, 8> bc7Modes{{
    {3, 4, 0, 0, 4, 0, true, false, 3, 0},
    {2, 6, 0, 0, 6, 0, false, true, 3, 0},
    {3, 6, 0, 0, 5, 0, false, false, 2, 0},
    {2, 6, 0, 0, 7, 0, true, false, 2, 0},
    {1, 0, 2, 1, 5, 6, false, false, 2, 3},
    {1, 0, 2, 0, 7, 8, false, false, 2, 2},
    {1, 0, 0, 0, 7, 7, true, false, 4, 0},
    {2, 6, 0, 0, 5, 5, true, false, 2, 0},
}};

constexpr std::array<std::uint8_t, 4> weights2{0, 21, 43, 64};
constexpr std::array<std::uint8_t, 8> weights3{0, 9, 18, 27, 37, 46, 55, 64};
constexpr std::array<std::uint8_t, 16> weights4{0, 4, 9, 13, 17, 21, 26, 30, 34, 38, 43, 47, 51, 55, 60, 64};

// Reads the bits of a block from its lowest.
class BitReader
{
public:
    explicit BitReader(const std::byte* block) noexcept
    {
        std::memcpy(m_bytes.data(), block, m_bytes.size());
    }

    [[nodiscard]] std::uint32_t read(std::uint32_t count) noexcept
    {
        std::uint32_t value = 0;
        for (std::uint32_t bit = 0; bit < count; ++bit, ++m_at)
        {
            value |= ((m_bytes[m_at >> 3] >> (m_at & 7)) & 1U) << bit;
        }
        return value;
    }

private:
    std::array<std::uint8_t, 16> m_bytes{};
    std::uint32_t m_at = 0;
};

[[nodiscard]] std::uint8_t interpolate(std::uint32_t low, std::uint32_t high, std::uint32_t index, std::uint32_t bits) noexcept
{
    const std::uint32_t weight = bits == 2 ? weights2[index] : bits == 3 ? weights3[index] : weights4[index];
    return static_cast<std::uint8_t>(((64 - weight) * low + weight * high + 32) >> 6);
}

// A value of `bits` bits widened to 8, its high bits repeated below.
[[nodiscard]] std::uint32_t widen(std::uint32_t value, std::uint32_t bits) noexcept
{
    value <<= 8 - bits;
    return value | (value >> bits);
}

} // namespace

void decodeBc7Block(const std::byte* block, std::uint8_t* pixels) noexcept
{
    BitReader bits(block);
    std::uint32_t mode = 0;
    while (mode < 8 && bits.read(1) == 0)
    {
        ++mode;
    }
    if (mode == 8)
    {
        // A reserved mode decodes to transparent black.
        std::fill_n(pixels, 64, std::uint8_t{0});
        return;
    }
    const Bc7Mode& layout = bc7Modes[mode];
    const std::uint32_t partition = bits.read(layout.partitionBits);
    const std::uint32_t rotation = bits.read(layout.rotationBits);
    const std::uint32_t selection = bits.read(layout.selectionBits);

    // Endpoints: red of every endpoint of every subset, then green, blue and alpha.
    std::array<std::array<std::uint32_t, 4>, 6> endpoints{};
    const std::uint32_t endpointCount = layout.subsets * 2U;
    for (std::uint32_t channel = 0; channel < 3; ++channel)
    {
        for (std::uint32_t endpoint = 0; endpoint < endpointCount; ++endpoint)
        {
            endpoints[endpoint][channel] = bits.read(layout.colorBits);
        }
    }
    if (layout.alphaBits > 0)
    {
        for (std::uint32_t endpoint = 0; endpoint < endpointCount; ++endpoint)
        {
            endpoints[endpoint][3] = bits.read(layout.alphaBits);
        }
    }
    std::uint32_t colorBits = layout.colorBits;
    std::uint32_t alphaBits = layout.alphaBits;
    if (layout.endpointPBits || layout.sharedPBits)
    {
        std::array<std::uint32_t, 6> pBits{};
        if (layout.endpointPBits)
        {
            for (std::uint32_t endpoint = 0; endpoint < endpointCount; ++endpoint)
            {
                pBits[endpoint] = bits.read(1);
            }
        }
        else
        {
            for (std::uint32_t subset = 0; subset < layout.subsets; ++subset)
            {
                pBits[subset * 2] = pBits[subset * 2 + 1] = bits.read(1);
            }
        }
        for (std::uint32_t endpoint = 0; endpoint < endpointCount; ++endpoint)
        {
            for (std::uint32_t channel = 0; channel < 4; ++channel)
            {
                endpoints[endpoint][channel] = (endpoints[endpoint][channel] << 1) | pBits[endpoint];
            }
        }
        ++colorBits;
        if (alphaBits > 0)
        {
            ++alphaBits;
        }
    }
    for (std::uint32_t endpoint = 0; endpoint < endpointCount; ++endpoint)
    {
        for (std::uint32_t channel = 0; channel < 3; ++channel)
        {
            endpoints[endpoint][channel] = widen(endpoints[endpoint][channel], colorBits);
        }
        endpoints[endpoint][3] = alphaBits > 0 ? widen(endpoints[endpoint][3], alphaBits) : 255;
    }

    // The subset of each pixel, and the pixels whose index has one bit less.
    std::array<std::uint8_t, 16> subsetOf{};
    std::array<bool, 16> anchor{};
    anchor[0] = true;
    if (layout.subsets == 2)
    {
        std::copy_n(partitions2.begin() + partition * 16, 16, subsetOf.begin());
        anchor[anchors2[partition]] = true;
    }
    else if (layout.subsets == 3)
    {
        std::copy_n(partitions3.begin() + partition * 16, 16, subsetOf.begin());
        anchor[anchors3Second[partition]] = true;
        anchor[anchors3Third[partition]] = true;
    }
    std::array<std::uint32_t, 16> indices{};
    for (std::uint32_t pixel = 0; pixel < 16; ++pixel)
    {
        indices[pixel] = bits.read(anchor[pixel] ? layout.indexBits - 1U : layout.indexBits);
    }
    std::array<std::uint32_t, 16> secondIndices{};
    if (layout.secondIndexBits > 0)
    {
        for (std::uint32_t pixel = 0; pixel < 16; ++pixel)
        {
            secondIndices[pixel] = bits.read(pixel == 0 ? layout.secondIndexBits - 1U : layout.secondIndexBits);
        }
    }

    for (std::uint32_t pixel = 0; pixel < 16; ++pixel)
    {
        const std::array<std::uint32_t, 4>& low = endpoints[subsetOf[pixel] * 2U];
        const std::array<std::uint32_t, 4>& high = endpoints[subsetOf[pixel] * 2U + 1];
        std::uint32_t colorIndex = indices[pixel];
        std::uint32_t colorIndexBits = layout.indexBits;
        std::uint32_t alphaIndex = indices[pixel];
        std::uint32_t alphaIndexBits = layout.indexBits;
        if (layout.secondIndexBits > 0)
        {
            // The selection of mode 4 gives the colours the indices of three bits; the alpha takes the others.
            if (selection == 0)
            {
                alphaIndex = secondIndices[pixel];
                alphaIndexBits = layout.secondIndexBits;
            }
            else
            {
                colorIndex = secondIndices[pixel];
                colorIndexBits = layout.secondIndexBits;
            }
        }
        std::uint8_t* const out = pixels + pixel * 4;
        for (std::uint32_t channel = 0; channel < 3; ++channel)
        {
            out[channel] = interpolate(low[channel], high[channel], colorIndex, colorIndexBits);
        }
        out[3] = interpolate(low[3], high[3], alphaIndex, alphaIndexBits);
        if (rotation != 0)
        {
            std::swap(out[3], out[rotation - 1]);
        }
    }
}

namespace {

// One channel of BC4, as BC5 keeps red and green: two endpoints and an index of three bits for each pixel.
void decodeBc4Channel(const std::byte* half, std::uint8_t* pixels, std::uint32_t channel) noexcept
{
    const auto first = static_cast<std::uint32_t>(half[0]);
    const auto second = static_cast<std::uint32_t>(half[1]);
    std::array<std::uint8_t, 8> values{};
    values[0] = static_cast<std::uint8_t>(first);
    values[1] = static_cast<std::uint8_t>(second);
    if (first > second)
    {
        for (std::uint32_t step = 1; step < 7; ++step)
        {
            values[step + 1] = static_cast<std::uint8_t>(((7 - step) * first + step * second + 3) / 7);
        }
    }
    else
    {
        for (std::uint32_t step = 1; step < 5; ++step)
        {
            values[step + 1] = static_cast<std::uint8_t>(((5 - step) * first + step * second + 2) / 5);
        }
        values[6] = 0;
        values[7] = 255;
    }
    std::uint64_t indices = 0;
    for (std::uint32_t byte = 0; byte < 6; ++byte)
    {
        indices |= static_cast<std::uint64_t>(half[2 + byte]) << (8 * byte);
    }
    for (std::uint32_t pixel = 0; pixel < 16; ++pixel)
    {
        pixels[pixel * 4 + channel] = values[(indices >> (3 * pixel)) & 7U];
    }
}

} // namespace

void decodeBc5Block(const std::byte* block, std::uint8_t* pixels) noexcept
{
    decodeBc4Channel(block, pixels, 0);
    decodeBc4Channel(block + 8, pixels, 1);
    for (std::uint32_t pixel = 0; pixel < 16; ++pixel)
    {
        pixels[pixel * 4 + 2] = 0;
        pixels[pixel * 4 + 3] = 255;
    }
}

std::vector<std::byte> decodeBlocks(asset::TextureFormat format, std::span<const std::byte> blocks, std::uint32_t width,
                                    std::uint32_t height)
{
    const bool bc7 = format == asset::TextureFormat::Bc7Unorm || format == asset::TextureFormat::Bc7Srgb;
    if (!bc7 && format != asset::TextureFormat::Bc5Unorm)
    {
        return {};
    }
    const std::uint32_t blocksWide = (width + 3) / 4;
    const std::uint32_t blocksHigh = (height + 3) / 4;
    if (blocks.size() < std::size_t{blocksWide} * blocksHigh * 16)
    {
        return {};
    }
    std::vector<std::byte> rgba(std::size_t{width} * height * 4);
    std::array<std::uint8_t, 64> pixels{};
    for (std::uint32_t blockY = 0; blockY < blocksHigh; ++blockY)
    {
        for (std::uint32_t blockX = 0; blockX < blocksWide; ++blockX)
        {
            const std::byte* const block = blocks.data() + (std::size_t{blockY} * blocksWide + blockX) * 16;
            if (bc7)
            {
                decodeBc7Block(block, pixels.data());
            }
            else
            {
                decodeBc5Block(block, pixels.data());
            }
            // The pixels of the block inside the level: its last blocks may go past the edges.
            for (std::uint32_t y = 0; y < 4 && blockY * 4 + y < height; ++y)
            {
                for (std::uint32_t x = 0; x < 4 && blockX * 4 + x < width; ++x)
                {
                    const std::size_t at = ((std::size_t{blockY} * 4 + y) * width + blockX * 4 + x) * 4;
                    std::memcpy(rgba.data() + at, pixels.data() + (y * 4 + x) * 4, 4);
                }
            }
        }
    }
    return rgba;
}

} // namespace devex::render::webgpu
