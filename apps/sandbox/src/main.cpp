#include <devex/asset/Primitives.hpp>
#include <devex/asset/import/GltfImporter.hpp>
#include <devex/core/Log.hpp>
#include <devex/core/Path.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/render/RenderWorld.hpp>
#include <devex/runtime/Application.hpp>

#include <chrono>
#include <format>
#include <variant>
#include <vector>

namespace {

using devex::core::Duration;
using devex::math::Mat4;
using devex::math::Quat;
using devex::math::Vec3;
using devex::platform::Event;
using devex::platform::Key;
using devex::platform::MouseButton;
using devex::render::MeshHandle;

const Vec3 up{0.0f, 1.0f, 0.0f};
const Vec3 right{1.0f, 0.0f, 0.0f};

// Milestone 3 playground: a first-person camera flying over procedural meshes. Dropping a .gltf or
// .glb file on the window imports it next to them.
class Sandbox final : public devex::runtime::Application
{
public:
    devex::core::Result<void> onStartup() override
    {
        devex::render::Renderer& gpu = renderer();

        auto ground = gpu.createMesh(devex::asset::makePlane(40.0f));
        auto cube = gpu.createMesh(devex::asset::makeCube());
        auto sphere = gpu.createMesh(devex::asset::makeUvSphere(0.5f, 48, 24));
        for (auto* created : {&ground, &cube, &sphere})
        {
            if (!*created)
            {
                return std::unexpected(created->error());
            }
        }
        m_ground = *ground;
        m_cube = *cube;
        m_sphere = *sphere;

        DEVEX_LOG_INFO("Fly with {}{}{}{}, {} and {} to go up and down, click to capture the mouse",
                       platform().keyLabel(Key::W), platform().keyLabel(Key::A),
                       platform().keyLabel(Key::S), platform().keyLabel(Key::D),
                       platform().keyLabel(Key::Space), platform().keyLabel(Key::LeftControl));
        DEVEX_LOG_INFO("Drop a .gltf or .glb file on the window to import it");
        return {};
    }

    void onShutdown() override
    {
        releaseImportedMeshes();
    }

    void onEvent(const Event& event) override
    {
        if (const auto* dropped = std::get_if<devex::platform::FileDropped>(&event))
        {
            importModel(devex::core::pathFromUtf8(dropped->path));
        }
        else if (const auto* resized = std::get_if<devex::platform::WindowResized>(&event))
        {
            DEVEX_LOG_DEBUG("Window resized to {}x{} pixels", resized->pixelSize.width,
                            resized->pixelSize.height);
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

        m_previousCubeAngle = m_cubeAngle;
        m_cubeAngle += 0.8f * step;
        ++m_fixedSteps;
    }

    void onUpdate(Duration frameDelta) override
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

        updateTitle(frameDelta);
    }

    void onRender(devex::render::RenderWorld& world) override
    {
        // Simulation states are interpolated between the last two fixed steps.
        const auto alpha = static_cast<float>(interpolationAlpha());
        const Vec3 position = devex::math::mix(m_previousPosition, m_position, alpha);
        const float cubeAngle = devex::math::mix(m_previousCubeAngle, m_cubeAngle, alpha);

        const Quat orientation = devex::math::angleAxis(devex::math::radians(m_yaw), up) *
                                 devex::math::angleAxis(devex::math::radians(m_pitch), right);
        const Mat4 cameraTransform =
            devex::math::translate(Mat4{1.0f}, position) * devex::math::mat4_cast(orientation);

        world.clearColor = {0.46f, 0.62f, 0.85f, 1.0f};
        world.camera.view = devex::math::inverse(cameraTransform);

        world.meshes.push_back({m_ground, Mat4{1.0f}});
        world.meshes.push_back(
            {m_cube, devex::math::rotate(devex::math::translate(Mat4{1.0f}, {0.0f, 0.75f, 0.0f}),
                                         cubeAngle, devex::math::normalize(Vec3{1.0f, 1.0f, 0.0f}))});
        for (int index = 0; index < 5; ++index)
        {
            const Vec3 spherePosition{-4.0f + 2.0f * static_cast<float>(index), 0.5f, -4.0f};
            world.meshes.push_back({m_sphere, devex::math::translate(Mat4{1.0f}, spherePosition)});
        }

        const Mat4 modelPlacement = devex::math::translate(Mat4{1.0f}, {3.0f, 0.0f, 0.0f});
        for (const ImportedInstance& instance : m_importedInstances)
        {
            world.meshes.push_back({instance.mesh, modelPlacement * instance.transform});
        }
    }

private:
    struct ImportedInstance
    {
        MeshHandle mesh;
        Mat4 transform{1.0f};
    };

    static constexpr float mouseSensitivity = 0.1f; // degrees per mouse unit
    static constexpr Duration statsPeriod = std::chrono::milliseconds(500);

    void importModel(const std::filesystem::path& path)
    {
        devex::core::Result<devex::asset::ImportedScene> scene = devex::asset::importGltf(path);
        if (!scene)
        {
            DEVEX_LOG_ERROR("Cannot import the model: {}", scene.error());
            return;
        }

        releaseImportedMeshes();
        std::vector<MeshHandle> meshes;
        for (const devex::asset::ImportedMesh& mesh : scene->meshes)
        {
            devex::core::Result<MeshHandle> handle = renderer().createMesh(mesh.data);
            if (!handle)
            {
                DEVEX_LOG_ERROR("Cannot upload mesh '{}': {}", mesh.name, handle.error());
                releaseImportedMeshes();
                return;
            }
            meshes.push_back(*handle);
            m_importedMeshes.push_back(*handle);
        }
        for (const devex::asset::ImportedInstance& instance : scene->instances)
        {
            m_importedInstances.push_back({meshes[instance.mesh], instance.transform});
        }
        DEVEX_LOG_INFO("Imported {}: {} meshes, {} instances", devex::core::toUtf8(path.filename()),
                       scene->meshes.size(), scene->instances.size());
    }

    void releaseImportedMeshes()
    {
        for (const MeshHandle mesh : m_importedMeshes)
        {
            renderer().destroyMesh(mesh);
        }
        m_importedMeshes.clear();
        m_importedInstances.clear();
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
            "Devex Sandbox | {} ({}) | {:.0f} FPS | {:.0f} fixed/s | position ({:.1f}, {:.1f}, "
            "{:.1f})",
            renderer().gpu().name, devex::render::toString(renderer().presentMode()),
            m_frames / seconds, m_fixedSteps / seconds, m_position.x, m_position.y, m_position.z));

        m_statsTime = Duration::zero();
        m_frames = 0;
        m_fixedSteps = 0;
    }

    MeshHandle m_ground;
    MeshHandle m_cube;
    MeshHandle m_sphere;
    std::vector<MeshHandle> m_importedMeshes;
    std::vector<ImportedInstance> m_importedInstances;

    Vec3 m_position{0.0f, 1.6f, 6.0f};
    Vec3 m_previousPosition{0.0f, 1.6f, 6.0f};
    float m_yaw = 0.0f;
    float m_pitch = -10.0f;
    float m_cubeAngle = 0.0f;
    float m_previousCubeAngle = 0.0f;

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
