#include <devex/asset/AssetSource.hpp>
#include <devex/asset/Project.hpp>
#include <devex/runtime/AssetManager.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using devex::asset::AssetId;
using devex::asset::AssetInfo;
using devex::asset::AssetType;

namespace {

// Two scenes of known size, without a project on disk.
class TwoScenes final : public devex::asset::AssetSource
{
public:
    TwoScenes()
    {
        m_infos.push_back({.id = small, .type = AssetType::Scene, .name = "small", .source = small});
        m_infos.push_back({.id = large, .type = AssetType::Scene, .name = "large", .source = large});
    }

    [[nodiscard]] const devex::asset::Project& project() const noexcept override
    {
        return m_project;
    }

    [[nodiscard]] const AssetInfo* find(AssetId id) const override
    {
        for (const AssetInfo& info : m_infos)
        {
            if (info.id == id)
            {
                return &info;
            }
        }
        return nullptr;
    }

    [[nodiscard]] std::vector<AssetInfo> assets(std::optional<AssetType> /*type*/) const override
    {
        return m_infos;
    }

    [[nodiscard]] std::optional<AssetId> findByPath(std::string_view /*resourcePath*/) const override
    {
        return std::nullopt;
    }

    [[nodiscard]] devex::core::Result<std::vector<std::byte>> loadArtifact(AssetId /*id*/) const override
    {
        return devex::core::makeError(devex::core::ErrorCode::NotFound, "no artifacts here");
    }

    [[nodiscard]] devex::core::Result<std::string> sceneText(AssetId id) const override
    {
        return std::string(id == large ? 3000 : 1000, 'x');
    }

    const AssetId small = AssetId::generate();
    const AssetId large = AssetId::generate();

private:
    devex::asset::Project m_project;
    std::vector<AssetInfo> m_infos;
};

} // namespace

TEST_CASE("The memory the loaded assets take is told by type and heaviest first", "[runtime][assets]")
{
    TwoScenes source;
    devex::runtime::AssetManager assets(nullptr, &source);
    CHECK(assets.memoryReport().types.empty());

    REQUIRE(assets.sceneText(source.small).has_value());
    REQUIRE(assets.sceneText(source.large).has_value());
    const devex::asset::MemoryReport report = assets.memoryReport();

    REQUIRE(report.types.size() == 1);
    CHECK(report.types[0].type == AssetType::Scene);
    CHECK(report.types[0].count == 2);
    CHECK(report.types[0].cpuBytes == 4000);
    CHECK(report.types[0].gpuBytes == 0);

    REQUIRE(report.largest.size() == 2);
    CHECK(report.largest[0].name == "large");
    CHECK(report.largest[0].cpuBytes == 3000);
    CHECK(report.largest[1].name == "small");

    // Only as many of the heaviest as asked for.
    CHECK(assets.memoryReport(1).largest.size() == 1);
}
