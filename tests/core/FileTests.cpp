#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <vector>

TEST_CASE("Binary files are replaced atomically and read back", "[core][file]")
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("devex-file-" + devex::core::Uuid::generate().toString());
    const std::filesystem::path file = directory / "nested" / "data.bin";

    const std::vector<std::byte> first{std::byte{1}, std::byte{0}, std::byte{255}};
    REQUIRE(devex::core::writeFileAtomically(file, first));
    CHECK(devex::core::readBinaryFile(file) == first);

    const std::vector<std::byte> second(4096, std::byte{7});
    REQUIRE(devex::core::writeFileAtomically(file, second));
    CHECK(devex::core::readBinaryFile(file) == second);

    // No temporary file is left behind.
    std::size_t entries = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(file.parent_path()))
    {
        ++entries;
    }
    CHECK(entries == 1);

    CHECK(devex::core::readBinaryFile(directory / "missing.bin").error().code ==
          devex::core::ErrorCode::NotFound);
    std::filesystem::remove_all(directory);
}
