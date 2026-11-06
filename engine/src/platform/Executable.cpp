#include <devex/core/Path.hpp>
#include <devex/platform/Executable.hpp>

#include <devex/core/File.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace devex::platform {

namespace {

// The subsystem field of a Portable Executable: where it is, and what it holds.
constexpr std::uint16_t windowsGuiSubsystem = 2;
constexpr std::uint16_t windowsConsoleSubsystem = 3;

// Where the subsystem sits in the file: after the MS-DOS stub, the "PE" signature, the file header,
// and 68 bytes into the optional header, for 32 and 64-bit images alike.
[[nodiscard]] core::Result<std::streamoff> subsystemOffset(std::istream& file, const std::filesystem::path& path)
{
    std::array<char, 64> dos{};
    file.read(dos.data(), dos.size());
    if (!file || dos[0] != 'M' || dos[1] != 'Z')
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' is not a Windows executable", core::toUtf8(path));
    }
    std::uint32_t peOffset = 0;
    std::memcpy(&peOffset, dos.data() + 0x3C, sizeof(peOffset));
    std::array<char, 4 + 20 + 2> headers{};
    file.seekg(peOffset);
    file.read(headers.data(), headers.size());
    if (!file || headers[0] != 'P' || headers[1] != 'E' || headers[2] != '\0' || headers[3] != '\0')
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' has no PE header", core::toUtf8(path));
    }
    std::uint16_t magic = 0;
    std::memcpy(&magic, headers.data() + 24, sizeof(magic));
    if (magic != 0x10B && magic != 0x20B)
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' has an unknown optional header", core::toUtf8(path));
    }
    return static_cast<std::streamoff>(peOffset) + 24 + 68;
}

} // namespace

core::Result<bool> opensConsole(const std::filesystem::path& executable)
{
    std::ifstream file(executable, std::ios::binary);
    if (!file)
    {
        return core::makeError(core::ErrorCode::NotFound, "cannot read '{}'", core::toUtf8(executable));
    }
    const core::Result<std::streamoff> offset = subsystemOffset(file, executable);
    if (!offset)
    {
        return std::unexpected(offset.error());
    }
    std::uint16_t subsystem = 0;
    file.seekg(*offset);
    file.read(reinterpret_cast<char*>(&subsystem), sizeof(subsystem));
    if (!file)
    {
        return core::makeError(core::ErrorCode::Parse, "'{}' is cut short", core::toUtf8(executable));
    }
    return subsystem == windowsConsoleSubsystem;
}

core::Result<void> setWindowedApplication(const std::filesystem::path& executable)
{
    std::fstream file(executable, std::ios::binary | std::ios::in | std::ios::out);
    if (!file)
    {
        return core::makeError(core::ErrorCode::Io, "cannot open '{}'", core::toUtf8(executable));
    }
    const core::Result<std::streamoff> offset = subsystemOffset(file, executable);
    if (!offset)
    {
        return std::unexpected(offset.error());
    }
    file.seekp(*offset);
    file.write(reinterpret_cast<const char*>(&windowsGuiSubsystem), sizeof(windowsGuiSubsystem));
    if (!file)
    {
        return core::makeError(core::ErrorCode::Io, "cannot write '{}'", core::toUtf8(executable));
    }
    return {};
}

namespace {

// A Portable Executable is little-endian whichever system writes it.
class ExecutableBytes
{
public:
    explicit ExecutableBytes(std::vector<std::byte>& bytes) noexcept : m_bytes(bytes)
    {
    }

    // Outside the file, it reads zero and remembers: a damaged file is refused once, at the end.
    template <std::unsigned_integral T>
    [[nodiscard]] T read(std::size_t offset) noexcept
    {
        if (offset > m_bytes.size() || m_bytes.size() - offset < sizeof(T))
        {
            m_damaged = true;
            return 0;
        }
        T value = 0;
        for (std::size_t index = 0; index < sizeof(T); ++index)
        {
            value = static_cast<T>(value | (static_cast<T>(m_bytes[offset + index]) << (index * 8)));
        }
        return value;
    }

