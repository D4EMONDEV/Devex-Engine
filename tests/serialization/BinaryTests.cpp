#include <devex/serialization/Binary.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

using devex::serialization::BinaryReader;
using devex::serialization::BinaryWriter;

TEST_CASE("Binary values, strings and arrays read back as written", "[serialization][binary]")
{
    BinaryWriter writer;
    writer.write(std::uint32_t{0xDEADBEEF});
    writer.writeString("Devex");
    const std::vector<float> values{1.5f, -2.0f, 3.25f};
    writer.writeArray(std::span<const float>(values));
    writer.write(std::int8_t{-5});
    const std::vector<std::byte> bytes = writer.take();

    BinaryReader reader(bytes);
    CHECK(reader.read<std::uint32_t>() == 0xDEADBEEF);
    CHECK(reader.readString() == "Devex");
    CHECK(reader.readArray<float>() == values);
    CHECK(reader.read<std::int8_t>() == -5);
    CHECK_FALSE(reader.failed());
    CHECK(reader.remaining() == 0);
}

TEST_CASE("Reading past the end fails without huge allocations", "[serialization][binary]")
{
    BinaryWriter writer;
    // An array claiming far more elements than the data holds.
    writer.write(std::uint64_t{1} << 40);
    const std::vector<std::byte> bytes = writer.take();

    BinaryReader reader(bytes);
    CHECK(reader.readArray<double>().empty());
    CHECK(reader.failed());
    // Once failed, reads return zeroed values.
    CHECK(reader.read<std::uint32_t>() == 0);

    BinaryReader truncated(std::span(bytes).first(3));
    CHECK(truncated.read<std::uint32_t>() == 0);
    CHECK(truncated.failed());
}
