// The sandbox gameplay: a flying camera, a turntable and a switch between day and night in the sandbox
// scene; a character that walks, jumps and launches balls in the physics arena.
#include <devex/asset/AssetId.hpp>
#include <devex/core/Log.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Input.hpp>
#include <devex/runtime/Game.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/ui/UiWorld.hpp>

#include <cmath>
#include <numbers>
#include <vector>

namespace {

using devex::math::Vec3;
using devex::platform::Key;
using devex::runtime::SystemContext;
using devex::runtime::SystemPhase;
using devex::scene::Entity;
using devex::scene::Scene;

constexpr Vec3 up{0.0f, 1.0f, 0.0f};
constexpr Vec3 right{1.0f, 0.0f, 0.0f};

// Flies its entity like a first-person camera while its camera is the primary one: a click
// captures the mouse to look around, W A S D (Z Q S D on AZERTY) move, Space and Ctrl go up and
// down, Shift is faster, Escape releases the mouse, then ends the game.
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

// A first-person character, on an entity with a CharacterController and a camera child: the mouse
// looks around once captured by a click, W A S D walk, Shift runs, Space jumps.
struct Player
{
    // In meters per second.
    float walkSpeed = 4.5f;
    float runSpeed = 8.0f;
    float jumpSpeed = 5.5f;
    float sensitivity = devex::math::radians(0.1f);
    float yaw = 0.0f;
    float pitch = 0.0f;
};
DEVEX_DECLARE_REFLECTION(Player);
DEVEX_REFLECT(Player)
{
    type.field("walk_speed", &Player::walkSpeed);
    type.field("run_speed", &Player::runSpeed);
    type.field("jump_speed", &Player::jumpSpeed);
    type.field("sensitivity", &Player::sensitivity, {.angle = true});
    type.field("yaw", &Player::yaw, {.angle = true});
    type.field("pitch", &Player::pitch, {.angle = true});
}

// Launches a ball where the player looks at each click while the mouse is captured.
struct BallLauncher
{
    // The prefab of the balls, a scene with a rigid body at its root.
    devex::asset::AssetId ball;
    // In meters per second.
    float speed = 16.0f;
    // Seconds before a ball disappears.
    float lifetime = 20.0f;
    // The oldest balls disappear beyond this count.
    std::uint32_t maxBalls = 40;
    // Played once where each ball leaves.
    devex::asset::AssetId throwSound;
};
DEVEX_DECLARE_REFLECTION(BallLauncher);
DEVEX_REFLECT(BallLauncher)
{
    type.field("ball", &BallLauncher::ball, {.assetType = "scene"});
    type.field("speed", &BallLauncher::speed);
    type.field("lifetime", &BallLauncher::lifetime);
    type.field("max_balls", &BallLauncher::maxBalls);
    type.field("throw_sound", &BallLauncher::throwSound, {.assetType = "audio"});
}

// A launched ball.
struct Ball
{
    float age = 0.0f;
    float lifetime = 20.0f;
};
DEVEX_DECLARE_REFLECTION(Ball);
DEVEX_REFLECT(Ball)
{
    type.field("age", &Ball::age);
    type.field("lifetime", &Ball::lifetime);
}

// On a trigger: lights the PointLight of its children while bodies are inside.
struct LampSwitch
{
    // In lumens, when lit.
    float intensity = 3000.0f;
    // Bodies inside the trigger; not saved.
    int inside = 0;
};
DEVEX_DECLARE_REFLECTION(LampSwitch);
DEVEX_REFLECT(LampSwitch)
{
    type.field("intensity", &LampSwitch::intensity);
}

// Moves its entity back and forth around where it started, as for a kinematic platform.
struct Oscillator
{
    // The farthest the entity goes from its starting position.
    Vec3 offset{0.0f, 1.0f, 0.0f};
    // In seconds.
    float period = 4.0f;
    // Runtime state, not saved.
    Vec3 origin{0.0f};
    float time = 0.0f;
    bool started = false;
};
DEVEX_DECLARE_REFLECTION(Oscillator);
DEVEX_REFLECT(Oscillator)
{
    type.field("offset", &Oscillator::offset);
    type.field("period", &Oscillator::period);
}

// Tab loads another scene of the game.
struct SceneSwitch
{
    devex::asset::AssetId scene;
};
DEVEX_DECLARE_REFLECTION(SceneSwitch);
DEVEX_REFLECT(SceneSwitch)
{
    type.field("scene", &SceneSwitch::scene, {.assetType = "scene"});
}

[[nodiscard]] bool isPrimaryCamera(const Scene& scene, Entity entity)
{
    const devex::scene::Camera* const camera = scene.tryGet<devex::scene::Camera>(entity);
    return camera != nullptr && camera->primary;
}

// The first child of an entity with a camera.
[[nodiscard]] Entity cameraChild(const Scene& scene, Entity entity)
{
    for (Entity child = scene.firstChild(entity); child.isValid(); child = scene.nextSibling(child))
    {
        if (scene.has<devex::scene::Camera>(child))
        {
            return child;
        }
    }
    return {};
}

// Whether the player is typing into a field of the interface: the letters are then text, not
// orders, and the systems that answer single keys leave them alone.
[[nodiscard]] bool typing(const SystemContext& context)
{
    return context.ui != nullptr && context.ui->isEditing();
}

// A click in the world captures the mouse; Escape releases it, then ends the game. A click on the
// interface belongs to it and leaves the cursor free, and a scene with an interface leaves Escape
// to its menus, which have their own way out.
void handleMouseCapture(SystemContext& context)
{
    devex::platform::Window& window = context.window;
    const bool onInterface = context.ui != nullptr && context.ui->pointerOverInterface();
    if (context.input.wasMouseButtonPressed(devex::platform::MouseButton::Left) &&
        !window.isMouseCaptured() && !onInterface)
    {
        window.setMouseCaptured(true);
    }
    if (context.input.wasKeyPressed(Key::Escape) && !typing(context))
    {
        if (window.isMouseCaptured())
        {
            window.setMouseCaptured(false);
        }
        else if (context.ui == nullptr || context.ui->canvases().empty())
        {
            context.quitRequested = true;
        }
    }
}

void flyCameras(SystemContext& context)
{
    const devex::platform::Input& input = context.input;
    const auto seconds = static_cast<float>(context.delta.count());
    for ([[maybe_unused]] auto [entity, camera, transform] : context.scene.view<FlyCamera, devex::scene::Transform>())
    {
        if (context.scene.has<devex::scene::Camera>(entity) && !isPrimaryCamera(context.scene, entity))
        {
            continue;
        }
        handleMouseCapture(context);
        if (context.window.isMouseCaptured())
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

void movePlayers(SystemContext& context)
{
    const devex::platform::Input& input = context.input;
    Scene& scene = context.scene;
    for ([[maybe_unused]] auto [entity, player, controller, transform] :
         scene.view<Player, devex::scene::CharacterController, devex::scene::Transform>())
    {
        const Entity eyes = cameraChild(scene, entity);
        if (!eyes.isValid() || !isPrimaryCamera(scene, eyes))
        {
            continue;
        }
        handleMouseCapture(context);
        if (context.window.isMouseCaptured())
        {
            player.yaw -= input.mouseDelta().x * player.sensitivity;
            player.pitch = std::clamp(player.pitch - input.mouseDelta().y * player.sensitivity,
                                      devex::math::radians(-85.0f), devex::math::radians(85.0f));
        }
        transform.rotation = devex::math::angleAxis(player.yaw, up);
        scene.get<devex::scene::Transform>(eyes).rotation = devex::math::angleAxis(player.pitch, right);

        const Vec3 forward = transform.rotation * Vec3{0.0f, 0.0f, -1.0f};
        const Vec3 sideways = transform.rotation * right;
        Vec3 direction{0.0f};
        direction += input.isKeyDown(Key::W) ? forward : Vec3{0.0f};
        direction -= input.isKeyDown(Key::S) ? forward : Vec3{0.0f};
        direction += input.isKeyDown(Key::D) ? sideways : Vec3{0.0f};
        direction -= input.isKeyDown(Key::A) ? sideways : Vec3{0.0f};
        const float speed = input.isKeyDown(Key::LeftShift) ? player.runSpeed : player.walkSpeed;
        const Vec3 walk = devex::math::length(direction) > 0.0f ? devex::math::normalize(direction) * speed : Vec3{0.0f};
        controller.velocity.x = walk.x;
        controller.velocity.z = walk.z;
        if (input.wasKeyPressed(Key::Space) && controller.grounded)
        {
            controller.velocity.y = player.jumpSpeed;
        }
    }
}

// Runs before movePlayers, so that the click capturing the mouse does not launch a ball.
void launchBalls(SystemContext& context)
{
    Scene& scene = context.scene;
    if (!context.window.isMouseCaptured() || !context.input.wasMouseButtonPressed(devex::platform::MouseButton::Left))
    {
        return;
    }
    struct Launch
    {
        Vec3 position;
        Vec3 velocity;
        BallLauncher launcher;
    };
    std::vector<Launch> launches;
    for ([[maybe_unused]] auto [entity, launcher, controller] : scene.view<BallLauncher, devex::scene::CharacterController>())
    {
        const Entity eyes = cameraChild(scene, entity);
        const devex::scene::WorldTransform* const view = eyes.isValid() ? scene.tryGet<devex::scene::WorldTransform>(eyes) : nullptr;
        if (view == nullptr || !isPrimaryCamera(scene, eyes))
        {
            continue;
        }
        const Vec3 forward = devex::math::normalize(Vec3(view->matrix * devex::math::Vec4(0.0f, 0.0f, -1.0f, 0.0f)));
        launches.push_back({Vec3(view->matrix[3]) + forward * 0.7f, forward * launcher.speed + controller.velocity, launcher});
    }

    for (const Launch& launch : launches)
    {
        const devex::core::Result<Entity> ball = devex::scene::instantiatePrefab(scene, launch.launcher.ball);
        if (!ball)
        {
            DEVEX_LOG_WARNING("Cannot launch a ball: {}", ball.error());
            continue;
        }
        if (devex::scene::Transform* const transform = scene.tryGet<devex::scene::Transform>(*ball))
        {
            transform->position = launch.position;
        }
        if (devex::scene::RigidBody* const body = scene.tryGet<devex::scene::RigidBody>(*ball))
        {
            body->linearVelocity = launch.velocity;
        }
        if (!scene.has<Ball>(*ball))
        {
            scene.add<Ball>(*ball);
        }
        scene.get<Ball>(*ball).lifetime = launch.launcher.lifetime;
        if (context.audio != nullptr && launch.launcher.throwSound.isValid())
        {
            context.audio->playOneShot(launch.launcher.throwSound, launch.position, 0.8f);
        }

        // Too many balls: the oldest one goes.
        std::size_t count = 0;
        Entity oldest;
        float oldestAge = -1.0f;
        for ([[maybe_unused]] auto [entity, existing] : scene.view<Ball>())
        {
            ++count;
            if (existing.age > oldestAge)
            {
                oldestAge = existing.age;
                oldest = entity;
            }
        }
        if (count > launch.launcher.maxBalls && oldest.isValid())
        {
            scene.destroyEntity(oldest);
        }
    }
}

void ageBalls(SystemContext& context)
{
    const auto seconds = static_cast<float>(context.delta.count());
    std::vector<Entity> expired;
    for ([[maybe_unused]] auto [entity, ball] : context.scene.view<Ball>())
    {
        ball.age += seconds;
        if (ball.age > ball.lifetime)
        {
            expired.push_back(entity);
        }
    }
    for (const Entity entity : expired)
    {
        context.scene.destroyEntity(entity);
    }
}

void switchLamps(SystemContext& context)
{
    if (context.physics == nullptr)
    {
        return;
    }
    Scene& scene = context.scene;
    for (const devex::physics::Contact& contact : context.physics->contacts())
    {
        if (!contact.trigger)
        {
            continue;
        }
        for (const Entity zone : {contact.first, contact.second})
        {
            LampSwitch* const lamp = scene.isAlive(zone) ? scene.tryGet<LampSwitch>(zone) : nullptr;
            if (lamp != nullptr)
            {
                lamp->inside = std::max(0, lamp->inside + (contact.phase == devex::physics::ContactPhase::Begin ? 1 : -1));
            }
        }
    }
    for ([[maybe_unused]] auto [zone, lamp] : scene.view<LampSwitch>())
    {
        for (Entity child = scene.firstChild(zone); child.isValid(); child = scene.nextSibling(child))
        {
            if (devex::scene::PointLight* const light = scene.tryGet<devex::scene::PointLight>(child))
            {
                light->intensity = lamp.inside > 0 ? lamp.intensity : 0.0f;
            }
        }
    }
}

void oscillate(SystemContext& context)
{
    const auto seconds = static_cast<float>(context.delta.count());
    for ([[maybe_unused]] auto [entity, oscillator, transform] : context.scene.view<Oscillator, devex::scene::Transform>())
    {
        if (!oscillator.started)
        {
            oscillator.origin = transform.position;
            oscillator.started = true;
        }
        oscillator.time += seconds;
        const float phase = 2.0f * std::numbers::pi_v<float> * oscillator.time / std::max(oscillator.period, 0.01f);
        transform.position = oscillator.origin + oscillator.offset * std::sin(phase);
    }
}

// C switches between the player's view and the flying cameras.
void switchScenes(SystemContext& context)
{
    if (!context.input.wasKeyPressed(Key::Tab) || typing(context))
    {
        return;
    }
    for ([[maybe_unused]] auto [entity, switcher] : context.scene.view<SceneSwitch>())
    {
        if (switcher.scene.isValid())
        {
            // In the background: this scene goes on until what the other shows is ready.
            context.sceneToLoadInBackground = switcher.scene;
            return;
        }
    }
}

void switchCameras(SystemContext& context)
{
    Scene& scene = context.scene;
    if (!context.input.wasKeyPressed(Key::C) || typing(context))
    {
        return;
    }
    std::vector<Entity> playerCameras;
    for ([[maybe_unused]] auto [entity, player] : scene.view<Player>())
    {
        if (const Entity eyes = cameraChild(scene, entity); eyes.isValid())
        {
            playerCameras.push_back(eyes);
        }
    }
    std::vector<Entity> flyingCameras;
    for ([[maybe_unused]] auto [entity, camera, flying] : scene.view<devex::scene::Camera, FlyCamera>())
    {
        flyingCameras.push_back(entity);
    }
    if (playerCameras.empty() || flyingCameras.empty())
    {
        return;
    }
    const bool toFlying = isPrimaryCamera(scene, playerCameras.front());
    for (const Entity entity : playerCameras)
    {
        scene.get<devex::scene::Camera>(entity).primary = !toFlying;
    }
    for (const Entity entity : flyingCameras)
    {
        scene.get<devex::scene::Camera>(entity).primary = toFlying;
    }
    DEVEX_LOG_INFO("{} camera", toFlying ? "Flying" : "Player");
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
    if (!context.input.wasKeyPressed(Key::N) || typing(context))
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
    game.component<Player>();
    game.component<BallLauncher>();
    game.component<Ball>();
    game.component<LampSwitch>();
    game.component<Oscillator>();
    game.component<SceneSwitch>();
    game.system("Move platforms", SystemPhase::FixedUpdate, &oscillate);
    game.system("Switch cameras", SystemPhase::Update, &switchCameras, -2);
    game.system("Launch balls", SystemPhase::Update, &launchBalls, -1);
    game.system("Fly cameras", SystemPhase::Update, &flyCameras);
    game.system("Move players", SystemPhase::Update, &movePlayers);
    game.system("Age balls", SystemPhase::Update, &ageBalls);
    game.system("Switch lamps", SystemPhase::Update, &switchLamps);
    game.system("Turn turntables", SystemPhase::Update, &turnTurntables);
    game.system("Switch day and night", SystemPhase::Update, &switchDayAndNight);
    game.system("Switch scenes", SystemPhase::Update, &switchScenes);
}
