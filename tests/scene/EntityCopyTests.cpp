#include <devex/asset/AssetId.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/EntityCopy.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <format>
#include <string>
#include <vector>

using devex::asset::AssetId;
using devex::core::Uuid;
using devex::scene::Entity;
using devex::scene::EntityRef;
using devex::scene::Scene;

namespace test {

// Ties an entity to others.
struct Leash
{
    EntityRef target;
    std::vector<EntityRef> others;
};
DEVEX_DECLARE_REFLECTION(Leash);

DEVEX_REFLECT(Leash)
{
    type.field("target", &Leash::target).field("others", &Leash::others);
}

} // namespace test

namespace {

[[nodiscard]] Uuid uuid(const char* text)
{
    return *Uuid::parse(text);
}

const AssetId lampPrefab{uuid("f0000000-0000-4000-8000-000000000001")};
const Uuid lampRoot = uuid("f1000000-0000-4000-8000-000000000001");
const Uuid lampBulb = uuid("f1000000-0000-4000-8000-000000000002");

// A lamp with a bulb, read by the prefab loader for the duration of a test.
class LampSource
{
public:
    LampSource()
    {
        devex::scene::setPrefabSourceLoader([](AssetId id) -> devex::core::Result<std::string> {
            if (id != lampPrefab)
            {
                return devex::core::makeError(devex::core::ErrorCode::NotFound, "no prefab {}", id.uuid);
            }
            return std::format(R"([scene format=1]

[entity uuid="{}" name="Lamp"]

[component type="Transform"]

[entity uuid="{}" name="Bulb"]
parent = "{}"

[component type="Transform"]
position = vec3(0, 2, 0)
)",
                               lampRoot, lampBulb, lampRoot);
        });
    }

    ~LampSource()
    {
        devex::scene::setPrefabSourceLoader({});
    }

    LampSource(const LampSource&) = delete;
    LampSource& operator=(const LampSource&) = delete;
};

} // namespace

TEST_CASE("Copied entities get new UUIDs, and references between them follow", "[scene][copy]")
{
    devex::scene::registerComponent<test::Leash>();
    Scene scene;
    const Entity outside = scene.createEntity("Outside");
    const Entity dog = scene.createEntity("Dog");
    const Entity collar = scene.createEntity("Collar");
    REQUIRE(scene.setParent(collar, dog));
    const Entity owner = scene.createEntity("Owner");
    test::Leash& leash = scene.add<test::Leash>(dog);
    leash.target = scene.reference(owner);
    leash.others = {scene.reference(collar), scene.reference(outside)};

    // The collar is under the dog: it is copied with it, once.
    const std::array roots{dog, owner, collar};
    const std::string text = devex::scene::saveEntityTrees(scene, roots);
    CHECK(devex::scene::isEntityCopy(text));
    const auto copies = devex::scene::copyEntityTrees(text);
    REQUIRE(copies.has_value());
    REQUIRE(copies->size() == 2);
    CHECK((*copies)[0].name == "Dog");
    CHECK((*copies)[1].name == "Owner");
    for (const devex::scene::EntityTreeCopy& copy : *copies)
    {
        REQUIRE(devex::scene::loadEntityTree(scene, copy.text, Entity{}).has_value());
    }

    const Entity dogCopy = scene.findEntity((*copies)[0].root);
    REQUIRE(dogCopy.isValid());
    CHECK(dogCopy != dog);
    const Entity collarCopy = scene.firstChild(dogCopy);
    REQUIRE(collarCopy.isValid());
    CHECK(collarCopy != collar);
    CHECK(scene.name(collarCopy) == "Collar");
    const test::Leash& copied = scene.get<test::Leash>(dogCopy);
    CHECK(copied.target.uuid == (*copies)[1].root);
    REQUIRE(copied.others.size() == 2);
    CHECK(copied.others[0].uuid == scene.uuid(collarCopy));
    // An entity that was not copied is still the one referred to.
    CHECK(copied.others[1].uuid == scene.uuid(outside));
    // The originals are untouched.
    CHECK(scene.get<test::Leash>(dog).target == scene.reference(owner));

    // The same copy pastes again as other entities.
    const auto again = devex::scene::copyEntityTrees(text);
    REQUIRE(again.has_value());
    CHECK((*again)[0].root != (*copies)[0].root);

    CHECK_FALSE(devex::scene::copyEntityTrees("[scene format=1]").has_value());
    CHECK_FALSE(devex::scene::isEntityCopy("Some text from elsewhere"));
}

TEST_CASE("Copied prefab instances stay linked, with their added entities and references", "[scene][copy][prefab]")
{
    devex::scene::registerComponent<test::Leash>();
    const LampSource source;
    Scene scene;
    const auto instance = devex::scene::instantiatePrefab(scene, lampPrefab);
    REQUIRE(instance.has_value());
    const Entity bulb = scene.findEntity(devex::scene::derivePrefabUuid(scene.uuid(*instance), lampBulb));
    REQUIRE(bulb.isValid());
    // An entity added under the bulb, tied to it.
    const Entity moth = scene.createEntity("Moth");
    REQUIRE(scene.setParent(moth, bulb));
    scene.add<test::Leash>(moth).target = scene.reference(bulb);

    const std::array roots{*instance};
    const auto copies = devex::scene::copyEntityTrees(devex::scene::saveEntityTrees(scene, roots));
    REQUIRE(copies.has_value());
    REQUIRE(copies->size() == 1);
    const auto pasted = devex::scene::loadEntityTree(scene, copies->front().text, Entity{});
    REQUIRE(pasted.has_value());

    CHECK(scene.uuid(*pasted) == copies->front().root);
    REQUIRE(scene.has<devex::scene::PrefabInstance>(*pasted));
    CHECK(scene.get<devex::scene::PrefabInstance>(*pasted).prefab == lampPrefab);
    const Entity bulbCopy = scene.findEntity(devex::scene::derivePrefabUuid(copies->front().root, lampBulb));
    REQUIRE(bulbCopy.isValid());
    const Entity mothCopy = scene.firstChild(bulbCopy);
    REQUIRE(mothCopy.isValid());
    CHECK(mothCopy != moth);
    CHECK(scene.name(mothCopy) == "Moth");
    CHECK(scene.get<test::Leash>(mothCopy).target.uuid == scene.uuid(bulbCopy));
}
