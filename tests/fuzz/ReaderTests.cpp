#include "Readers.hpp"

#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/Components.hpp>
#include <devex/serialization/Binary.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr std::string_view entityHeader =
    "[scene format=1]\n[entity uuid=\"6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23\" name=\"Test\"]\n";

class QuietReaderLogs
{
public:
    QuietReaderLogs()
        : m_previous(devex::core::logLevel())
    {
        devex::core::setLogLevel(devex::core::LogLevel::Error);
    }
    ~QuietReaderLogs()
    {
        devex::core::setLogLevel(m_previous);
    }
    QuietReaderLogs(const QuietReaderLogs&) = delete;
    QuietReaderLogs& operator=(const QuietReaderLogs&) = delete;

private:
    devex::core::LogLevel m_previous;
};

template <typename Check>
void mutations(const std::vector<std::byte>& seed, Check check)
{
    std::mt19937 random(0x445658u);
    check(seed);
    for (std::size_t iteration = 0; iteration < 512; ++iteration)
    {
        INFO(iteration);
        auto bytes = seed;
        const std::size_t position = random() % bytes.size();
        switch (iteration % 4)
        {
        case 0:
            bytes.resize(position);
            break;
        case 1:
            bytes[position] ^= std::byte{static_cast<unsigned char>(1u << (random() % 8))};
            break;
        case 2:
            for (std::size_t offset = position; offset < bytes.size() && offset < position + 8; ++offset)
            {
                bytes[offset] = std::byte{0xff};
            }
            break;
        default:
            for (int replacement = 0; replacement < 4; ++replacement)
            {
                bytes[random() % bytes.size()] = std::byte{static_cast<unsigned char>(random())};
            }
            break;
        }
        check(bytes);
    }
}

} // namespace

TEST_CASE("Mesh and terrain files reject every truncated prefix", "[asset][artifact][corrupt]")
{
    const auto check = [](const auto& bytes, auto decode) {
        REQUIRE(decode(bytes).has_value());
        for (std::size_t length = 0; length < bytes.size(); ++length)
        {
            INFO(length);
            REQUIRE_FALSE(decode(std::span<const std::byte>(bytes).first(length)).has_value());
        }
    };
    check(devex::asset::encodeMesh(devex::asset::makePlane()), devex::asset::decodeMesh);
    check(devex::asset::encodeTerrain(devex::asset::makeFlatTerrain(33)), devex::asset::decodeTerrain);
}

TEST_CASE("Corrupt array counts fail before requesting oversized allocations",
          "[asset][artifact][corrupt]")
{
    auto mesh = devex::asset::encodeMesh(devex::asset::makePlane());
    auto terrain = devex::asset::encodeTerrain(devex::asset::makeFlatTerrain(33));
    const auto huge = std::numeric_limits<std::uint64_t>::max();
    // The 12-byte artifact header, then the first array count (terrain has its resolution first).
    std::memcpy(mesh.data() + 12, &huge, sizeof(huge));
    std::memcpy(terrain.data() + 16, &huge, sizeof(huge));
    CHECK_FALSE(devex::asset::decodeMesh(mesh).has_value());
    CHECK_FALSE(devex::asset::decodeTerrain(terrain).has_value());

    mesh = devex::asset::encodeMesh(devex::asset::makePlane());
    devex::serialization::BinaryReader reader(mesh);
    std::array<std::byte, 12> header{};
    reader.readBytes(header);
    static_cast<void>(reader.readArray<devex::asset::Vertex>());
    static_cast<void>(reader.readArray<std::uint32_t>());
    const auto submeshes = std::numeric_limits<std::uint32_t>::max();
    std::memcpy(mesh.data() + reader.offset(), &submeshes, sizeof(submeshes));
    CHECK_FALSE(devex::asset::decodeMesh(mesh).has_value());
}

TEST_CASE("Mesh files reject non-finite geometry and skinning data", "[asset][mesh][corrupt]")
{
    auto mesh = devex::asset::makePlane();
    mesh.inverseBind.emplace_back(1.0f);
    mesh.skin.resize(mesh.vertices.size());
    for (auto& skin : mesh.skin)
    {
        skin.weights.x = 1.0f;
    }
    REQUIRE(devex::asset::decodeMesh(devex::asset::encodeMesh(mesh)).has_value());
    const std::array attributes{
        &mesh.vertices[0].position.x, &mesh.vertices[0].normal.y, &mesh.vertices[0].uv.x,
        &mesh.vertices[0].tangent.w, &mesh.bounds.min.x, &mesh.bounds.max.z,
        &mesh.skin[0].weights.x, &mesh.inverseBind[0][2][1]};
    for (float* const attribute : attributes)
    {
        const float previous = *attribute;
        for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                    std::numeric_limits<float>::infinity(),
                                    -std::numeric_limits<float>::infinity()})
        {
            *attribute = invalid;
            CHECK_FALSE(devex::asset::decodeMesh(devex::asset::encodeMesh(mesh)).has_value());
        }
        *attribute = previous;
    }
}

TEST_CASE("Scenes reject numbers that overflow component floats", "[scene][serializer][corrupt]")
{
    for (const std::string_view component : {
             "[component type=\"Transform\"]\nposition = vec3(1e300, 0, 0)\n",
             "[component type=\"Transform\"]\nrotation = quat(0, -1e300, 0, 1)\n",
             "[component type=\"PointLight\"]\nintensity = 1e300\n"})
    {
        const auto loaded =
            devex::scene::loadScene(std::string(entityHeader) + std::string(component));
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().code == devex::core::ErrorCode::Parse);
        CHECK(loaded.error().message.find("float range") != std::string::npos);
    }
}

TEST_CASE("The largest finite component floats survive a scene round trip",
          "[scene][serializer][corrupt]")
{
    devex::scene::Scene scene;
    const auto entity = scene.createEntity("Float limits");
    const float largest = std::numeric_limits<float>::max();
    scene.add<devex::scene::Transform>(entity).position = {largest, -largest, 0.0f};
    const auto loaded = devex::scene::loadScene(devex::scene::saveScene(scene));
    REQUIRE(loaded.has_value());
    CHECK(loaded->get<devex::scene::Transform>(loaded->findEntity(scene.uuid(entity))).position ==
          devex::math::Vec3(largest, -largest, 0.0f));
}

TEST_CASE("Deterministic mutations exercise the terrain reader in every CI build",
          "[corrupt][fuzz]")
{
    mutations(devex::asset::encodeTerrain(devex::asset::makeFlatTerrain(33)), devex::tests::checkTerrain);
}

TEST_CASE("Deterministic mutations exercise the mesh reader in every CI build", "[corrupt][fuzz]")
{
    mutations(devex::asset::encodeMesh(devex::asset::makePlane()), devex::tests::checkMesh);
}

TEST_CASE("Deterministic mutations exercise the scene reader in every CI build", "[corrupt][fuzz]")
{
    const QuietReaderLogs quiet;
    const std::string text = std::string(entityHeader) +
        "[component type=\"Transform\"]\nposition = vec3(0, 1, -2)\n"
        "[component type=\"GameData\"]\nvalues = list(1, 2, list(3))\n";
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    mutations(std::vector<std::byte>(bytes.begin(), bytes.end()), devex::tests::checkScene);
}
