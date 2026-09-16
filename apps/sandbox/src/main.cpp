#include <devex/asset/AssetId.hpp>
#include <devex/asset/import/AssetDatabase.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/ModelInstantiation.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <optional>
#include <variant>
#include <vector>

namespace {

using devex::asset::AssetId;
using devex::core::Duration;
using devex::core::Uuid;
using devex::math::Quat;
using devex::math::Vec3;
using devex::platform::Key;
using devex::platform::MouseButton;
using devex::scene::Camera;
using devex::scene::DirectionalLight;
using devex::scene::Entity;
using devex::scene::MeshRenderer;
using devex::scene::Scene;
using devex::scene::Transform;

const Vec3 up{0.0f, 1.0f, 0.0f};
const Vec3 right{1.0f, 0.0f, 0.0f};

// Identifiers from the .dvxmeta files of apps/sandbox/project/assets.
[[nodiscard]] AssetId projectAsset(std::string_view uuid)
{
    return AssetId{*Uuid::parse(uuid)};
}

const AssetId groundMaterial = projectAsset("78014e97-7de2-4e27-89db-74abd840010d");
const AssetId glowMaterial = projectAsset("ff4872a7-7bc8-4ffb-900d-0c565be864f0");
const AssetId crateModel = projectAsset("fa17e48e-77a9-48d6-9cb6-3075791e73bf");
const AssetId beaconModel = projectAsset("65be0348-3b58-4e34-9d7b-30ba9d12512c");

// A model to place once its import is available.
struct Placement
{
    AssetId model;
    Vec3 position{0.0f};
    // Nil for a root.
    Uuid parent;
};

// Milestone 6 playground: a scene built from project assets imported in the background. Models
// appear once imported, and editing a texture, a material or a model in the assets folder updates
// the scene while it runs. F5 saves the scene, F9 reloads it, and dropping a .gltf or .glb file
// copies it into the project and places it in front of the camera.
class Sandbox final : public devex::runtime::Application
{
public:
    devex::core::Result<void> onStartup() override
    {
        buildScene();
        DEVEX_LOG_INFO("Fly with {}{}{}{}, {} and {} to go up and down, click to capture the mouse",
                       platform().keyLabel(Key::W), platform().keyLabel(Key::A),
                       platform().keyLabel(Key::S), platform().keyLabel(Key::D),
                       platform().keyLabel(Key::Space), platform().keyLabel(Key::LeftControl));
        DEVEX_LOG_INFO("F5 saves the scene to {}, F9 reloads it; drop a glTF file to import it",
                       devex::core::toUtf8(scenePath()));
        return {};
    }

    void onEvent(const devex::platform::Event& event) override
    {
        if (const auto* dropped = std::get_if<devex::platform::FileDropped>(&event))
        {
            importDroppedFile(devex::core::pathFromUtf8(dropped->path));
        }
    }

    void onFixedUpdate(Duration fixedDelta) override
    {
        const devex::platform::Input& keys = input();
        const float step = static_cast<float>(fixedDelta.count());

        // Movement is relative to the camera heading, on the horizontal plane.
        const Quat heading = devex::math::angleAxis(devex::math::radians(m_yaw), up);
        const Vec3 forward = heading * Vec3{0.0f, 0.0f, -1.0f};
        const Vec3 sideways = heading * right;

        Vec3 direction{0.0f};
        direction += keys.isKeyDown(Key::W) ? forward : Vec3{0.0f};
        direction -= keys.isKeyDown(Key::S) ? forward : Vec3{0.0f};
        direction += keys.isKeyDown(Key::D) ? sideways : Vec3{0.0f};
        direction -= keys.isKeyDown(Key::A) ? sideways : Vec3{0.0f};
        direction += keys.isKeyDown(Key::Space) ? up : Vec3{0.0f};
        direction -= keys.isKeyDown(Key::LeftControl) ? up : Vec3{0.0f};

        m_previousPosition = m_position;
        if (devex::math::length(direction) > 0.0f)
        {
            const float speed = keys.isKeyDown(Key::LeftShift) ? 12.0f : 4.0f;
            m_position += devex::math::normalize(direction) * speed * step;
        }

        m_previousTurntableAngle = m_turntableAngle;
        m_turntableAngle += 0.6f * step;
        ++m_fixedSteps;
    }

    void onUpdate(Duration frameDelta) override
    {
        handleMouseAndKeys();
        placeImportedModels();

        // Rendered states are interpolated between the last two fixed steps.
        const auto alpha = static_cast<float>(interpolationAlpha());
        Scene& world = scene();
        if (Transform* const camera = world.tryGet<Transform>(world.findEntity(m_cameraUuid)))
        {
            camera->position = devex::math::mix(m_previousPosition, m_position, alpha);
            camera->rotation = devex::math::angleAxis(devex::math::radians(m_yaw), up) *
                               devex::math::angleAxis(devex::math::radians(m_pitch), right);
        }
        if (Transform* const turntable =
                world.tryGet<Transform>(world.findEntity(m_turntableUuid)))
        {
            const float angle = devex::math::mix(m_previousTurntableAngle, m_turntableAngle, alpha);
            turntable->rotation = devex::math::angleAxis(angle, up);
        }

        updateTitle(frameDelta);
    }