    template <std::unsigned_integral T>
    void write(std::size_t offset, T value) noexcept
    {
        for (std::size_t index = 0; index < sizeof(T) && offset + index < m_bytes.size(); ++index)
        {
            m_bytes[offset + index] = static_cast<std::byte>((static_cast<std::uint64_t>(value) >> (index * 8)) & 0xFF);
        }
    }

    [[nodiscard]] bool damaged() const noexcept
    {
        return m_damaged;
    }

private:
    std::vector<std::byte>& m_bytes;
    bool m_damaged = false;
};

struct ExecutableSection
{
    std::uint32_t virtualSize = 0;
    std::uint32_t virtualAddress = 0;
    std::uint32_t rawSize = 0;
    std::uint32_t rawPointer = 0;
};

// Where the parts of the headers are in the file.
struct ExecutableLayout
{
    std::size_t coffHeader = 0;
    std::size_t optionalHeader = 0;
    std::size_t directories = 0;
    std::size_t sectionTable = 0;
    std::uint32_t sectionAlignment = 0;
    std::uint32_t fileAlignment = 0;
    std::vector<ExecutableSection> sections;
};

constexpr std::size_t sectionHeaderSize = 40;
constexpr std::size_t resourceDirectory = 2;
constexpr std::size_t certificateDirectory = 4;
constexpr std::uint32_t namedOrDirectory = 0x80000000u;
constexpr std::uint32_t iconResourceType = 3;
constexpr std::uint32_t iconGroupResourceType = 14;

[[nodiscard]] core::Result<ExecutableLayout> readLayout(ExecutableBytes& file)
{
    const auto refused = [] { return core::makeError(core::ErrorCode::InvalidArgument, "this is not a Windows executable"); };
    if (file.read<std::uint16_t>(0) != 0x5A4D)
    {
        return refused();
    }
    ExecutableLayout layout;
    const std::size_t signature = file.read<std::uint32_t>(60);
    if (file.read<std::uint32_t>(signature) != 0x00004550)
    {
        return refused();
    }
    layout.coffHeader = signature + 4;
    const std::uint16_t sectionCount = file.read<std::uint16_t>(layout.coffHeader + 2);
    const std::uint16_t optionalSize = file.read<std::uint16_t>(layout.coffHeader + 16);
    layout.optionalHeader = layout.coffHeader + 20;
    const std::uint16_t magic = file.read<std::uint16_t>(layout.optionalHeader);
    if (magic != 0x20B && magic != 0x10B)
    {
        return refused();
    }
    const bool wide = magic == 0x20B;
    layout.sectionAlignment = file.read<std::uint32_t>(layout.optionalHeader + 32);
    layout.fileAlignment = file.read<std::uint32_t>(layout.optionalHeader + 36);
    const std::uint32_t directoryCount = file.read<std::uint32_t>(layout.optionalHeader + (wide ? 108 : 92));
    layout.directories = layout.optionalHeader + (wide ? 112 : 96);
    if (directoryCount <= resourceDirectory || layout.sectionAlignment == 0 || layout.fileAlignment == 0)
    {
        return refused();
    }
    layout.sectionTable = layout.optionalHeader + optionalSize;
    for (std::uint16_t index = 0; index < sectionCount; ++index)
    {
        const std::size_t header = layout.sectionTable + index * sectionHeaderSize;
        layout.sections.push_back({.virtualSize = file.read<std::uint32_t>(header + 8),
                                   .virtualAddress = file.read<std::uint32_t>(header + 12),
                                   .rawSize = file.read<std::uint32_t>(header + 16),
                                   .rawPointer = file.read<std::uint32_t>(header + 20)});
    }
    if (file.damaged())
    {
        return refused();
    }
    return layout;
}

// Where an address of the running image is in the file.
[[nodiscard]] std::optional<std::size_t> fileOffset(const ExecutableLayout& layout, std::uint32_t address)
{
    for (const ExecutableSection& section : layout.sections)
    {
        const std::uint32_t size = std::max(section.virtualSize, section.rawSize);
        if (address >= section.virtualAddress && address - section.virtualAddress < size)
        {
            return std::size_t{section.rawPointer} + (address - section.virtualAddress);
        }
    }
    return std::nullopt;
}

// A name of the resource tree: its characters, as many as its length says.
[[nodiscard]] std::u16string resourceName(ExecutableBytes& file, std::size_t offset)
{
    std::u16string name;
    const std::uint16_t length = file.read<std::uint16_t>(offset);
    for (std::uint16_t index = 0; index < length && !file.damaged(); ++index)
    {
        name.push_back(static_cast<char16_t>(file.read<std::uint16_t>(offset + 2 + std::size_t{index} * 2)));
    }
    return name;
}

// The three levels of the tree of resources: their types, their names, and their languages.
[[nodiscard]] core::Result<std::vector<ExecutableResource>> readResources(const std::vector<std::byte>& bytes, ExecutableBytes& file,
                                                                          const ExecutableLayout& layout)
{
    const auto refused = [] { return core::makeError(core::ErrorCode::InvalidArgument, "the resources of this executable are damaged"); };
    std::vector<ExecutableResource> resources;
    const std::uint32_t address = file.read<std::uint32_t>(layout.directories + resourceDirectory * 8);
    if (address == 0)
    {
        return resources;
    }
    const std::optional<std::size_t> root = fileOffset(layout, address);
    if (!root)
    {
        return refused();
    }
    // The entries of a directory, each called with its identifier or name, and where it points.
    const auto entries = [&](std::size_t directory, const auto& visit) {
        const std::size_t count = std::size_t{file.read<std::uint16_t>(directory + 12)} + file.read<std::uint16_t>(directory + 14);
        for (std::size_t index = 0; index < count && !file.damaged(); ++index)
        {
            const std::size_t entry = directory + 16 + index * 8;
            const std::uint32_t name = file.read<std::uint32_t>(entry);
            const std::uint32_t target = file.read<std::uint32_t>(entry + 4);
            const bool named = (name & namedOrDirectory) != 0;
            visit(named ? 0 : name, named ? resourceName(file, *root + (name & ~namedOrDirectory)) : std::u16string{}, target);
        }
    };
    bool shaped = true;
    entries(*root, [&](std::uint32_t type, const std::u16string& typeName, std::uint32_t typeTarget) {
        shaped = shaped && (typeTarget & namedOrDirectory) != 0;
        entries(*root + (typeTarget & ~namedOrDirectory), [&](std::uint32_t id, const std::u16string& name, std::uint32_t nameTarget) {
            shaped = shaped && (nameTarget & namedOrDirectory) != 0;
            entries(*root + (nameTarget & ~namedOrDirectory), [&](std::uint32_t language, const std::u16string&, std::uint32_t leaf) {
                shaped = shaped && (leaf & namedOrDirectory) == 0;
                const std::size_t entry = *root + leaf;
                const std::uint32_t dataAddress = file.read<std::uint32_t>(entry);
                const std::uint32_t size = file.read<std::uint32_t>(entry + 4);
                const std::optional<std::size_t> data = fileOffset(layout, dataAddress);
                shaped = shaped && data && *data <= bytes.size() && bytes.size() - *data >= size;
                if (shaped)
                {
                    resources.push_back({.type = type,
                                         .typeName = typeName,
                                         .id = id,
                                         .name = name,
                                         .language = language,
                                         .codePage = file.read<std::uint32_t>(entry + 8),
                                         .data = std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(*data),
                                                                        bytes.begin() + static_cast<std::ptrdiff_t>(*data + size))});
                }
            });
        });
    });
    if (!shaped || file.damaged())
    {
        return refused();
    }
    return resources;
}

