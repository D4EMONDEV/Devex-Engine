#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Error.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace devex::asset {

struct ModelNode
{
    std::string name;
    // Index of the parent node, or -1 for a root.
    std::int32_t parent = -1;
    math::Vec3 translation{0.0f};
    math::Quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec3 scale{1.0f};
    // Invalid when the node only groups or places its children.
    AssetId mesh;
};

// The node hierarchy of an imported scene file, such as the default scene of a glTF file.
struct ModelData
{
    // Every parent comes before its children.
    std::vector<ModelNode> nodes;
};

[[nodiscard]] core::Result<void> validate(const ModelData& model);

} // namespace devex::asset
