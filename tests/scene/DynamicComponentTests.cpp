#include <devex/asset/AssetId.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/DynamicComponent.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using devex::reflection::ValueKind;
using devex::scene::DynamicComponentLayout;
using devex::scene::DynamicComponentPool;
using devex::scene::DynamicField;
using devex::scene::Entity;
using devex::scene::Scene;

namespace {

[[nodiscard]] std::shared_ptr<const DynamicComponentLayout> makeLayout(std::string name = "Mover")
{
    const std::vector<DynamicField> fields{
        {.name = "speed", .kind = ValueKind::Float, .angle = true},
        {.name = "label", .kind = ValueKind::String},
        {.name = "direction", .kind = ValueKind::Vec3},
        {.name = "enabled", .kind = ValueKind::Bool},
        {.name = "mode", .kind = ValueKind::Enum, .enumNames = {"idle", "running"}},
    };
    devex::core::Result<std::shared_ptr<const DynamicComponentLayout>> layout =
        DynamicComponentLayout::create(std::move(name), fields);
    REQUIRE(layout.has_value());
    return *layout;
}

} // namespace

TEST_CASE("A component type described at runtime lays out its fields", "[scene][dynamic]")
{
    const std::shared_ptr<const DynamicComponentLayout> layout = makeLayout();
    const devex::reflection::TypeInfo& type = layout->type();
    CHECK(type.name == "Mover");
    REQUIRE(type.fields.size() == 5);
    CHECK(type.fields[0].kind == ValueKind::Float);
    CHECK(type.fields[0].angle);
    REQUIRE(type.findField("mode") != nullptr);
    REQUIRE(type.findField("mode")->enumNames.size() == 2);
    CHECK(type.findField("mode")->enumNames[1] == "running");
    CHECK(type.findField("mode")->enumSize == 4);
    CHECK(layout->offsets().size() == 5);
    CHECK(layout->size() >= sizeof(float) + sizeof(std::string) + sizeof(devex::math::Vec3));

    // Fields are reached through the reflection, like those of a C++ component.
    std::vector<std::byte> memory(layout->size());
    layout->construct(memory.data());
    *static_cast<float*>(type.fields[0].address(memory.data())) = 2.5f;
    *static_cast<std::string*>(type.findField("label")->address(memory.data())) = "hello";
    CHECK(*static_cast<const float*>(type.fields[0].address(memory.data())) == 2.5f);
    CHECK(*static_cast<const std::string*>(type.findField("label")->address(memory.data())) == "hello");
    layout->destroy(memory.data());

    CHECK_FALSE(DynamicComponentLayout::create("", {}).has_value());
    const std::vector<DynamicField> twice{{.name = "speed", .kind = ValueKind::Float},
                                          {.name = "speed", .kind = ValueKind::Float}};
    CHECK(DynamicComponentLayout::create("Twice", twice).error().code == devex::core::ErrorCode::AlreadyExists);
    const std::vector<DynamicField> emptyEnum{{.name = "mode", .kind = ValueKind::Enum}};
    CHECK_FALSE(DynamicComponentLayout::create("Empty", emptyEnum).has_value());
}

TEST_CASE("Components described at runtime are stored, copied and removed", "[scene][dynamic]")
{
    const std::shared_ptr<const DynamicComponentLayout> layout = makeLayout();
    const devex::reflection::FieldInfo& label = *layout->type().findField("label");
    DynamicComponentPool pool(layout);

    Scene scene;
    const Entity first = scene.createEntity("First");
    const Entity second = scene.createEntity("Second");
    const Entity third = scene.createEntity("Third");
    for (const auto& [entity, text] : {std::pair{first, "one"}, {second, "two"}, {third, "three"}})
    {
        void* const component = pool.emplace(entity);
        REQUIRE(component != nullptr);
        *static_cast<std::string*>(label.address(component)) = text;
    }
    CHECK(pool.size() == 3);
    CHECK(pool.emplace(first) == pool.find(first));
    CHECK(*static_cast<const std::string*>(label.address(pool.find(second))) == "two");

    const std::unique_ptr<devex::scene::ComponentPoolBase> copy = pool.clone();
    pool.remove(second);
    CHECK(pool.size() == 2);
    CHECK(pool.find(second) == nullptr);
    CHECK(*static_cast<const std::string*>(label.address(pool.find(third))) == "three");
    // The copy keeps its own strings.
    const auto& copied = static_cast<const DynamicComponentPool&>(*copy);
    CHECK(copied.size() == 3);
    CHECK(*static_cast<const std::string*>(label.address(copied.find(second))) == "two");
}

TEST_CASE("Components described at runtime are saved and loaded like the others", "[scene][dynamic]")
{
    devex::scene::ComponentRegistry& registry = devex::scene::componentRegistry();
    REQUIRE(registry.addDynamic(makeLayout("Mover")));
    CHECK_FALSE(registry.addDynamic(makeLayout("Mover")));
    const devex::scene::ComponentType* const type = registry.find("Mover");
    REQUIRE(type != nullptr);
    REQUIRE(type->layout != nullptr);

    std::string text;
    {
        Scene scene;
        const Entity entity = scene.createEntity("Runner");
        scene.add<devex::scene::Transform>(entity);
        void* const component = type->emplace(scene, entity);
        REQUIRE(component != nullptr);
        *static_cast<float*>(type->type->findField("speed")->address(component)) = 1.5f;
        *static_cast<std::string*>(type->type->findField("label")->address(component)) = "written";
        *static_cast<std::uint32_t*>(type->type->findField("mode")->address(component)) = 1;
        text = devex::scene::saveScene(scene);
    }
    CHECK(text.find("[component type=\"Mover\"]") != std::string::npos);
    CHECK(text.find("label = \"written\"") != std::string::npos);
    CHECK(text.find("mode = \"running\"") != std::string::npos);

    const devex::core::Result<Scene> loaded = devex::scene::loadScene(text);
    REQUIRE(loaded.has_value());
    const Entity entity = loaded->findEntity(loaded->uuid(loaded->firstRoot()));
    const void* const component = type->find(*loaded, entity);
    REQUIRE(component != nullptr);
    CHECK(*static_cast<const float*>(type->type->findField("speed")->address(component)) == 1.5f);
    CHECK(*static_cast<const std::string*>(type->type->findField("label")->address(component)) == "written");

    // Unregistering keeps the values of scenes as text, as unloading game code does.
    Scene scene = loaded->clone();
    CHECK(devex::scene::preserveComponentPool(scene, type->index) == 1);
    CHECK(registry.remove("Mover"));
    CHECK(registry.find("Mover") == nullptr);
    CHECK(devex::scene::saveScene(scene).find("label = \"written\"") != std::string::npos);
}
