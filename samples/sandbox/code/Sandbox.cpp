// The sandbox gameplay: a flying camera, a turntable, and a switch between day and night.
#include <devex/core/Log.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Input.hpp>
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>

#include <cmath>

namespace {

using devex::math::Vec3;
using devex::platform::Key;
using devex::runtime::SystemContext;
using devex::runtime::SystemPhase;

constexpr Vec3 up{0.0f, 1.0f, 0.0f};
constexpr Vec3 right{1.0f, 0.0f, 0.0f};

// Flies its entity like a first-person camera: a click captures the mouse to look around, W A S D
// (Z Q S D on AZERTY) move, Space and Ctrl go up and down, Shift is faster, Escape releases the
// mouse, then ends the game.
struct FlyCamera
{
    // In meters per second.
    float speed = 4.0f;
    float fastSpeed = 12.0f;
    // In radians per unit of mouse movement.
    float sensitivity = devex::math::radians(0.1f);
    float yaw = 0.0f;
    float pitch = 0.0f;
};
DEVEX_DECLARE_REFLECTION(FlyCamera);
DEVEX_REFLECT(FlyCamera)
{
    type.field("speed", &FlyCamera::speed);
    type.field("fast_speed", &FlyCamera::fastSpeed);
    type.field("sensitivity", &FlyCamera::sensitivity, {.angle = true});
    type.field("yaw", &FlyCamera::yaw, {.angle = true});
    type.field("pitch", &FlyCamera::pitch, {.angle = true});
}

// Turns its entity, and its children with it, around the vertical axis.
struct Turntable
{
    // In radians per second.
    float speed = 0.6f;
};
DEVEX_DECLARE_REFLECTION(Turntable);
DEVEX_REFLECT(Turntable)
{
    type.field("speed", &Turntable::speed, {.angle = true});
}

// On an entity with an Environment: N switches the sky and every directional light between day and
// night, which automatic exposure then follows.
struct DayNight
{
    bool night = false;
    // Sunlight and moonlight, in lux.
    float dayIlluminance = 100000.0f;
    float nightIlluminance = 0.3f;
    float dayTemperature = 5800.0f;
    float nightTemperature = 9000.0f;
    // Luminance of the sky, in nits.
    float daySkyIntensity = 25000.0f;
    float nightSkyIntensity = 0.03f;
};
DEVEX_DECLARE_REFLECTION(DayNight);
DEVEX_REFLECT(DayNight)
{
    type.field("night", &DayNight::night);
    type.field("day_illuminance", &DayNight::dayIlluminance);
    type.field("night_illuminance", &DayNight::nightIlluminance);
    type.field("day_temperature", &DayNight::dayTemperature);
    type.field("night_temperature", &DayNight::nightTemperature);
    type.field("day_sky_intensity", &DayNight::daySkyIntensity);
    type.field("night_sky_intensity", &DayNight::nightSkyIntensity);
}

void flyCameras(SystemContext& context)
{
    const devex::platform::Input& input = context.input;
    devex::platform::Window& window = context.window;
    if (input.wasMouseButtonPressed(devex::platform::MouseButton::Left) && !window.isMouseCaptured())
    {
        window.setMouseCaptured(true);
    }
    if (input.wasKeyPressed(Key::Escape))
    {
        if (window.isMouseCaptured())
        {
            window.setMouseCaptured(false);
        }
        else
        {
            context.quitRequested = true;
        }
    }

    const auto seconds = static_cast<float>(context.delta.count());
    for ([[maybe_unused]] auto [entity, camera, transform] : context.scene.view<FlyCamera, devex::scene::Transform>())
    {
        if (window.isMouseCaptured())
        {
            camera.yaw -= input.mouseDelta().x * camera.sensitivity;
            camera.pitch = std::clamp(camera.pitch - input.mouseDelta().y * camera.sensitivity,
                                      devex::math::radians(-89.0f), devex::math::radians(89.0f));
        }

        // Movement is relative to the heading, on the horizontal plane.
        const devex::math::Quat heading = devex::math::angleAxis(camera.yaw, up);
        const Vec3 forward = heading * Vec3{0.0f, 0.0f, -1.0f};
        const Vec3 sideways = heading * right;
        Vec3 direction{0.0f};
        direction += input.isKeyDown(Key::W) ? forward : Vec3{0.0f};
        direction -= input.isKeyDown(Key::S) ? forward : Vec3{0.0f};
        direction += input.isKeyDown(Key::D) ? sideways : Vec3{0.0f};
        direction -= input.isKeyDown(Key::A) ? sideways : Vec3{0.0f};
        direction += input.isKeyDown(Key::Space) ? up : Vec3{0.0f};
        direction -= input.isKeyDown(Key::LeftControl) ? up : Vec3{0.0f};
        if (devex::math::length(direction) > 0.0f)
        {
            const float speed = input.isKeyDown(Key::LeftShift) ? camera.fastSpeed : camera.speed;
            transform.position += devex::math::normalize(direction) * speed * seconds;
        }
        transform.rotation = heading * devex::math::angleAxis(camera.pitch, right);
    }
}

void turnTurntables(SystemContext& context)
{
    const auto seconds = static_cast<float>(context.delta.count());
    for ([[maybe_unused]] auto [entity, turntable, transform] :
         context.scene.view<Turntable, devex::scene::Transform>())
    {
        transform.rotation = devex::math::normalize(devex::math::angleAxis(turntable.speed * seconds, up) * transform.rotation);
    }
}

void switchDayAndNight(SystemContext& context)
{
    if (!context.input.wasKeyPressed(Key::N))
    {
        return;
    }
    for ([[maybe_unused]] auto [entity, switcher, environment] :
         context.scene.view<DayNight, devex::scene::Environment>())
    {
        switcher.night = !switcher.night;
        environment.intensity = switcher.night ? switcher.nightSkyIntensity : switcher.daySkyIntensity;
        for ([[maybe_unused]] auto [lightEntity, light] : context.scene.view<devex::scene::DirectionalLight>())
        {
            light.illuminance = switcher.night ? switcher.nightIlluminance : switcher.dayIlluminance;
            light.temperature = switcher.night ? switcher.nightTemperature : switcher.dayTemperature;
        }
        DEVEX_LOG_INFO("{}", switcher.night ? "Night" : "Day");
    }
}

} // namespace

DEVEX_GAME_MODULE(game)
{
    game.component<FlyCamera>();
    game.component<Turntable>();
    game.component<DayNight>();
    game.system("Fly cameras", SystemPhase::Update, &flyCameras);
    game.system("Turn turntables", SystemPhase::Update, &turnTurntables);
    game.system("Switch day and night", SystemPhase::Update, &switchDayAndNight);
}
