#include <devex/asset/Artifact.hpp>
#include <devex/asset/Package.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/scene/Components.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <format>

using devex::core::Duration;
using devex::core::Result;
using devex::runtime::ApplicationConfig;

namespace {

const ApplicationConfig testConfig{
    .title = "Devex tests",
    .width = 320,
    .height = 240,
    .enableRendering = false,
};

// Counts lifecycle calls and quits after a few frames.
class CountingApplication final : public devex::runtime::Application
{
public:
    int startups = 0;
    int updates = 0;
    int shutdowns = 0;
    bool windowMatchedConfig = false;

    Result<void> onStartup() override
    {
        ++startups;
        windowMatchedConfig =
            window().size() == devex::math::Extent2D{testConfig.width, testConfig.height};
        return {};
    }

    void onUpdate(Duration /*frameDelta*/) override
    {
        if (++updates == 3)
        {
            requestQuit();
        }
    }

    void onShutdown() override
    {
        ++shutdowns;
    }
};

class FailingApplication final : public devex::runtime::Application
{
public:
    int updates = 0;
    int shutdowns = 0;

    Result<void> onStartup() override
    {
        return devex::core::makeError(devex::core::ErrorCode::InvalidState, "startup refused");
    }

    void onUpdate(Duration /*frameDelta*/) override
    {
        ++updates;
    }

    void onShutdown() override
    {
        ++shutdowns;
    }
};

} // namespace

// Checks that the runtime updates scene transforms between onUpdate calls.
class SceneApplication final : public devex::runtime::Application
{
public:
    bool worldTransformUpdated = false;

    Result<void> onStartup() override
    {
        m_entity = scene().createEntity("Moved");
        scene().add<devex::scene::Transform>(
            m_entity, devex::scene::Transform{.position = {1.0f, 2.0f, 3.0f}});
        return {};
    }

    void onUpdate(Duration /*frameDelta*/) override
    {
        if (const auto* world = scene().tryGet<devex::scene::WorldTransform>(m_entity))
        {
            worldTransformUpdated = world->matrix[3] == devex::math::Vec4{1.0f, 2.0f, 3.0f, 1.0f};
        }
        if (++m_updates == 2)
        {
            requestQuit();
        }
    }

private:
    devex::scene::Entity m_entity;
    int m_updates = 0;
};

TEST_CASE("The runtime updates the scene transforms every frame", "[runtime][application]")
{
    SceneApplication application;

    CHECK(devex::runtime::run(application, testConfig) == EXIT_SUCCESS);

    CHECK(application.worldTransformUpdated);
}

TEST_CASE("run drives the lifecycle until the application quits", "[runtime][application]")
{
    CountingApplication application;

    CHECK(devex::runtime::run(application, testConfig) == EXIT_SUCCESS);

    CHECK(application.startups == 1);
    CHECK(application.windowMatchedConfig);
    CHECK(application.updates == 3);
    CHECK(application.shutdowns == 1);
}

TEST_CASE("A failed startup stops before the first frame", "[runtime][application]")
{
    FailingApplication application;

    CHECK(devex::runtime::run(application, testConfig) == EXIT_FAILURE);

    CHECK(application.updates == 0);
    CHECK(application.shutdowns == 0);
}

namespace {

// Plays the scenes of a package: the first one at startup, the second one after a frame.
class PackagedApplication final : public devex::runtime::Application
{
public:
    devex::asset::AssetId first;
    devex::asset::AssetId second;
    std::string gameName;
    bool windowFollowedSettings = false;
    std::string startupEntity;
    std::string loadedEntity;

    Result<void> onStartup() override
    {
        const devex::asset::AssetSource* const source = assetSource();
        if (source == nullptr || project() == nullptr || assetDatabase() != nullptr)
        {
            return devex::core::makeError(devex::core::ErrorCode::InvalidState, "the package is not the asset source");
        }
        gameName = project()->name;
        windowFollowedSettings = window().size() == devex::math::Extent2D{400, 300};
        if (Result<void> loaded = loadScene(first); !loaded)
        {
            return loaded;
        }
        startupEntity = scene().name(scene().firstRoot());
        return {};
    }

    void onUpdate(Duration /*frameDelta*/) override
    {
        if (++m_updates == 1)
        {
            if (loadScene(second))
            {
                loadedEntity = scene().name(scene().firstRoot());
            }
            CHECK_FALSE(loadScene(devex::asset::AssetId::generate()).has_value());
        }
        else
        {
            requestQuit();
        }
    }

private:
    int m_updates = 0;
};

[[nodiscard]] std::string sceneWithEntity(std::string_view name)
{
    return std::format("[scene format=1]\n\n[entity uuid=\"{}\" name=\"{}\"]\n", devex::core::Uuid::generate(), name);
}

} // namespace

TEST_CASE("Exported games play the scenes of their package", "[runtime][application]")
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / ("devex-packaged-" + devex::core::Uuid::generate().toString());
    PackagedApplication application;
    application.first = devex::asset::AssetId::generate();
    application.second = devex::asset::AssetId::generate();
    {
        devex::core::Result<devex::asset::PackageWriter> writer = devex::asset::PackageWriter::create(directory / "Game.dvxpak");
        REQUIRE(writer.has_value());
        devex::asset::Project settings{.name = "Packaged game"};
        settings.window.width = 400;
        settings.window.height = 300;
        writer->setProject(settings);
        REQUIRE(writer->add({.id = application.first, .type = devex::asset::AssetType::Scene, .name = "first",
                             .source = application.first},
                            "res://assets/first.dvxscene", devex::asset::encodeScene(sceneWithEntity("Start"))));
        REQUIRE(writer->add({.id = application.second, .type = devex::asset::AssetType::Scene, .name = "second",
                             .source = application.second},
                            "res://assets/second.dvxscene", devex::asset::encodeScene(sceneWithEntity("Next"))));
        REQUIRE(writer->finish());
    }

    ApplicationConfig config = testConfig;
    config.package = directory / "Game.dvxpak";
    config.useProjectWindowSettings = true;
    CHECK(devex::runtime::run(application, config) == EXIT_SUCCESS);
    CHECK(application.gameName == "Packaged game");
    CHECK(application.windowFollowedSettings);
    CHECK(application.startupEntity == "Start");
    CHECK(application.loadedEntity == "Next");

    config.package = directory / "Missing.dvxpak";
    PackagedApplication missing;
    CHECK(devex::runtime::run(missing, config) == EXIT_FAILURE);
    std::filesystem::remove_all(directory);
}

TEST_CASE("An application can run again after a previous run", "[runtime][application]")
{
    CountingApplication first;
    CountingApplication second;

    CHECK(devex::runtime::run(first, testConfig) == EXIT_SUCCESS);
    CHECK(devex::runtime::run(second, testConfig) == EXIT_SUCCESS);
    CHECK(second.updates == 3);
}
