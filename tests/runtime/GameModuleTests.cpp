#include <devex/core/Uuid.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/runtime/Game.hpp>
#include <devex/runtime/GameModule.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <format>
#include <vector>

using devex::runtime::GameModule;
using devex::runtime::GameRegistry;
using devex::runtime::SystemContext;
using devex::runtime::SystemPhase;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

const std::filesystem::path testModule{DEVEX_TEST_GAME_MODULE};

// A directory removed at the end of the test.
class TemporaryDirectory
{
public:
    TemporaryDirectory()
        : path(std::filesystem::temp_directory_path() / ("devex-modules-" + devex::core::Uuid::generate().toString()))
    {
    }

    ~TemporaryDirectory()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    std::filesystem::path path;
};

[[nodiscard]] std::int32_t counterValue(const Scene& scene, Entity entity)
{
    const devex::scene::ComponentType* const type = devex::scene::componentRegistry().find("Counter");
    REQUIRE(type != nullptr);
    const devex::reflection::FieldInfo* const field = type->type->findField("value");
    return *static_cast<const std::int32_t*>(field->address(type->find(scene, entity)));
}

std::vector<std::string> order;

void first(SystemContext&)
{
    order.emplace_back("first");
}

void second(SystemContext&)
{
    order.emplace_back("second");
}

void early(SystemContext&)
{
    order.emplace_back("early");
}

} // namespace

TEST_CASE("Systems run by phase, order and registration", "[runtime][game]")
{
    GameRegistry registry;
    registry.system("First", SystemPhase::Update, &first);
    registry.system("Second", SystemPhase::Update, &second);
    registry.system("Early", SystemPhase::Update, &early, -1);
    registry.system("Fixed", SystemPhase::FixedUpdate, &first);
    registry.system("Ignored", SystemPhase::Update, nullptr);
    REQUIRE(registry.systems().size() == 4);
    CHECK(registry.systems()[0].name == "Fixed");

    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.hidden = true});
    REQUIRE(window.has_value());
    devex::runtime::AssetManager assets(nullptr, nullptr);
    Scene scene;
    SystemContext context{.scene = scene, .input = platform->input(), .window = *window, .assets = assets};
    order.clear();
    registry.run(SystemPhase::Update, context);
    CHECK(order == std::vector<std::string>{"early", "first", "second"});
}

TEST_CASE("Game modules register, run, and reload without losing their components", "[runtime][game]")
{
    const TemporaryDirectory copies;
    auto platform = devex::platform::Platform::create();
    REQUIRE(platform.has_value());
    auto window = platform->createWindow({.hidden = true});
    REQUIRE(window.has_value());
    devex::runtime::AssetManager assets(nullptr, nullptr);

    Scene scene;
    const Entity entity = scene.createEntity("Counted");
    {
        auto module = GameModule::load(testModule, copies.path);
        if (!module)
        {
            FAIL(std::format("{}", module.error()));
        }
        REQUIRE((*module)->registry().components().size() == 1);
        CHECK((*module)->registry().systems().size() == 3);
        const devex::scene::ComponentType* const counter = devex::scene::componentRegistry().find("Counter");
        REQUIRE(counter != nullptr);
        counter->emplace(scene, entity);

        SystemContext context{.scene = scene, .input = platform->input(), .window = *window, .assets = assets};
        (*module)->registry().run(SystemPhase::Start, context);
        for (int step = 0; step < 5; ++step)
        {
            (*module)->registry().run(SystemPhase::FixedUpdate, context);
        }
        CHECK(counterValue(scene, entity) == 5);
        CHECK_FALSE(context.quitRequested);
        CHECK(scene.has<devex::scene::Transform>(entity));

        // Copies keep the module's pools, which also need releasing.
        Scene copy = scene.clone();

        // Releasing keeps the module's registered components as text, drops the component only its
        // code knows, and has the engine recreate the Transform pool the module created.
        CHECK((*module)->release(scene) == 1);
        CHECK((*module)->release(copy) == 1);
        CHECK(scene.has<devex::scene::PreservedComponents>(entity));
        CHECK(scene.has<devex::scene::Transform>(entity));
        CHECK_FALSE((*module)->release(scene));
        module->reset();
        CHECK(devex::scene::componentRegistry().find("Counter") == nullptr);

        // The preserved counter is written with the scene.
        CHECK(devex::scene::saveScene(copy).find("[component type=\"Counter\"]\nvalue = 5\nstep = 1") != std::string::npos);
    }

    // A new build of the module brings the components back where they were.
    auto reloaded = GameModule::load(testModule, copies.path);
    if (!reloaded)
    {
        FAIL(std::format("{}", reloaded.error()));
    }
    CHECK(devex::scene::restorePreservedComponents(scene) == 1);
    CHECK(counterValue(scene, entity) == 5);
    SystemContext context{.scene = scene, .input = platform->input(), .window = *window, .assets = assets};
    for (int step = 0; step < 95; ++step)
    {
        (*reloaded)->registry().run(SystemPhase::FixedUpdate, context);
    }
    CHECK(counterValue(scene, entity) == 100);
    CHECK(context.quitRequested);
    (*reloaded)->release(scene);
}

TEST_CASE("Libraries that are not game modules are refused", "[runtime][game]")
{
    const TemporaryDirectory copies;
    CHECK_FALSE(GameModule::load(copies.path / "missing.dll", copies.path).has_value());
    const std::filesystem::path engine = testModule.parent_path() / "devex-engine.dll";
    const auto module = GameModule::load(engine, copies.path);
    REQUIRE_FALSE(module.has_value());
    CHECK(module.error().message.find("not a game module") != std::string::npos);
}

TEST_CASE("Incompatible game modules are refused and released before registration", "[runtime][game]")
{
    const TemporaryDirectory temporary;
    std::filesystem::create_directories(temporary.path);
    const std::filesystem::path source = temporary.path / testModule.filename();
    REQUIRE(std::filesystem::copy_file(DEVEX_TEST_INCOMPATIBLE_GAME_MODULE, source));
    std::filesystem::path copies;
    SECTION("A player loads the module in place")
    {
    }
    SECTION("The editor loads a disposable copy")
    {
        copies = temporary.path / "copies";
    }

    CHECK(devex::scene::componentRegistry().find("IncompatibleGameComponent") == nullptr);
    const auto module = GameModule::load(source, copies);
    REQUIRE_FALSE(module.has_value());
    CHECK(module.error().code == devex::core::ErrorCode::Unsupported);
    CHECK(devex::scene::componentRegistry().find("IncompatibleGameComponent") == nullptr);

    // On Windows a DLL still loaded by the failed attempt cannot be replaced or deleted.
    // Both the original and the editor's copy must be released for the automatic rebuild.
    std::error_code error;
    CHECK(std::filesystem::remove(source, error));
    CHECK_FALSE(error);
    if (!copies.empty())
    {
        std::size_t removed = 0;
        for (const auto& entry : std::filesystem::directory_iterator(copies))
        {
            CHECK(std::filesystem::remove(entry.path(), error));
            CHECK_FALSE(error);
            ++removed;
        }
        CHECK(removed == 1);
    }
}