    void onRender(devex::render::RenderWorld& world) override
    {
        // The scene has no environment settings yet: the sky color is set here.
        world.clearColor = {0.46f, 0.62f, 0.85f, 1.0f};
    }

private:
    static constexpr float mouseSensitivity = 0.1f; // degrees per mouse unit
    static constexpr Duration statsPeriod = std::chrono::milliseconds(500);

    [[nodiscard]] std::filesystem::path scenePath() const
    {
        return platform().baseDirectory() / "sandbox.dvxscene";
    }

    void buildScene()
    {
        Scene& world = scene();

        const Entity sun = world.createEntity("Sun");
        world.add<Transform>(sun, Transform{
                                      .rotation = devex::math::angleAxis(devex::math::radians(35.0f), up) *
                                                  devex::math::angleAxis(devex::math::radians(-60.0f), right),
                                  });
        world.add<DirectionalLight>(sun);

        const Entity camera = world.createEntity("Camera");
        world.add<Transform>(camera, Transform{.position = m_position});
        world.add<Camera>(camera);
        m_cameraUuid = world.uuid(camera);

        const Entity ground = world.createEntity("Ground");
        world.add<Transform>(ground, Transform{.scale = {40.0f, 1.0f, 40.0f}});
        world.add<MeshRenderer>(ground, MeshRenderer{devex::asset::builtin::planeMesh, groundMaterial});

        // Children orbit with the turntable because their transforms are relative to it.
        const Entity turntable = world.createEntity("Turntable");
        world.add<Transform>(turntable);
        m_turntableUuid = world.uuid(turntable);
        m_placements.push_back({crateModel, Vec3{0.0f}, m_turntableUuid});

        for (int index = 0; index < 4; ++index)
        {
            const float angle = static_cast<float>(index) * std::numbers::pi_v<float> * 0.5f;
            const Entity satellite = world.createEntity(std::format("Satellite {}", index + 1));
            world.add<Transform>(satellite, Transform{
                                                .position = {2.0f * std::cos(angle), 0.3f,
                                                             2.0f * std::sin(angle)},
                                                .scale = Vec3{0.6f},
                                            });
            const bool sphere = index % 2 == 0;
            world.add<MeshRenderer>(
                satellite, MeshRenderer{sphere ? devex::asset::builtin::sphereMesh
                                               : devex::asset::builtin::cubeMesh,
                                        sphere ? glowMaterial : AssetId{}});
            attach(satellite, turntable);
        }

        const Entity row = world.createEntity("Sphere row");
        world.add<Transform>(row, Transform{.position = {0.0f, 0.5f, -5.0f}});
        for (int index = 0; index < 5; ++index)
        {
            const Entity sphere = world.createEntity(std::format("Sphere {}", index + 1));
            world.add<Transform>(sphere,
                                 Transform{.position = {-4.0f + 2.0f * static_cast<float>(index), 0.0f, 0.0f}});
            world.add<MeshRenderer>(sphere, MeshRenderer{devex::asset::builtin::sphereMesh});
            attach(sphere, row);
        }

        m_placements.push_back({beaconModel, Vec3{-6.0f, 0.0f, -2.0f}, Uuid{}});
        m_placements.push_back({beaconModel, Vec3{6.0f, 0.0f, -2.0f}, Uuid{}});
    }

    void attach(Entity child, Entity parent)
    {
        if (devex::core::Result<void> attached = scene().setParent(child, parent); !attached)
        {
            DEVEX_LOG_ERROR("Cannot build the hierarchy: {}", attached.error());
        }
    }

    // Places the models whose import has finished; the others wait for a later frame.
    void placeImportedModels()
    {
        devex::asset::AssetDatabase* const database = assetDatabase();
        std::erase_if(m_placements, [&](const Placement& placement) {
            if (database == nullptr)
            {
                return true;
            }
            if (const std::optional<devex::asset::SourceFile> source = database->sourceOf(placement.model);
                source && source->status == devex::asset::ImportStatus::Failed)
            {
                DEVEX_LOG_WARNING("{} cannot be placed: {}", source->path, source->error);
                return true;
            }
            const devex::asset::ModelData* const model = assets().model(placement.model);
            const devex::asset::AssetInfo* const info = database->find(placement.model);
            if (model == nullptr || info == nullptr)
            {
                return false;
            }

            Scene& world = scene();
            const Entity parent = world.findEntity(placement.parent);
            const Entity root = devex::scene::instantiateModel(world, *model, info->name, parent);
            world.get<Transform>(root).position = placement.position;
            return true;
        });
    }

