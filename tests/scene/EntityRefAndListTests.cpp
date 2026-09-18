#include <devex/asset/AssetId.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/DynamicComponent.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <format>
#include <string>
#include <vector>

using devex::asset::AssetId;
using devex::core::Uuid;
using devex::math::Vec3;
using devex::reflection::ValueKind;
using devex::scene::Entity;
using devex::scene::EntityRef;
using devex::scene::Scene;

namespace test {

// A door that opens for the entities of a list, at points of another.
struct Door
{
    EntityRef opener;
    std::vector<EntityRef> keys;
    std::vector<Vec3> stops;
    std::vector<std::string> messages;
    float speed = 1.0f;
};
DEVEX_DECLARE_REFLECTION(Door);

DEVEX_REFLECT(Door)
{
    type.field("opener", &Door::opener)
        .field("keys", &Door::keys)
        .field("stops", &Door::stops)
        .field("messages", &Door::messages)
        .field("speed", &Door::speed);
}

} // namespace test

namespace {

[[nodiscard]] Uuid uuid(const char* text)
{
    return *Uuid::parse(text);
}

const AssetId gatePrefab{uuid("e0000000-0000-4000-8000-000000000001")};
const Uuid gateRoot = uuid("e1000000-0000-4000-8000-000000000001");
const Uuid gateLever = uuid("e1000000-0000-4000-8000-000000000002");
const Uuid outside = uuid("e1000000-0000-4000-8000-0000000000ff");
const Uuid instanceUuid = uuid("e2000000-0000-4000-8000-000000000001");

// A gate whose door is opened by its own lever.
[[nodiscard]] std::string gateText()
{
    return std::format(R"([scene format=1]

[entity uuid="{0}" name="Gate"]

[component type="Transform"]

[component type="Door"]
opener = entity("{1}")
keys = list(entity("{1}"), entity("{2}"), entity())
stops = list(vec3(0, 0, 0), vec3(0, 3, 0))

[entity uuid="{1}" name="Lever"]
parent = "{0}"

[component type="Transform"]
)",
                       gateRoot, gateLever, outside);
}

} // namespace

TEST_CASE("Lists and entity references are reflected with their offsets", "[reflection][scene]")
{
    const devex::reflection::TypeInfo& type = devex::reflection::typeInfo<test::Door>();
    CHECK(type.size == sizeof(test::Door));
    CHECK(type.alignment == alignof(test::Door));

    const devex::reflection::FieldInfo& opener = *type.findField("opener");
    CHECK(opener.kind == ValueKind::Entity);
    CHECK(opener.list == nullptr);
    CHECK(opener.offset == offsetof(test::Door, opener));

    const devex::reflection::FieldInfo& stops = *type.findField("stops");
    CHECK(stops.kind == ValueKind::Vec3);
    REQUIRE(stops.list != nullptr);
    CHECK(stops.offset == offsetof(test::Door, stops));
    CHECK(type.findField("speed")->offset == offsetof(test::Door, speed));

    test::Door door;
    stops.list->resize(stops.address(&door), 2);
    CHECK(door.stops.size() == 2);
    *static_cast<Vec3*>(stops.list->element(stops.address(&door), 1)) = Vec3(1.0f, 2.0f, 3.0f);
    stops.list->insert(stops.address(&door), 0);
    CHECK(door.stops[2] == Vec3(1.0f, 2.0f, 3.0f));
    stops.list->erase(stops.address(&door), 1);
    CHECK(stops.list->size(stops.address(&door)) == 2);

    // New rotations in a list start as the identity rotation.
    std::vector<devex::math::Quat> rotations;
    devex::reflection::listOps<devex::math::Quat>().resize(&rotations, 1);
    CHECK(rotations[0] == devex::math::Quat(1.0f, 0.0f, 0.0f, 0.0f));
}

TEST_CASE("Lists and entity references are written as text and read back", "[scene]")
{
    const devex::reflection::TypeInfo& type = devex::reflection::typeInfo<test::Door>();
    test::Door door;
    door.opener = EntityRef{gateLever};
    door.keys = {EntityRef{gateLever}, EntityRef{}};
    door.messages = {"open", "closed"};

    const devex::serialization::TextValue opener = devex::scene::writeFieldValue(*type.findField("opener"), &door.opener);
    CHECK(devex::serialization::formatValue(opener) == std::format("entity(\"{}\")", gateLever));
    const devex::serialization::TextValue keys = devex::scene::writeFieldValue(*type.findField("keys"), &door.keys);
    CHECK(devex::serialization::formatValue(keys) == std::format("list(entity(\"{}\"), entity())", gateLever));
    CHECK(devex::serialization::formatValue(devex::scene::writeFieldValue(*type.findField("stops"), &door.stops)) ==
          "list()");

    test::Door copy;
    REQUIRE(devex::scene::readFieldValue(*type.findField("opener"), opener, &copy.opener));
    REQUIRE(devex::scene::readFieldValue(*type.findField("keys"), keys, &copy.keys));
    REQUIRE(devex::scene::readFieldValue(
        *type.findField("messages"), devex::scene::writeFieldValue(*type.findField("messages"), &door.messages),
        &copy.messages));
    CHECK(copy.opener == door.opener);
    CHECK(copy.keys == door.keys);
    CHECK(copy.messages == door.messages);

    // A wrong element leaves the list as it was.
    const devex::core::Result<devex::serialization::TextDocument> wrong =
        devex::serialization::parseText("[x]\nvalue = list(\"a\", 3)\n");
    REQUIRE(wrong.has_value());
    CHECK_FALSE(devex::scene::readFieldValue(*type.findField("messages"), wrong->sections[0].properties[0].value,
                                             &copy.messages));
    CHECK(copy.messages == door.messages);
}

