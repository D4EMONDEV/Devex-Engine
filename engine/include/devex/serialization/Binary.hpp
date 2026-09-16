#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Little-endian binary encoding of cooked data such as imported assets. Values are written with
// their in-memory layout: only trivially copyable types without padding-dependent meaning belong
// in these streams.
namespace devex::serialization {

// The cooked formats target little-endian machines only.
static_assert(std::endian::native == std::endian::little);

template <typename T>
concept BinaryValue = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

class BinaryWriter
{
public:
    template <BinaryValue T>
    void write(const T& value)
    {
        writeBytes(std::as_bytes(std::span(&value, 1)));
    }

    void writeBytes(std::span<const std::byte> bytes)
    {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    // A 32-bit length followed by the bytes.
    void writeString(std::string_view text)
    {
        write(static_cast<std::uint32_t>(text.size()));
        writeBytes(std::as_bytes(std::span(text.data(), text.size())));
    }

    // A 64-bit element count followed by the elements.
    template <BinaryValue T>
    void writeArray(std::span<const T> values)
    {
        write(static_cast<std::uint64_t>(values.size()));
        writeBytes(std::as_bytes(values));
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return m_bytes.size();
    }

    [[nodiscard]] std::vector<std::byte> take() noexcept
    {
        return std::move(m_bytes);
    }

private:
    std::vector<std::byte> m_bytes;
};

// Reads what BinaryWriter wrote. A read past the end marks the reader as failed and returns
// zeroed values, so decoders check failed() once at the end instead of after every read.
class BinaryReader
{
public:
    explicit BinaryReader(std::span<const std::byte> bytes) noexcept
        : m_bytes(bytes)
    {
    }

    template <BinaryValue T>
    [[nodiscard]] T read() noexcept
    {
        T value{};
        readBytes(std::as_writable_bytes(std::span(&value, 1)));
        return value;
    }

    void readBytes(std::span<std::byte> destination) noexcept
    {
        if (m_failed || destination.size() > remaining())
        {
            m_failed = true;
            std::memset(destination.data(), 0, destination.size());
            return;
        }
        std::memcpy(destination.data(), m_bytes.data() + m_offset, destination.size());
        m_offset += destination.size();
    }

    [[nodiscard]] std::string readString()
    {
        const auto length = read<std::uint32_t>();
        if (m_failed || length > remaining())
        {
            m_failed = true;
            return {};
        }
        std::string text(reinterpret_cast<const char*>(m_bytes.data() + m_offset), length);
        m_offset += length;
        return text;
    }

    // Reads an array written by writeArray. A count larger than the remaining data fails before
    // allocating, so corrupt input cannot request huge allocations.
    template <BinaryValue T>
    [[nodiscard]] std::vector<T> readArray()
    {
        const auto count = read<std::uint64_t>();
        if (m_failed || count > remaining() / sizeof(T))
        {
            m_failed = true;
            return {};
        }
        std::vector<T> values(static_cast<std::size_t>(count));
        readBytes(std::as_writable_bytes(std::span(values)));
        return values;
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return m_failed;
    }

    [[nodiscard]] std::size_t remaining() const noexcept
    {
        return m_bytes.size() - m_offset;
    }

    [[nodiscard]] std::size_t offset() const noexcept
    {
        return m_offset;
    }

    // Marks the data as invalid, for instance after reading an unknown enumerator.
    void fail() noexcept
    {
        m_failed = true;
    }

private:
    std::span<const std::byte> m_bytes;
    std::size_t m_offset = 0;
    bool m_failed = false;
};

} // namespace devex::serialization