[[nodiscard]] constexpr std::size_t alignedUp(std::size_t value, std::size_t alignment) noexcept
{
    return (value + alignment - 1) / alignment * alignment;
}

// Windows looks up named entries before numbered ones, each in order.
struct ResourceKey
{
    std::u16string name;
    std::uint32_t id = 0;

    [[nodiscard]] bool operator<(const ResourceKey& other) const noexcept
    {
        if (name.empty() != other.name.empty())
        {
            return !name.empty();
        }
        return name != other.name ? name < other.name : id < other.id;
    }
};

// The tree of resources as a section that the running image holds at an address.
[[nodiscard]] std::vector<std::byte> resourceSection(const std::vector<ExecutableResource>& resources, std::uint32_t address)
{
    using Languages = std::map<std::uint32_t, const ExecutableResource*>;
    using Names = std::map<ResourceKey, Languages>;
    std::map<ResourceKey, Names> tree;
    for (const ExecutableResource& resource : resources)
    {
        tree[{resource.typeName, resource.type}][{resource.name, resource.id}][resource.language] = &resource;
    }
    constexpr auto directorySize = [](std::size_t entries) { return 16 + entries * 8; };
    // In order: the directories, the names, what describes each resource, and the resources.
    std::size_t cursor = directorySize(tree.size());
    std::map<const void*, std::size_t> directories;
    for (const auto& [type, names] : tree)
    {
        directories[&names] = cursor;
        cursor += directorySize(names.size());
    }
    for (const auto& [type, names] : tree)
    {
        for (const auto& [name, languages] : names)
        {
            directories[&languages] = cursor;
            cursor += directorySize(languages.size());
        }
    }
    std::map<const std::u16string*, std::size_t> strings;
    const auto placeName = [&](const std::u16string& name) {
        if (!name.empty())
        {
            strings[&name] = cursor;
            cursor += 2 + name.size() * 2;
        }
    };
    for (const auto& [type, names] : tree)
    {
        placeName(type.name);
        for (const auto& [name, languages] : names)
        {
            placeName(name.name);
        }
    }
    cursor = alignedUp(cursor, 8);
    std::map<const ExecutableResource*, std::size_t> descriptions;
    for (const ExecutableResource& resource : resources)
    {
        descriptions[&resource] = cursor;
        cursor += 16;
    }
    std::map<const ExecutableResource*, std::size_t> data;
    for (const ExecutableResource& resource : resources)
    {
        cursor = alignedUp(cursor, 8);
        data[&resource] = cursor;
        cursor += resource.data.size();
    }

    std::vector<std::byte> section(cursor, std::byte{0});
    ExecutableBytes out(section);
    const auto writeDirectory = [&](std::size_t at, const auto& children, const auto& key, const auto& target) {
        std::uint16_t named = 0;
        std::uint16_t numbered = 0;
        std::size_t entry = at + 16;
        for (const auto& [child, value] : children)
        {
            const ResourceKey resourceKey = key(child);
            (resourceKey.name.empty() ? numbered : named) += 1;
            const std::uint32_t label = resourceKey.name.empty()
                                            ? resourceKey.id
                                            : namedOrDirectory | static_cast<std::uint32_t>(strings.at(&child.name));
            out.write<std::uint32_t>(entry, label);
            out.write<std::uint32_t>(entry + 4, target(value));
            entry += 8;
        }
        out.write<std::uint16_t>(at + 12, named);
        out.write<std::uint16_t>(at + 14, numbered);
    };
    const auto writeLanguages = [&](std::size_t at, const Languages& languages) {
        std::size_t entry = at + 16;
        for (const auto& [language, resource] : languages)
        {
            out.write<std::uint32_t>(entry, language);
            out.write<std::uint32_t>(entry + 4, static_cast<std::uint32_t>(descriptions.at(resource)));
            entry += 8;
        }
        out.write<std::uint16_t>(at + 14, static_cast<std::uint16_t>(languages.size()));
    };
    const auto sameKey = [](const ResourceKey& key) -> const ResourceKey& { return key; };
    writeDirectory(0, tree, sameKey, [&](const Names& names) {
        return namedOrDirectory | static_cast<std::uint32_t>(directories.at(&names));
    });
    for (const auto& [type, names] : tree)
    {
        writeDirectory(directories.at(&names), names, sameKey, [&](const Languages& languages) {
            return namedOrDirectory | static_cast<std::uint32_t>(directories.at(&languages));
        });
        for (const auto& [name, languages] : names)
        {
            writeLanguages(directories.at(&languages), languages);
        }
    }
    for (const auto& [name, at] : strings)
    {
        out.write<std::uint16_t>(at, static_cast<std::uint16_t>(name->size()));
        for (std::size_t index = 0; index < name->size(); ++index)
        {
            out.write<std::uint16_t>(at + 2 + index * 2, static_cast<std::uint16_t>((*name)[index]));
        }
    }
    for (const ExecutableResource& resource : resources)
    {
        const std::size_t at = descriptions.at(&resource);
        out.write<std::uint32_t>(at, address + static_cast<std::uint32_t>(data.at(&resource)));
        out.write<std::uint32_t>(at + 4, static_cast<std::uint32_t>(resource.data.size()));
        out.write<std::uint32_t>(at + 8, resource.codePage);
        std::ranges::copy(resource.data, section.begin() + static_cast<std::ptrdiff_t>(data.at(&resource)));
    }
    return section;
}

