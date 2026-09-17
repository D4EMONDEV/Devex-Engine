#include <devex/core/File.hpp>
#include <devex/core/MappedFile.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <utility>
#include <vector>

TEST_CASE("Mapped files show the bytes of the file", "[core][file]")
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("devex-mapped-" + devex::core::Uuid::generate().toString());
    std::vector<std::byte> bytes(70000);
    for (std::size_t index = 0; index < bytes.size(); ++index)
    {
        bytes[index] = static_cast<std::byte>(index * 31 % 251);
    }
    REQUIRE(devex::core::writeFileAtomically(directory / "data.bin", bytes));
    REQUIRE(devex::core::writeFileAtomically(directory / "empty.bin", {}));

    {
        devex::core::Result<devex::core::MappedFile> mapped = devex::core::MappedFile::open(directory / "data.bin");
        REQUIRE(mapped.has_value());
        CHECK(std::ranges::equal(mapped->bytes(), bytes));

        // Moving keeps the mapping alive in the new owner.
        devex::core::MappedFile moved = std::move(*mapped);
        CHECK(moved.bytes().size() == bytes.size());
        CHECK(mapped->bytes().empty());
        CHECK(moved.bytes()[1000] == bytes[1000]);

        const devex::core::Result<devex::core::MappedFile> empty = devex::core::MappedFile::open(directory / "empty.bin");
        REQUIRE(empty.has_value());
        CHECK(empty->bytes().empty());

        CHECK(devex::core::MappedFile::open(directory / "missing.bin").error().code == devex::core::ErrorCode::NotFound);
    }
    std::filesystem::remove_all(directory);
}
