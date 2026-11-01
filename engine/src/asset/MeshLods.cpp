#include <devex/asset/MeshData.hpp>

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace devex::asset
{

void generateMeshLods(MeshData& mesh, std::uint32_t levels)
{
    mesh.lods.clear();
    if (!validate(mesh) || mesh.indices.size() < 96 || levels == 0)
        return;
    const bool skinned = !mesh.skin.empty();
    if (skinned && std::ranges::any_of(mesh.skin,
                                       [](const VertexSkin& skin)
                                       {
                                           return skin.weights.x < 0 || skin.weights.y < 0 ||
                                                  skin.weights.z < 0 || skin.weights.w < 0;
                                       }))
        return;
    const std::size_t attributes = skinned ? 9 : 5;
    std::vector<float> values(mesh.vertices.size() * attributes);
    std::vector<std::array<std::uint16_t, 4>> palettes(mesh.vertices.size());
    std::vector<unsigned char> locked(mesh.vertices.size(), 0);
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index)
    {
        const auto& vertex = mesh.vertices[index];
        float* v = values.data() + index * attributes;
        v[0] = vertex.normal.x;
        v[1] = vertex.normal.y;
        v[2] = vertex.normal.z;
        v[3] = vertex.uv.x;
        v[4] = vertex.uv.y;
        if (!skinned)
            continue;
        std::array<std::pair<std::uint16_t, float>, 4> influences{};
        for (int slot = 0; slot < 4; ++slot)
            influences[slot] = {mesh.skin[index].weights[slot] > 0
                                    ? mesh.skin[index].joints[slot]
                                    : std::numeric_limits<std::uint16_t>::max(),
                                mesh.skin[index].weights[slot]};
        std::ranges::sort(influences);
        for (std::size_t slot = 0; slot < 4; ++slot)
        {
            palettes[index][slot] = influences[slot].first;
            v[5 + slot] = influences[slot].second;
        }
    }
    // Joint IDs are discrete, not numeric attributes. Preserve seams between different palettes;
    // within a palette the weights participate in the simplifier's attribute error.
    if (skinned)
        for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3)
            for (std::size_t edge = 0; edge < 3; ++edge)
            {
                const auto a = mesh.indices[triangle + edge];
                const auto b = mesh.indices[triangle + (edge + 1) % 3];
                if (palettes[a] != palettes[b])
                    locked[a] = locked[b] = 1;
            }
    const float scale = meshopt_simplifyScale(&mesh.vertices.front().position.x,
                                              mesh.vertices.size(), sizeof(Vertex));
    if (!(scale > 0.0f) || !std::isfinite(scale))
        return;
    const std::array<float, 9> weights{scale * 0.1f, scale * 0.1f, scale * 0.1f,
                                       scale * 0.1f, scale * 0.1f, scale,
                                       scale,        scale,        scale};
    const auto parts = submeshesOf(mesh);
    for (std::uint32_t level = 1; level <= std::min(levels, 4u); ++level)
    {
        MeshLod lod;
        for (const auto& part : parts)
        {
            std::vector<std::uint32_t> simplified(part.indexCount);
            float error = 0.0f;
            const auto target = std::max<std::size_t>(3, (part.indexCount >> level) / 3 * 3);
            const auto count = meshopt_simplifyWithAttributes(
                simplified.data(), mesh.indices.data() + part.firstIndex, part.indexCount,
                &mesh.vertices.front().position.x, mesh.vertices.size(), sizeof(Vertex),
                values.data(), attributes * sizeof(float), weights.data(), attributes,
                locked.data(), target, scale * 0.05f,
                meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute, &error);
            if (!std::isfinite(error))
                return;
            if (count == 0)
            {
                simplified.assign(mesh.indices.begin() + part.firstIndex,
                                  mesh.indices.begin() + part.firstIndex + part.indexCount);
            }
            else
                simplified.resize(count);
            meshopt_optimizeVertexCache(simplified.data(), simplified.data(), simplified.size(),
                                        mesh.vertices.size());
            lod.submeshes.push_back({.firstIndex = static_cast<std::uint32_t>(lod.indices.size()),
                                     .indexCount = static_cast<std::uint32_t>(simplified.size()),
                                     .material = part.material});
            lod.indices.insert(lod.indices.end(), simplified.begin(), simplified.end());
            lod.error = std::max({lod.error, error, scale * 0.001f * float(1u << level)});
        }
        const auto previous =
            mesh.lods.empty() ? mesh.indices.size() : mesh.lods.back().indices.size();
        if (lod.indices.size() >= previous * 95 / 100)
            break;
        std::vector<std::uint32_t> remap(mesh.vertices.size(), ~std::uint32_t{0});
        for (auto& index : lod.indices)
        {
            if (remap[index] == ~std::uint32_t{0})
            {
                remap[index] = static_cast<std::uint32_t>(lod.vertices.size());
                lod.vertices.push_back(mesh.vertices[index]);
                if (skinned)
                    lod.skin.push_back(mesh.skin[index]);
            }
            index = remap[index];
        }
        if (!mesh.lods.empty())
            lod.error = std::max(lod.error, mesh.lods.back().error);
        mesh.lods.push_back(std::move(lod));
    }
}

} // namespace devex::asset