void appendNumber(std::vector<std::byte>& bytes, std::uint64_t value, std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xFF));
    }
}

// A bitmap icon: a header, the BGRA pixels bottom-up, and an empty 1-bit mask, since alpha is used.
[[nodiscard]] std::vector<std::byte> bitmapIcon(const IconImage& image)
{
    std::vector<std::byte> bytes;
    appendNumber(bytes, 40, 4);
    appendNumber(bytes, image.size, 4);
    appendNumber(bytes, std::uint64_t{image.size} * 2, 4);
    appendNumber(bytes, 1, 2);
    appendNumber(bytes, 32, 2);
    // No compression, no sizes, and no palette.
    appendNumber(bytes, 0, 24);
    for (std::size_t row = image.size; row-- > 0;)
    {
        for (std::size_t column = 0; column < image.size; ++column)
        {
            const std::size_t pixel = (row * image.size + column) * 4;
            bytes.push_back(std::byte{image.rgba[pixel + 2]});
            bytes.push_back(std::byte{image.rgba[pixel + 1]});
            bytes.push_back(std::byte{image.rgba[pixel]});
            bytes.push_back(std::byte{image.rgba[pixel + 3]});
        }
    }
    const std::size_t maskRow = ((image.size + 31) / 32) * 4;
    bytes.resize(bytes.size() + maskRow * image.size, std::byte{0});
    return bytes;
}

} // namespace

