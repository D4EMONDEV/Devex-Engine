#include <devex/asset/Artifact.hpp>
#include <devex/asset/AssetSource.hpp>
#include <devex/asset/Primitives.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/JobSystem.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/runtime/AssetManager.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <format>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

using devex::asset::AssetId;
using devex::asset::AssetInfo;
using devex::asset::AssetType;

namespace {

// Artifacts in memory, which a test may change while workers read them.
class MemorySource final : public devex::asset::AssetSource
{
public:
    void set(AssetId id, AssetType type, std::vector<std::byte> bytes)
    {
        const std::scoped_lock lock(m_mutex);
        m_infos.insert_or_assign(id, AssetInfo{.id = id, .type = type, .name = std::format("{}", id.uuid), .source = id});
        m_bytes.insert_or_assign(id, std::move(bytes));
    }

    void setBroken(AssetId id, AssetType type)
    {
        const std::scoped_lock lock(m_mutex);
        m_infos.insert_or_assign(id, AssetInfo{.id = id, .type = type, .name = "broken", .source = id});
        m_bytes.erase(id);
    }

    [[nodiscard]] const devex::asset::Project& project() const noexcept override
    {
        return m_project;
    }

    [[nodiscard]] const AssetInfo* find(AssetId id) const override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_infos.find(id);
        return found != m_infos.end() ? &found->second : nullptr;
    }

    [[nodiscard]] std::vector<AssetInfo> assets(std::optional<AssetType> /*type*/) const override
    {
        return {};
    }

    [[nodiscard]] std::optional<AssetId> findByPath(std::string_view /*resourcePath*/) const override
    {
        return std::nullopt;
    }

    [[nodiscard]] devex::core::Result<std::vector<std::byte>> loadArtifact(AssetId id) const override
    {
        const std::scoped_lock lock(m_mutex);
        const auto found = m_bytes.find(id);
        if (found == m_bytes.end())
        {
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "no artifact for {}", id.uuid);
        }
        return found->second;
    }

    [[nodiscard]] devex::core::Result<std::string> sceneText(AssetId id) const override
    {
        return devex::core::makeError(devex::core::ErrorCode::NotFound, "no scene {}", id.uuid);
    }

private:
    mutable std::mutex m_mutex;
    devex::asset::Project m_project;
    std::unordered_map<AssetId, AssetInfo> m_infos;
    std::unordered_map<AssetId, std::vector<std::byte>> m_bytes;
};

[[nodiscard]] devex::asset::TextureData smallTexture()
{
    devex::asset::TextureData texture{.format = devex::asset::TextureFormat::Rgba8Srgb};
    texture.mips.push_back({.width = 4, .height = 4, .bytes = std::vector<std::byte>(4 * 4 * 4, std::byte{180})});
    return texture;
}

// Renders one frame drawing the mesh, which copies what waits to the GPU.
void renderFrame(devex::render::Renderer& renderer, devex::render::MeshHandle mesh)
{
    devex::render::RenderWorld& world = renderer.beginFrame();
    world.meshes.push_back({.mesh = mesh});
    const devex::core::Result<void> presented = renderer.endFrame();
    REQUIRE(presented.has_value());
}

} // namespace

