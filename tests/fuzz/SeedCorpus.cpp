#include <devex/asset/Artifact.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>

#include <filesystem>
#include <string_view>
#include <utility>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        DEVEX_LOG_ERROR("Usage: devex_fuzz_seed_corpus <corpus-directory>");
        return 1;
    }
    const std::filesystem::path root{argv[1]};
    const auto write = [&](const std::filesystem::path& path, const auto& bytes) {
        const auto result = devex::core::writeFileAtomically(root / path, bytes);
        if (!result)
        {
            DEVEX_LOG_ERROR("{}", result.error().message);
        }
        return result.has_value();
    };
    const auto terrain = devex::asset::makeFlatTerrain(33);
    auto mesh = devex::asset::makePlane();
    if (!write("terrain/flat", devex::asset::encodeTerrain(terrain)) ||
        !write("mesh/plane", devex::asset::encodeMesh(mesh)))
    {
        return 1;
    }
    mesh.inverseBind.emplace_back(1.0f);
    mesh.skin.resize(mesh.vertices.size());
    for (auto& skin : mesh.skin)
    {
        skin.weights.x = 1.0f;
    }
    if (!write("mesh/skinned", devex::asset::encodeMesh(mesh)))
    {
        return 1;
    }
    for (const auto& [name, text] : {
             std::pair<std::string_view, std::string_view>{"scene/empty", "[scene format=1]\n"},
             {"scene/transform", "[scene format=1 kind=\"3d\"]\n"
                                 "[entity uuid=\"6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23\" name=\"Seed\"]\n"
                                 "[component type=\"Transform\"]\nposition = vec3(0, 1, -2)\n"},
             {"scene/unknown", "[scene format=1]\n"
                               "[entity uuid=\"6f1c2a9e-3b7d-4e21-9a55-0c8d7e4f1b23\" name=\"Seed\"]\n"
                               "[component type=\"GameData\"]\nvalues = list(1, -0.0, list(3))\n"}})
    {
        const auto result = devex::core::writeTextFile(root / name, text);
        if (!result)
        {
            DEVEX_LOG_ERROR("{}", result.error().message);
            return 1;
        }
    }
    return 0;
}
