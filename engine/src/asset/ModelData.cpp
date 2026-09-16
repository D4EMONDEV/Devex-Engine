#include <devex/asset/ModelData.hpp>

namespace devex::asset {

core::Result<void> validate(const ModelData& model)
{
    for (std::size_t index = 0; index < model.nodes.size(); ++index)
    {
        const std::int32_t parent = model.nodes[index].parent;
        if (parent < -1 || parent >= static_cast<std::int64_t>(index))
        {
            return core::makeError(core::ErrorCode::InvalidArgument,
                                   "node {} has parent {}, which does not come before it", index,
                                   parent);
        }
    }
    return {};
}

} // namespace devex::asset