core::Result<std::vector<ExecutableResource>> readExecutableResources(const std::filesystem::path& executable)
{
    core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(executable);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    ExecutableBytes file(*bytes);
    const core::Result<ExecutableLayout> layout = readLayout(file);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }
    return readResources(*bytes, file, *layout);
}

core::Result<void> setExecutableIcon(const std::filesystem::path& executable, std::span<const IconImage> images)
{
    if (images.empty() || images.size() > 16)
    {
        return core::makeError(core::ErrorCode::InvalidArgument, "an icon needs from 1 to 16 sizes");
    }
    for (const IconImage& image : images)
    {
        if (image.size == 0 || image.size > 256 || image.rgba.size() != std::size_t{image.size} * image.size * 4)
        {
            return core::makeError(core::ErrorCode::InvalidArgument, "an icon image of {} pixels is invalid", image.size);
        }
    }
    core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(executable);
    if (!bytes)
    {
        return std::unexpected(bytes.error());
    }
    ExecutableBytes file(*bytes);
    const core::Result<ExecutableLayout> layout = readLayout(file);
    if (!layout)
    {
        return std::unexpected(layout.error());
    }
    core::Result<std::vector<ExecutableResource>> existing = readResources(*bytes, file, *layout);
    if (!existing)
    {
        return std::unexpected(existing.error());
    }
    std::vector<ExecutableResource> resources = std::move(*existing);
    std::erase_if(resources, [](const ExecutableResource& resource) {
        return resource.typeName.empty() && (resource.type == iconResourceType || resource.type == iconGroupResourceType);
    });

    // As .ico files store them, but with the identifiers of the resources.
    std::vector<std::byte> group;
    appendNumber(group, 0, 2);
    appendNumber(group, 1, 2);
    appendNumber(group, images.size(), 2);
    std::uint32_t id = 1;
    for (const IconImage& image : images)
    {
        std::vector<std::byte> data = image.png.empty() ? bitmapIcon(image) : image.png;
        const std::uint64_t side = image.size >= 256 ? 0 : image.size;
        appendNumber(group, side, 1);
        appendNumber(group, side, 1);
        appendNumber(group, 0, 2);
        appendNumber(group, 1, 2);
        appendNumber(group, 32, 2);
        appendNumber(group, data.size(), 4);
        appendNumber(group, id, 2);
        resources.push_back({.type = iconResourceType, .id = id, .data = std::move(data)});
        ++id;
    }
    resources.push_back({.type = iconGroupResourceType, .id = 1, .data = std::move(group)});

    // The tree goes into a section of its own at the end, since the old one has no room to grow, and stays unused.
    const std::size_t sectionHeader = layout->sectionTable + layout->sections.size() * sectionHeaderSize;
    std::size_t firstData = bytes->size();
    std::size_t imageEnd = 0;
    for (const ExecutableSection& section : layout->sections)
    {
        if (section.rawPointer != 0)
        {
            firstData = std::min<std::size_t>(firstData, section.rawPointer);
        }
        imageEnd = std::max<std::size_t>(imageEnd, std::size_t{section.virtualAddress} + std::max(section.virtualSize, section.rawSize));
    }
    if (sectionHeader + sectionHeaderSize > firstData)
    {
        return core::makeError(core::ErrorCode::Unsupported, "'{}' has no room for one more section", core::toUtf8(executable));
    }
    const std::size_t address = alignedUp(imageEnd, layout->sectionAlignment);
    const std::vector<std::byte> section = resourceSection(resources, static_cast<std::uint32_t>(address));
    const std::size_t rawPointer = alignedUp(bytes->size(), layout->fileAlignment);
    const std::size_t rawSize = alignedUp(section.size(), layout->fileAlignment);
    bytes->resize(rawPointer + rawSize, std::byte{0});
    std::ranges::copy(section, bytes->begin() + static_cast<std::ptrdiff_t>(rawPointer));

    ExecutableBytes out(*bytes);
    constexpr std::array<std::byte, 8> sectionName{std::byte{'.'}, std::byte{'r'}, std::byte{'s'}, std::byte{'r'},
                                                   std::byte{'c'}, std::byte{'2'}, std::byte{0},   std::byte{0}};
    std::ranges::copy(sectionName, bytes->begin() + static_cast<std::ptrdiff_t>(sectionHeader));
    out.write<std::uint32_t>(sectionHeader + 8, static_cast<std::uint32_t>(section.size()));
    out.write<std::uint32_t>(sectionHeader + 12, static_cast<std::uint32_t>(address));
    out.write<std::uint32_t>(sectionHeader + 16, static_cast<std::uint32_t>(rawSize));
    out.write<std::uint32_t>(sectionHeader + 20, static_cast<std::uint32_t>(rawPointer));
    // Initialized data, which is read.
    out.write<std::uint32_t>(sectionHeader + 36, 0x40000040u);
    out.write<std::uint16_t>(layout->coffHeader + 2, static_cast<std::uint16_t>(layout->sections.size() + 1));
    out.write<std::uint32_t>(layout->optionalHeader + 56, static_cast<std::uint32_t>(alignedUp(address + section.size(), layout->sectionAlignment)));
    // Nothing checks them on a program that is not signed, and they no longer hold.
    out.write<std::uint32_t>(layout->optionalHeader + 64, 0);
    out.write<std::uint64_t>(layout->directories + certificateDirectory * 8, 0);
    out.write<std::uint32_t>(layout->directories + resourceDirectory * 8, static_cast<std::uint32_t>(address));
    out.write<std::uint32_t>(layout->directories + resourceDirectory * 8 + 4, static_cast<std::uint32_t>(section.size()));
    return core::writeFileAtomically(executable, *bytes);
}

} // namespace devex::platform
