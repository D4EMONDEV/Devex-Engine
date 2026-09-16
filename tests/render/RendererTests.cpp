#include <devex/asset/Primitives.hpp>
#include <devex/core/Log.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Renderer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>
#include <vector>

using devex::core::LogLevel;
using devex::core::LogRecord;

namespace {

// Collects error messages, including those reported by the Vulkan validation layer.
class ErrorCapture
{
public:
    ErrorCapture()
        : m_sink(devex::core::addLogSink([this](const LogRecord& record) {
            if (record.level >= LogLevel::Error)
            {
                m_errors.emplace_back(record.message);
            }
        }))
    {
    }

    ~ErrorCapture()
    {
        devex::core::removeLogSink(m_sink);
    }

    ErrorCapture(const ErrorCapture&) = delete;
    ErrorCapture& operator=(const ErrorCapture&) = delete;

    [[nodiscard]] const std::vector<std::string>& errors() const noexcept
    {
        return m_errors;
    }

private:
    std::vector<std::string> m_errors;
    devex::core::LogSinkId m_sink;
};

} // namespace

TEST_CASE("The renderer presents frames without validation errors", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());

        auto window = platform->createWindow({
            .title = "Devex render tests",
            .width = 320,
            .height = 240,
            .vulkan = true,
            .hidden = true,
        });
        REQUIRE(window.has_value());

        auto renderer = devex::render::Renderer::create(*platform, *window, {
            .applicationName = "Devex render tests",
            .validation = true,
        });
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        for (int frame = 0; frame < 5; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.clearColor = {0.1f * static_cast<float>(frame), 0.2f, 0.3f, 1.0f};

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
        }

        CHECK_FALSE(renderer->gpu().name.empty());
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Meshes are drawn and destroyed without validation errors", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        auto cube = renderer->createMesh(devex::asset::makeCube());
        auto sphere = renderer->createMesh(devex::asset::makeUvSphere());
        REQUIRE(cube.has_value());
        REQUIRE(sphere.has_value());
        CHECK_FALSE(renderer->createMesh(devex::asset::MeshData{}).has_value());

        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.0f, 3.0f});
        for (int frame = 0; frame < 6; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.meshes.push_back({.mesh = *cube});
            if (frame < 3)
            {
                world.meshes.push_back({
                    .mesh = *sphere,
                    .transform = devex::math::translate(devex::math::Mat4{1.0f}, {1.5f, 0.0f, 0.0f}),
                });
            }

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            // The sphere is released while frames that draw it may still be in flight.
            if (frame == 2)
            {
                renderer->destroyMesh(*sphere);
            }
        }
        // Destroying a stale handle is ignored.
        renderer->destroyMesh(*sphere);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Textured materials draw submeshes and survive texture removal", "[render][gpu]")
{
    const ErrorCapture capture;
    {
        auto platform = devex::platform::Platform::create();
        REQUIRE(platform.has_value());
        auto window = platform->createWindow({.width = 320, .height = 240, .vulkan = true, .hidden = true});
        REQUIRE(window.has_value());
        auto renderer = devex::render::Renderer::create(*platform, *window, {.validation = true});
        if (!renderer)
        {
            FAIL(std::format("{}", renderer.error()));
        }

        // Two submeshes: the first two faces of the cube, then the rest.
        devex::asset::MeshData cubeData = devex::asset::makeCube();
        cubeData.submeshes = {{.firstIndex = 0, .indexCount = 12},
                              {.firstIndex = 12, .indexCount = 24}};
        auto cube = renderer->createMesh(cubeData);
        REQUIRE(cube.has_value());
        CHECK(renderer->submeshCount(*cube) == 2);

        // A 2x2 texture with its mip, and a block-compressed one with a partial block.
        devex::asset::TextureData checker{.format = devex::asset::TextureFormat::Rgba8Srgb};
        checker.mips.push_back({.width = 2, .height = 2, .bytes = std::vector<std::byte>(16, std::byte{200})});
        checker.mips.push_back({.width = 1, .height = 1, .bytes = std::vector<std::byte>(4, std::byte{100})});
        devex::asset::TextureData compressed{.format = devex::asset::TextureFormat::Bc7Srgb};
        compressed.mips.push_back({.width = 5, .height = 3, .bytes = std::vector<std::byte>(2 * 16)});
        auto checkerTexture = renderer->createTexture(checker);
        auto compressedTexture = renderer->createTexture(compressed);
        REQUIRE(checkerTexture.has_value());
        REQUIRE(compressedTexture.has_value());
        compressed.mips.front().bytes.pop_back();
        CHECK_FALSE(renderer->createTexture(compressed).has_value());

        const devex::render::MaterialHandle opaque = renderer->createMaterial({
            .baseColorTexture = *checkerTexture,
            .emissiveTexture = *compressedTexture,
        });
        const devex::render::MaterialHandle cutout = renderer->createMaterial({
            .baseColorFactor = {1.0f, 0.5f, 0.2f, 0.4f},
            .alphaMode = devex::asset::AlphaMode::Mask,
            .doubleSided = true,
        });
        CHECK(renderer->stats().textureCount == 2);
        CHECK(renderer->stats().materialCount == 2);

        const devex::math::Mat4 cameraTransform =
            devex::math::translate(devex::math::Mat4{1.0f}, {0.0f, 0.0f, 3.0f});
        for (int frame = 0; frame < 8; ++frame)
        {
            devex::render::RenderWorld& world = renderer->beginFrame();
            world.camera.view = devex::math::inverse(cameraTransform);
            world.meshes.push_back({.mesh = *cube, .submesh = 0, .material = opaque});
            world.meshes.push_back({.mesh = *cube, .submesh = 1, .material = cutout});
            // Out of range submeshes and unknown materials are tolerated.
            world.meshes.push_back({.mesh = *cube, .submesh = 7});

            const devex::core::Result<void> presented = renderer->endFrame();
            if (!presented)
            {
                FAIL(std::format("frame {}: {}", frame, presented.error()));
            }
            if (frame == 2)
            {
                // The material falls back to the white texture while frames still use the old one.
                renderer->destroyTexture(*checkerTexture);
                renderer->updateMaterial(cutout, {.baseColorFactor = {0.0f, 1.0f, 0.0f, 1.0f}});
            }
            if (frame == 5)
            {
                renderer->destroyMaterial(opaque);
            }
        }
        CHECK(renderer->stats().drawCalls == 2);
        CHECK(renderer->stats().textureCount == 1);
        CHECK(renderer->stats().materialCount == 1);
    }

    for (const std::string& error : capture.errors())
    {
        UNSCOPED_INFO(error);
    }
    CHECK(capture.errors().empty());
}

TEST_CASE("Only one renderer can exist at a time", "[render][gpu]")
{
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.vulkan = true, .hidden = true});
    REQUIRE(window.has_value());

    auto first = devex::render::Renderer::create(*platform, *window, {.validation = false});
    REQUIRE(first.has_value());

    auto second = devex::render::Renderer::create(*platform, *window, {.validation = false});
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code == devex::core::ErrorCode::InvalidState);
}
