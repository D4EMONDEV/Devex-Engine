#pragma once

#include <devex/core/Error.hpp>
#include <devex/core/Export.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/math/Math.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// Remote calls: a method of an entity, called by name, that runs on the other players' games. The same
// entity has the same identifier on every player's game, which is how a call finds it.
namespace devex::net {

// Who may call a method over the network, and how the call travels.
struct RpcPolicy
{
    // Any player may call it, rather than only the authority of the entity.
    bool anyPeer = false;
    // It also runs on the game that calls it, rather than only on the others.
    bool callLocal = false;
    // Arrives, in order. Otherwise it may be lost, which suits what the next call replaces.
    bool reliable = true;
};

// What a call carries. The position of a type here is its number on the network, which C# shares
// (managed/Devex.Managed/Rpc.cs): new types go at the end.
using RpcValue = std::variant<bool, std::int64_t, double, std::string, math::Vec2, math::Vec3, math::Vec4, math::Quat, core::Uuid, std::vector<std::byte>>;

struct RpcCall
{
    // The entity it is for.
    core::Uuid target;
    // "Type.Method": C# names its methods so, and C++ game code picks the same form.
    std::string name;
    std::vector<RpcValue> arguments;
};

// The start of a call, enough to tell whether to accept it without reading the rest.
struct RpcHeader
{
    core::Uuid target;
    std::string_view name;
};

inline constexpr std::size_t maxRpcNameLength = 256;
inline constexpr std::size_t maxRpcArguments = 32;

// Fails on a name or a list of arguments over the limits above.
[[nodiscard]] DEVEX_API core::Result<std::vector<std::byte>> encodeRpc(const RpcCall& call);
// What comes from the network is not trusted: anything malformed is an error, and nothing is read
// out of bounds.
[[nodiscard]] DEVEX_API core::Result<RpcCall> decodeRpc(std::span<const std::byte> bytes);
// The name it gives points into the bytes.
[[nodiscard]] DEVEX_API std::optional<RpcHeader> decodeRpcHeader(std::span<const std::byte> bytes);

// A value of a game as a call carries it: what is not one of the types above does not compile.
template <typename T>
[[nodiscard]] RpcValue toRpcValue(const T& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        return RpcValue(std::in_place_type<bool>, value);
    }
    else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>)
    {
        return RpcValue(std::in_place_type<std::int64_t>, static_cast<std::int64_t>(value));
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        return RpcValue(std::in_place_type<double>, static_cast<double>(value));
    }
    else if constexpr (std::is_convertible_v<const T&, std::string_view>)
    {
        return RpcValue(std::in_place_type<std::string>, std::string_view(value));
    }
    else
    {
        return RpcValue(std::in_place_type<T>, value);
    }
}

// A value a call carries as the type a method asks for, none when it is another kind of value or does
// not fit. A whole number makes a decimal one, as "Hit(3)" for a float parameter, but not the reverse.
template <typename T>
[[nodiscard]] std::optional<T> fromRpcValue(const RpcValue& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        const bool* const found = std::get_if<bool>(&value);
        return found != nullptr ? std::optional<T>(*found) : std::nullopt;
    }
    else if constexpr (std::is_enum_v<T>)
    {
        const std::optional<std::underlying_type_t<T>> number = fromRpcValue<std::underlying_type_t<T>>(value);
        return number ? std::optional<T>(static_cast<T>(*number)) : std::nullopt;
    }
    else if constexpr (std::is_integral_v<T>)
    {
        const std::int64_t* const found = std::get_if<std::int64_t>(&value);
        return found != nullptr && std::in_range<T>(*found) ? std::optional<T>(static_cast<T>(*found)) : std::nullopt;
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        if (const double* const decimal = std::get_if<double>(&value))
        {
            return static_cast<T>(*decimal);
        }
        const std::int64_t* const whole = std::get_if<std::int64_t>(&value);
        return whole != nullptr ? std::optional<T>(static_cast<T>(*whole)) : std::nullopt;
    }
    else
    {
        const T* const found = std::get_if<T>(&value);
        return found != nullptr ? std::optional<T>(*found) : std::nullopt;
    }
}

} // namespace devex::net
