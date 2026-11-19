#pragma once

// Decodes the blocks of BC5 and BC7 textures to RGBA8 on the CPU, for a GPU that cannot read them: WebGPU in the browser
// of a phone, which reads ASTC and ETC2 instead, and OpenGL ES, or an OpenGL without BC7.
#include <devex/asset/TextureData.hpp>
#include <devex/core/Export.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace devex::render {

// The 16 pixels of one block of 16 bytes, RGBA, row after row.
DEVEX_API void decodeBc7Block(const std::byte* block, std::uint8_t* pixels) noexcept;
// Red and green from their two halves; blue 0 and alpha 255.
DEVEX_API void decodeBc5Block(const std::byte* block, std::uint8_t* pixels) noexcept;

// A level of `width` by `height` pixels in BC5 or BC7, as RGBA8 rows; empty for another format.
[[nodiscard]] DEVEX_API std::vector<std::byte> decodeBlocks(asset::TextureFormat format, std::span<const std::byte> blocks,
                                                            std::uint32_t width, std::uint32_t height);

} // namespace devex::render
