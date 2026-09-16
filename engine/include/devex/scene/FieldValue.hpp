#pragma once

#include <devex/core/Error.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/serialization/Text.hpp>

// Conversions between reflected fields and text values, shared by scene files and editor
// commands.
namespace devex::scene {

// Reads the field at `address`, whose C++ type matches `kind`.
[[nodiscard]] serialization::TextValue writeFieldValue(reflection::ValueKind kind,
                                                       const void* address);

// Writes `value` into the field at `address`, or explains which value was expected.
[[nodiscard]] core::Result<void> readFieldValue(reflection::ValueKind kind,
                                                const serialization::TextValue& value,
                                                void* address);

} // namespace devex::scene
