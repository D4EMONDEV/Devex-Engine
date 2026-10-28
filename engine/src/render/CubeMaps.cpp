#include <devex/render/CubeMaps.hpp>

#include <array>

namespace devex::render {
namespace {

// Where each face looks, and which way is up in it.
struct Face
{
    math::Vec3 forward;
    math::Vec3 up;
};
constexpr std::array<Face, 6> faces{{
    {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},
    {{0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
    {{0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},
}};

} // namespace

math::Vec3 cubeFaceDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t size) noexcept
{
    // As shaders/ibl.slang reads them.
    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size) * 2.0f - 1.0f;
    math::Vec3 direction;
    switch (face)
    {
    case 0:
        direction = {1.0f, -v, -u};
        break;
    case 1:
        direction = {-1.0f, -v, u};
        break;
    case 2:
        direction = {u, 1.0f, v};
        break;
    case 3:
        direction = {u, -1.0f, -v};
        break;
    case 4:
        direction = {u, -v, 1.0f};
        break;
    default:
        direction = {-u, -v, -1.0f};
        break;
    }
    return math::normalize(direction);
}

math::Mat4 cubeFaceView(math::Vec3 position, std::uint32_t face) noexcept
{
    const Face& looked = faces[face % faces.size()];
    return math::lookAt(position, position + looked.forward, looked.up);
}

} // namespace devex::render