    void importDroppedFile(const std::filesystem::path& file)
    {
        devex::asset::AssetDatabase* const database = assetDatabase();
        if (database == nullptr)
        {
            DEVEX_LOG_WARNING("Dropped files need a project");
            return;
        }
        const devex::core::Result<AssetId> added = database->addFile(file, "res://assets/dropped");
        if (!added)
        {
            DEVEX_LOG_ERROR("Cannot add {}: {}", devex::core::toUtf8(file.filename()), added.error());
            return;
        }
        // In front of the camera, on the ground.
        const Quat heading = devex::math::angleAxis(devex::math::radians(m_yaw), up);
        Vec3 position = m_position + heading * Vec3{0.0f, 0.0f, -4.0f};
        position.y = 0.0f;
        m_placements.push_back({*added, position, Uuid{}});
        DEVEX_LOG_INFO("Copied {} into the project; it appears once imported",
                       devex::core::toUtf8(file.filename()));
    }

    void handleMouseAndKeys()
    {
        const devex::platform::Input& keys = input();
        devex::platform::Window& mainWindow = window();

        if (keys.wasMouseButtonPressed(MouseButton::Left) && !mainWindow.isMouseCaptured())
        {
            mainWindow.setMouseCaptured(true);
        }
        if (keys.wasKeyPressed(Key::Escape))
        {
            if (mainWindow.isMouseCaptured())
            {
                mainWindow.setMouseCaptured(false);
            }
            else
            {
                requestQuit();
            }
        }
        if (mainWindow.isMouseCaptured())
        {
            m_yaw -= keys.mouseDelta().x * mouseSensitivity;
            m_pitch = devex::math::clamp(m_pitch - keys.mouseDelta().y * mouseSensitivity,
                                         -89.0f, 89.0f);
        }

        if (keys.wasKeyPressed(Key::F5))
        {
            saveScene();
        }
        if (keys.wasKeyPressed(Key::F9))
        {
            reloadScene();
        }
    }

    void saveScene()
    {
        if (devex::core::Result<void> saved = devex::scene::saveSceneFile(scene(), scenePath());
            !saved)
        {
            DEVEX_LOG_ERROR("Cannot save the scene: {}", saved.error());
            return;
        }
        DEVEX_LOG_INFO("Saved {} entities", scene().entityCount());
    }

    void reloadScene()
    {
        devex::core::Result<Scene> loaded = devex::scene::loadSceneFile(scenePath());
        if (!loaded)
        {
            DEVEX_LOG_ERROR("Cannot load the scene: {}", loaded.error());
            return;
        }
        scene() = std::move(*loaded);
        // Placed models are part of the saved scene.
        m_placements.clear();

        // Entities keep their UUIDs across saves, so the camera is found again.
        if (const Transform* const camera = scene().tryGet<Transform>(scene().findEntity(m_cameraUuid)))
        {
            m_position = camera->position;
            m_previousPosition = camera->position;
        }
        DEVEX_LOG_INFO("Loaded {} entities", scene().entityCount());
    }

    void updateTitle(Duration frameDelta)
    {
        m_statsTime += frameDelta;
        ++m_frames;
        if (m_statsTime < statsPeriod)
        {
            return;
        }

        const double seconds = m_statsTime.count();
        const std::size_t importing =
            assetDatabase() != nullptr ? assetDatabase()->pendingImports() : 0;
        window().setTitle(std::format(
            "Devex Sandbox | {} ({}) | {:.0f} FPS | {:.0f} fixed/s | {} entities{} | position "
            "({:.1f}, {:.1f}, {:.1f})",
            renderer().gpu().name, devex::render::toString(renderer().presentMode()),
            m_frames / seconds, m_fixedSteps / seconds, scene().entityCount(),
            importing > 0 ? std::format(" | importing {}", importing) : std::string(),
            m_position.x, m_position.y, m_position.z));

        m_statsTime = Duration::zero();
        m_frames = 0;
        m_fixedSteps = 0;
    }

    Uuid m_cameraUuid;
    Uuid m_turntableUuid;
    std::vector<Placement> m_placements;

    Vec3 m_position{0.0f, 2.0f, 8.0f};
    Vec3 m_previousPosition{0.0f, 2.0f, 8.0f};
    float m_yaw = 0.0f;
    float m_pitch = -12.0f;
    float m_turntableAngle = 0.0f;
    float m_previousTurntableAngle = 0.0f;

    Duration m_statsTime = Duration::zero();
    int m_frames = 0;
    int m_fixedSteps = 0;
};

} // namespace

int main()
{
    return devex::runtime::run<Sandbox>({
        .title = "Devex Sandbox",
        .width = 1280,
        .height = 720,
        .project = devex::core::pathFromUtf8(DEVEX_SANDBOX_PROJECT),
    });
}