TEST_CASE("Meshes and textures load on the workers, then reach the GPU", "[runtime][assets][gpu]")
{
    std::vector<std::string> errors;
    const devex::core::LogSinkId sink = devex::core::addLogSink([&errors](const devex::core::LogRecord& record) {
        if (record.level >= devex::core::LogLevel::Error)
        {
            errors.emplace_back(record.message);
        }
    });
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        REQUIRE(renderer.has_value());

        const AssetId mesh = AssetId::generate();
        const AssetId texture = AssetId::generate();
        const AssetId material = AssetId::generate();
        const AssetId other = AssetId::generate();
        MemorySource source;
        source.set(mesh, AssetType::Mesh, devex::asset::encodeMesh(devex::asset::makeCube()));
        source.set(texture, AssetType::Texture, devex::asset::encodeTexture(smallTexture()));
        source.set(other, AssetType::Texture, devex::asset::encodeTexture(smallTexture()));
        source.set(material, AssetType::Material, devex::asset::encodeMaterial({.baseColorTexture = texture}));

        devex::core::JobSystem jobs(2);
        devex::runtime::AssetManager assets(&*renderer, &source, &jobs);

        // Asking starts the load and answers nothing yet.
        CHECK(assets.mesh(mesh) == nullptr);
        CHECK(assets.pendingLoads() == 1);
        CHECK_FALSE(assets.isReady(mesh));
        // A material is there at once; its texture follows.
        CHECK(assets.material(material).isValid());
        CHECK_FALSE(assets.isReady(material));
        CHECK(assets.pendingLoads() == 2);

        assets.waitForLoads();
        CHECK(assets.pendingLoads() == 0);
        const devex::runtime::LoadedMesh* const loaded = assets.mesh(mesh);
        REQUIRE(loaded != nullptr);
        CHECK(assets.texture(texture).isValid());
        // Handed to the renderer, not yet copied.
        CHECK_FALSE(assets.isReady(mesh));
        renderFrame(*renderer, loaded->handle);
        CHECK(assets.isReady(mesh));
        CHECK(assets.isReady(texture));
        CHECK(assets.isReady(material));

        // A reimported mesh: the previous version stays until the new one is loaded.
        const devex::render::MeshHandle before = loaded->handle;
        source.set(mesh, AssetType::Mesh, devex::asset::encodeMesh(devex::asset::makeUvSphere()));
        const std::array events{devex::asset::AssetEvent{.id = mesh, .type = AssetType::Mesh}};
        assets.handleEvents(events);
        REQUIRE(assets.mesh(mesh) != nullptr);
        CHECK(assets.mesh(mesh)->handle == before);
        assets.waitForLoads();
        REQUIRE(assets.mesh(mesh) != nullptr);
        CHECK(assets.mesh(mesh)->handle != before);
        renderFrame(*renderer, assets.mesh(mesh)->handle);

        // A source that goes away while its assets load leaves nothing behind.
        assets.preload(other);
        CHECK(assets.pendingLoads() == 1);
        assets.setSource(nullptr);
        CHECK(assets.pendingLoads() == 0);
        assets.finishLoads();
        CHECK_FALSE(assets.texture(other).isValid());
        renderFrame(*renderer, devex::render::MeshHandle{});
    }
    devex::core::removeLogSink(sink);
    for (const std::string& error : errors)
    {
        UNSCOPED_INFO(error);
    }
    CHECK(errors.empty());
}

TEST_CASE("An asset that fails to load is ready at once and is not asked for again", "[runtime][assets][gpu]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
    REQUIRE(window.has_value());
    auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
    REQUIRE(renderer.has_value());

    const AssetId broken = AssetId::generate();
    const AssetId mesh = AssetId::generate();
    MemorySource source;
    source.setBroken(broken, AssetType::Mesh);
    source.set(mesh, AssetType::Mesh, devex::asset::encodeMesh(devex::asset::makeCube()));

    devex::core::JobSystem jobs(1);
    devex::runtime::AssetManager assets(&*renderer, &source, &jobs);
    CHECK(assets.mesh(broken) == nullptr);
    assets.waitForLoads();
    CHECK(assets.mesh(broken) == nullptr);
    CHECK(assets.pendingLoads() == 0);
    // Waiting for it would wait forever.
    CHECK(assets.isReady(broken));

    // Without a job system, everything loads when it is asked for.
    devex::runtime::AssetManager synchronous(&*renderer, &source);
    CHECK(synchronous.mesh(mesh) != nullptr);
    CHECK(synchronous.pendingLoads() == 0);
}
