#pragma once

#include <devex/core/Uuid.hpp>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// What travels on the network is written byte by byte, numbers in little endian, whatever the machine.
namespace devex::net {

class ByteWriter
{
public:
    explicit ByteWriter(std::vector<std::byte>& bytes) noexcept
        : m_bytes(bytes)
    {
    }

    void number(std::uint64_t value, std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            m_bytes.push_back(static_cast<std::byte>(value >> (8 * index)));
        }
    }

    void decimal(float value)
    {
        number(std::bit_cast<std::uint32_t>(value), sizeof(float));
    }

    void wide(double value)
    {
        number(std::bit_cast<std::uint64_t>(value), sizeof(double));
    }

    void raw(std::span<const std::byte> bytes)
    {
        m_bytes.insert(m_bytes.end(), bytes.begin(), bytes.end());
    }

    void uuid(const core::Uuid& value)
    {
        raw(std::as_bytes(std::span(value.bytes())));
    }

    // With its length on 4 bytes first.
    void sized(std::span<const std::byte> bytes)
    {
        number(bytes.size(), sizeof(std::uint32_t));
        raw(bytes);
    }

    void text(std::string_view value)
    {
        sized(std::as_bytes(std::span(value.data(), value.size())));
    }

private:
    std::vector<std::byte>& m_bytes;
};

// Reads what comes from the network, which is not trusted: past the end, every read gives nothing and
// it stays failed.
class ByteReader
{
public:
    explicit ByteReader(std::span<const std::byte> bytes) noexcept
        : m_bytes(bytes)
    {
    }

    [[nodiscard]] bool failed() const noexcept
    {
        return m_failed;
    }

    [[nodiscard]] bool atEnd() const noexcept
    {
        return m_at == m_bytes.size();
    }

    [[nodiscard]] std::span<const std::byte> take(std::size_t size) noexcept
    {
        if (m_failed || size > m_bytes.size() - m_at)
        {
            m_failed = true;
            return {};
        }
        const std::span<const std::byte> taken = m_bytes.subspan(m_at, size);
        m_at += size;
        return taken;
    }

    [[nodiscard]] std::uint64_t number(std::size_t size) noexcept
    {
        std::uint64_t value = 0;
        const std::span<const std::byte> taken = take(size);
        for (std::size_t index = 0; index < taken.size(); ++index)
        {
            value |= std::to_integer<std::uint64_t>(taken[index]) << (8 * index);
        }
        return value;
    }

    [[nodiscard]] float decimal() noexcept
    {
        return std::bit_cast<float>(static_cast<std::uint32_t>(number(sizeof(float))));
    }

    [[nodiscard]] double wide() noexcept
    {
        return std::bit_cast<double>(number(sizeof(double)));
    }

    [[nodiscard]] core::Uuid uuid() noexcept
    {
        const std::uint64_t high = bigNumber();
        const std::uint64_t low = bigNumber();
        return core::Uuid::fromParts(high, low);
    }

    [[nodiscard]] std::span<const std::byte> sized() noexcept
    {
        return take(static_cast<std::size_t>(number(sizeof(std::uint32_t))));
    }

    [[nodiscard]] std::string_view text() noexcept
    {
        const std::span<const std::byte> value = sized();
        return {reinterpret_cast<const char*>(value.data()), value.size()};
    }

private:
    // An identifier keeps its bytes in the order the engine stores them, the first one the highest.
    [[nodiscard]] std::uint64_t bigNumber() noexcept
    {
        std::uint64_t value = 0;
        for (const std::byte byte : take(sizeof(std::uint64_t)))
        {
            value = (value << 8) | std::to_integer<std::uint64_t>(byte);
        }
        return value;
    }

    std::span<const std::byte> m_bytes;
    std::size_t m_at = 0;
    bool m_failed = false;
};

} // namespace devex::net
