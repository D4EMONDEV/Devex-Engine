#pragma once

#include <devex/core/Error.hpp>
#include <devex/reflection/Reflection.hpp>
#include <devex/serialization/Text.hpp>

// Conversions between reflected fields and text values, shared by scene files and editor
// commands.
namespace devex::scene {

// Reads the field at `address`, whose C++ type matches `kind`. Enumerations need the field.
[[nodiscard]] serialization::TextValue writeFieldValue(reflection::ValueKind kind,
                                                       const void* address);

// Writes `value` into the field at `address`, or explains which value was expected.
// Enumerations need the field.
[[nodiscard]] core::Result<void> readFieldValue(reflection::ValueKind kind,
                                                const serialization::TextValue& value,
                                                void* address);

// The same for any field, enumerations included, which are written by name.
[[nodiscard]] serialization::TextValue writeFieldValue(const reflection::FieldInfo& field,
                                                       const void* address);
[[nodiscard]] core::Result<void> readFieldValue(const reflection::FieldInfo& field,
                                                const serialization::TextValue& value,
                                                void* address);

} // namespace devex::scene
