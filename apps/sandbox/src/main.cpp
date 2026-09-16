#include <devex/asset/AssetId.hpp>
#include <devex/asset/import/GltfImporter.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/runtime/Application.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <numbers>
#include <variant>
#include <vector>

namespace {

using devex::asset::AssetId;
using devex::core::Duration;
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

// Milestone 4 playground: a scene with a hierarchy, a free camera, saving with F5 and reloading
// with F9. Dropping a .gltf or .glb file on the window imports it as entities.
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
            importModel(devex::core::pathFromUtf8(dropped->path));
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
        world.add<MeshRenderer>(ground, devex::asset::builtin::planeMesh);

        // Children orbit with the turntable because their transforms are relative to it.
        const Entity turntable = world.createEntity("Turntable");
        world.add<Transform>(turntable, Transform{.position = {0.0f, 0.75f, 0.0f}});
        m_turntableUuid = world.uuid(turntable);

        const Entity centerCube = world.createEntity("Center cube");
        world.add<Transform>(centerCube);
        world.add<MeshRenderer>(centerCube, devex::asset::builtin::cubeMesh);
        attach(centerCube, turntable);

        for (int index = 0; index < 4; ++index)
        {
            const float angle = static_cast<float>(index) * std::numbers::pi_v<float> * 0.5f;
            const Entity satellite = world.createEntity(std::format("Satellite {}", index + 1));
            world.add<Transform>(satellite, Transform{
                                                .position = {2.0f * std::cos(angle), 0.0f,
                                                             2.0f * std::sin(angle)},
                                                .scale = Vec3{0.6f},
                                            });
            world.add<MeshRenderer>(satellite, index % 2 == 0 ? devex::asset::builtin::sphereMesh
                                                              : devex::asset::builtin::cubeMesh);
            attach(satellite, turntable);
        }

        const Entity row = world.createEntity("Sphere row");
        world.add<Transform>(row, Transform{.position = {0.0f, 0.5f, -5.0f}});
        for (int index = 0; index < 5; ++index)
        {
            const Entity sphere = world.createEntity(std::format("Sphere {}", index + 1));
            world.add<Transform>(sphere,
                                 Transform{.position = {-4.0f + 2.0f * static_cast<float>(index), 0.0f, 0.0f}});
            world.add<MeshRenderer>(sphere, devex::asset::builtin::sphereMesh);
            attach(sphere, row);
        }
    }

    void attach(Entity child, Entity parent)
    {
        if (devex::core::Result<void> attached = scene().setParent(child, parent); !attached)
        {
            DEVEX_LOG_ERROR("Cannot build the hierarchy: {}", attached.error());
        }
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

        // Entities keep their UUIDs across saves, so the camera is found again.
        if (const Transform* const camera = scene().tryGet<Transform>(scene().findEntity(m_cameraUuid)))
        {
            m_position = camera->position;
            m_previousPosition = camera->position;
        }
        DEVEX_LOG_INFO("Loaded {} entities", scene().entityCount());
    }

    void importModel(const std::filesystem::path& path)
    {
        devex::core::Result<devex::asset::ImportedScene> imported = devex::asset::importGltf(path);
        if (!imported)
        {
            DEVEX_LOG_ERROR("Cannot import the model: {}", imported.error());
            return;
        }

        // Imported meshes get identifiers for this session only, until the asset database exists.
        std::vector<AssetId> meshIds;
        for (const devex::asset::ImportedMesh& mesh : imported->meshes)
        {
            devex::core::Result<devex::render::MeshHandle> handle = renderer().createMesh(mesh.data);
            if (!handle)
            {
                DEVEX_LOG_ERROR("Cannot upload mesh '{}': {}", mesh.name, handle.error());
                return;
            }
            meshIds.push_back(AssetId::generate());
            assets().registerMesh(meshIds.back(), *handle);
        }

        Scene& world = scene();
        const Entity model = world.createEntity(devex::core::toUtf8(path.stem()));
        world.add<Transform>(model, Transform{.position = {4.0f, 0.0f, 2.0f}});
        for (const devex::asset::ImportedInstance& instance : imported->instances)
        {
            const devex::math::Trs trs = devex::math::decomposeTrs(instance.transform);
            const Entity part = world.createEntity(imported->meshes[instance.mesh].name);
            world.add<Transform>(part, Transform{trs.translation, trs.rotation, trs.scale});
            world.add<MeshRenderer>(part, meshIds[instance.mesh]);
            attach(part, model);
        }
        DEVEX_LOG_INFO("Imported {}: {} meshes, {} instances", devex::core::toUtf8(path.filename()),
                       imported->meshes.size(), imported->instances.size());
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
        window().setTitle(std::format(
            "Devex Sandbox | {} ({}) | {:.0f} FPS | {:.0f} fixed/s | {} entities | position "
            "({:.1f}, {:.1f}, {:.1f})",
            renderer().gpu().name, devex::render::toString(renderer().presentMode()),
            m_frames / seconds, m_fixedSteps / seconds, scene().entityCount(), m_position.x,
            m_position.y, m_position.z));

        m_statsTime = Duration::zero();
        m_frames = 0;
        m_fixedSteps = 0;
    }

    devex::core::Uuid m_cameraUuid;
    devex::core::Uuid m_turntableUuid;

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
    });
}
