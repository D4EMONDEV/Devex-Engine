#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>

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

Result<void> writeTextFile(const std::filesystem::path& path, std::string_view text)
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

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.write(text.data(), static_cast<std::streamsize>(text.size())))
    {
        return makeError(ErrorCode::Io, "cannot write '{}'", toUtf8(path));
    }
    return {};
}

} // namespace devex::core