TEST_CASE("Entity references resolve in their scene and follow prefab instances", "[scene][prefab]")
{
    devex::scene::registerComponent<test::Door>();
    devex::scene::setPrefabSourceLoader([](AssetId id) -> devex::core::Result<std::string> {
        if (id == gatePrefab)
        {
            return gateText();
        }
        return devex::core::makeError(devex::core::ErrorCode::NotFound, "no prefab {}", id.uuid);
    });

    // In the prefab itself, the references name its entities.
    devex::core::Result<Scene> prefab = devex::scene::loadScene(gateText());
    REQUIRE(prefab.has_value());
    const Entity prefabRoot = prefab->findEntity(gateRoot);
    CHECK(prefab->resolve(prefab->get<test::Door>(prefabRoot).opener) == prefab->findEntity(gateLever));
    CHECK(prefab->reference(prefab->findEntity(gateLever)) == EntityRef{gateLever});
    CHECK_FALSE(prefab->resolve(EntityRef{outside}).isValid());
    CHECK_FALSE(prefab->resolve(EntityRef{}).isValid());

    // In an instance, they name the entities of the instance; others stay as they were.
    const std::string sceneText =
        std::format("[scene format=1]\n\n[entity uuid=\"{}\" name=\"Gate\"]\nprefab = asset(\"{}\")\n", instanceUuid,
                    gatePrefab.uuid);
    devex::core::Result<Scene> scene = devex::scene::loadScene(sceneText);
    REQUIRE(scene.has_value());
    const Entity root = scene->findEntity(instanceUuid);
    REQUIRE(root.isValid());
    const test::Door& door = scene->get<test::Door>(root);
    const Entity lever = scene->findEntity(devex::scene::derivePrefabUuid(instanceUuid, gateLever));
    REQUIRE(lever.isValid());
    CHECK(scene->resolve(door.opener) == lever);
    REQUIRE(door.keys.size() == 3);
    CHECK(scene->resolve(door.keys[0]) == lever);
    CHECK(door.keys[1] == EntityRef{outside});
    CHECK(door.keys[2].isNil());
    REQUIRE(door.stops.size() == 2);
    CHECK(door.stops[1] == Vec3(0.0f, 3.0f, 0.0f));

    // An unchanged instance saves no override: its references compare equal to the prefab's.
    const std::string saved = devex::scene::saveScene(*scene);
    CHECK(saved.find("[override") == std::string::npos);

    // The copy that plays keeps them.
    const Scene copy = scene->clone();
    CHECK(copy.resolve(copy.get<test::Door>(root).opener) == lever);

    devex::scene::setPrefabSourceLoader({});
    CHECK(devex::scene::componentRegistry().remove("Door"));
}

TEST_CASE("Components described at runtime hold lists and entity references", "[scene][dynamic]")
{
    const std::vector<devex::scene::DynamicField> fields{
        {.name = "target", .kind = ValueKind::Entity},
        {.name = "points", .kind = ValueKind::Vec3, .list = true},
        {.name = "names", .kind = ValueKind::String, .list = true},
        {.name = "modes", .kind = ValueKind::Enum, .enumNames = {"walk", "run"}, .list = true},
    };
    devex::core::Result<std::shared_ptr<const devex::scene::DynamicComponentLayout>> layout =
        devex::scene::DynamicComponentLayout::create("Patrol", fields);
    REQUIRE(layout.has_value());
    const devex::reflection::TypeInfo& type = (*layout)->type();
    REQUIRE(type.findField("points")->list != nullptr);
    CHECK(type.findField("target")->kind == ValueKind::Entity);

    devex::scene::DynamicComponentPool pool(*layout);
    Scene scene;
    const Entity entity = scene.createEntity("Guard");
    void* const component = pool.emplace(entity);
    const devex::reflection::FieldInfo& names = *type.findField("names");
    names.list->resize(names.address(component), 2);
    *static_cast<std::string*>(names.list->element(names.address(component), 1)) = "second";
    const devex::reflection::FieldInfo& modes = *type.findField("modes");
    REQUIRE(devex::scene::readFieldValue(modes, devex::scene::writeFieldValue(modes, modes.address(component)),
                                         modes.address(component)));

    // Copies own their lists.
    const std::unique_ptr<devex::scene::ComponentPoolBase> copy = pool.clone();
    names.list->erase(names.address(component), 0);
    const auto& copied = static_cast<const devex::scene::DynamicComponentPool&>(*copy);
    const void* const copiedComponent = copied.find(entity);
    CHECK(names.list->size(names.address(const_cast<void*>(copiedComponent))) == 2);
    CHECK(*static_cast<const std::string*>(names.list->element(names.address(const_cast<void*>(copiedComponent)), 1)) ==
          "second");
    CHECK(devex::serialization::formatValue(devex::scene::writeFieldValue(names, names.address(component))) ==
          "list(\"second\")");

    // Lists of booleans are refused.
    const std::vector<devex::scene::DynamicField> flags{{.name = "flags", .kind = ValueKind::Bool, .list = true}};
    CHECK_FALSE(devex::scene::DynamicComponentLayout::create("Flags", flags).has_value());
}
