#include <devex/core/File.hpp>
#include <devex/core/Uuid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <thread>
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

TEST_CASE("A file held for a moment by a reader is still replaced", "[core][file]")
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("devex-file-" + devex::core::Uuid::generate().toString());
    const std::filesystem::path file = directory / "data.bin";
    REQUIRE(devex::core::writeFileAtomically(file, std::vector<std::byte>(16, std::byte{1})));

    // A reader opened the old contents, as an antivirus or another thread may: on Windows, the
    // file cannot be replaced until it lets go, a moment later.
    std::optional<std::ifstream> reader(std::in_place, file, std::ios::binary);
    REQUIRE(reader->is_open());
    std::thread release([&reader] {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        reader.reset();
    });
    const std::vector<std::byte> replaced(32, std::byte{2});
    const auto written = devex::core::writeFileAtomically(file, replaced);
    release.join();
    INFO((written ? std::string() : written.error().message));
    REQUIRE(written.has_value());
    CHECK(devex::core::readBinaryFile(file) == replaced);
    std::filesystem::remove_all(directory);
}
