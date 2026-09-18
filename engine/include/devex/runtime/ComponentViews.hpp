#pragma once

#include <devex/reflection/Reflection.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// C# code reaches the components written in C++ through views generated from their reflection: a
// ref struct over the memory of one component, whose properties read and write its fields in place.
//
//     ref float mass = ref entity.Get<RigidBody>().Mass;
//
// The views of the engine's components are generated when the engine is built, into Devex.Managed;
// those of a game's C++ components by the editor, into the C# project of the game. Each view carries
// the layout hash of its type, which the runtime compares with the running engine's before giving
// access, so that a view generated for another layout never touches the memory.
namespace devex::runtime {

// A hash of what a view relies on: the name and size of the type, and the name, kind, offset, list
// and enumeration values of each field.
[[nodiscard]] std::uint64_t componentLayoutHash(const reflection::TypeInfo& type);

// The C# source of the views of the types, in the namespace (the global one when empty). Types with
// a field whose offset is unknown are skipped, and Transform, which Devex.Managed declares itself.
// `origin` names what the types come from, in the header comment.
[[nodiscard]] std::string generateComponentViews(std::span<const reflection::TypeInfo* const> types,
                                                 std::string_view csharpNamespace, std::string_view origin);

// "cast_shadows" becomes "CastShadows", as C# names members.
[[nodiscard]] std::string pascalCase(std::string_view name);

} // namespace devex::runtime
