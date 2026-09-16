#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>

#include <fstream>
#include <iterator>
#include <system_error>

namespace devex::core {

Result<std::string> readTextFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        return makeError(ErrorCode::NotFound, "cannot open '{}'", toUtf8(path));
    }
    std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad())
    {
        return makeError(ErrorCode::Io, "cannot read '{}'", toUtf8(path));
    }
    return text;
}

namespace {

[[nodiscard]] Result<void> createParentDirectories(const std::filesystem::path& path)
{
    if (path.has_parent_path())
    {
        std::error_code error;
        std::filesystem::create_directories(path.parent_path(), error);
        if (error)
        {
            return makeError(ErrorCode::Io, "cannot create the directory of '{}': {}",
                             toUtf8(path), error.message());
        }
    }
    return {};
}

} // namespace

Result<void> writeTextFile(const std::filesystem::path& path, std::string_view text)
{
    if (Result<void> created = createParentDirectories(path); !created)
    {
        return created;
    }

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.write(text.data(), static_cast<std::streamsize>(text.size())))
    {
        return makeError(ErrorCode::Io, "cannot write '{}'", toUtf8(path));
    }
    return {};
}

Result<std::vector<std::byte>> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        return makeError(ErrorCode::NotFound, "cannot open '{}'", toUtf8(path));
    }
    const std::streamoff size = file.tellg();
    if (size < 0)
    {
        return makeError(ErrorCode::Io, "cannot read '{}'", toUtf8(path));
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size)))
    {
        return makeError(ErrorCode::Io, "cannot read '{}'", toUtf8(path));
    }
    return bytes;
}

Result<void> writeFileAtomically(const std::filesystem::path& path, std::span<const std::byte> bytes)
{
    if (Result<void> created = createParentDirectories(path); !created)
    {
        return created;
    }

    std::filesystem::path temporary = path;
    temporary += std::filesystem::path(".tmp-" + Uuid::generate().toString());
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file.write(reinterpret_cast<const char*>(bytes.data()),
                        static_cast<std::streamsize>(bytes.size())) ||
            !file.flush())
        {
            file.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return makeError(ErrorCode::Io, "cannot write '{}'", toUtf8(temporary));
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
    {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return makeError(ErrorCode::Io, "cannot replace '{}': {}", toUtf8(path), error.message());
    }
    return {};
}

} // namespace devex::core
