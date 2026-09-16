#include <devex/core/Log.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Event.hpp>
#include <devex/platform/Input.hpp>
#include <devex/runtime/Application.hpp>

#include <chrono>
#include <format>
#include <variant>

namespace {

using devex::core::Duration;
using devex::math::Vec3;
using devex::platform::Event;
using devex::platform::Key;
using devex::platform::MouseButton;

// Milestone 1 playground: a virtual first-person camera driven by physical WASD keys and the
// mouse, reported in the window title.
class Sandbox final : public devex::runtime::Application
{
public:
    devex::core::Result<void> onStartup() override
    {
        DEVEX_LOG_INFO("Move with {}{}{}{}, click to capture the mouse, {} to release it or quit",
                       platform().keyLabel(Key::W), platform().keyLabel(Key::A),
                       platform().keyLabel(Key::S), platform().keyLabel(Key::D),
                       platform().keyLabel(Key::Escape));
        return {};
    }

    void onEvent(const Event& event) override
    {
        using namespace devex::platform;

        if (const auto* pressed = std::get_if<KeyPressed>(&event))
        {
            if (!pressed->repeat)
            {
                DEVEX_LOG_DEBUG("Key {} pressed (labelled {})", platform().keyName(pressed->key),
                                platform().keyLabel(pressed->key));
            }
        }
        else if (const auto* resized = std::get_if<WindowResized>(&event))
        {
            DEVEX_LOG_INFO("Window resized to {}x{} ({}x{} pixels)", resized->size.width,
                           resized->size.height, resized->pixelSize.width,
                           resized->pixelSize.height);
        }
        else if (const auto* focus = std::get_if<WindowFocusChanged>(&event))
        {
            DEVEX_LOG_DEBUG("Window {}", focus->focused ? "focused" : "unfocused");
        }
        else if (const auto* dropped = std::get_if<FileDropped>(&event))
        {
            DEVEX_LOG_INFO("File dropped: {}", dropped->path);
        }
    }

    void onFixedUpdate(Duration fixedDelta) override
    {
        const devex::platform::Input& keys = input();

        // Y-up, right-handed: forward is -Z.
        Vec3 direction{0.0f};
        if (keys.isKeyDown(Key::W))
        {
            direction.z -= 1.0f;
        }
        if (keys.isKeyDown(Key::S))
        {
            direction.z += 1.0f;
        }
        if (keys.isKeyDown(Key::A))
        {
            direction.x -= 1.0f;
        }
        if (keys.isKeyDown(Key::D))
        {
            direction.x += 1.0f;
        }

        if (direction != Vec3{0.0f})
        {
            const float distance = moveSpeed * static_cast<float>(fixedDelta.count());
            m_position += devex::math::normalize(direction) * distance;
        }
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

private:
    static constexpr float moveSpeed = 5.0f;         // meters per second
    static constexpr float mouseSensitivity = 0.1f;  // degrees per mouse unit
    static constexpr Duration statsPeriod = std::chrono::milliseconds(500);

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
            "Devex Sandbox | {:.0f} FPS | {:.0f} fixed/s | position ({:.1f}, {:.1f}) | "
            "yaw {:.0f}° pitch {:.0f}°",
            m_frames / seconds, m_fixedSteps / seconds, m_position.x, m_position.z, m_yaw,
            m_pitch));

        m_statsTime = Duration::zero();
        m_frames = 0;
        m_fixedSteps = 0;
    }

    Vec3 m_position{0.0f};
    float m_yaw = 0.0f;
    float m_pitch = 0.0f;
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
        .maxFrameRate = 240,
    });
}
