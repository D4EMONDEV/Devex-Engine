#include "tools/Selection.hpp"
#include "tools/ToolsState.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/Scene.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <format>
#include <string>

using devex::asset::AssetId;
using devex::core::Uuid;
using devex::scene::Entity;
using devex::scene::Scene;
using devex::tools::detail::Selection;

namespace {

[[nodiscard]] Uuid uuid(const char* text)
{
    return *Uuid::parse(text);
}

const AssetId lampPrefab{uuid("a1000000-0000-4000-8000-000000000001")};
const AssetId pairPrefab{uuid("a1000000-0000-4000-8000-000000000002")};
const Uuid lampRoot = uuid("b1000000-0000-4000-8000-000000000001");
const Uuid lampBulb = uuid("b1000000-0000-4000-8000-000000000002");
const Uuid pairRoot = uuid("c1000000-0000-4000-8000-000000000001");
const Uuid pairLeft = uuid("c1000000-0000-4000-8000-000000000002");

// A lamp with a bulb, and a pair holding one lamp, read for the duration of a test.
class Prefabs
{
public:
    Prefabs()
    {
        devex::scene::setPrefabSourceLoader([](AssetId id) -> devex::core::Result<std::string> {
            if (id == lampPrefab)
            {
                return std::format(R"([scene format=1]

[entity uuid="{}" name="Lamp"]

[entity uuid="{}" name="Bulb"]
parent = "{}"
)",
                                   lampRoot, lampBulb, lampRoot);
            }
            if (id == pairPrefab)
            {
                return std::format(R"([scene format=1]

[entity uuid="{}" name="Pair"]

[entity uuid="{}" name="Left"]
parent = "{}"
prefab = asset("{}")
)",
                                   pairRoot, pairLeft, pairRoot, lampPrefab.uuid);
            }
            return devex::core::makeError(devex::core::ErrorCode::NotFound, "no prefab {}", id.uuid);
        });
    }

    ~Prefabs()
    {
        devex::scene::setPrefabSourceLoader({});
    }

    Prefabs(const Prefabs&) = delete;
    Prefabs& operator=(const Prefabs&) = delete;
};

} // namespace

TEST_CASE("A selection keeps its entities in order, the last one active", "[tools][selection]")
{
    const Uuid first = Uuid::generate();
    const Uuid second = Uuid::generate();
    const Uuid third = Uuid::generate();
    Selection selection;
    CHECK(selection.empty());
    CHECK(selection.active().isNil());

    selection.set(first);
    selection.add(second);
    selection.add(third);
    CHECK(selection.size() == 3);
    CHECK(selection.active() == third);
    // Adding a selected entity makes it active without repeating it.
    selection.add(first);
    CHECK(selection.size() == 3);
    CHECK(selection.active() == first);

    selection.toggle(second);
    CHECK_FALSE(selection.contains(second));
    selection.toggle(second);
    CHECK(selection.active() == second);

    selection.set(Uuid{});
    CHECK(selection.empty());
    const std::array several{first, Uuid{}, second, first};
    selection.set(several);
    CHECK(selection.size() == 2);
    CHECK(selection.active() == first);
}

TEST_CASE("The roots of a selection skip what their ancestors carry, and follow the hierarchy", "[tools][selection]")
{
    Scene scene;
    const Entity table = scene.createEntity("Table");
    const Entity cup = scene.createEntity("Cup");
    REQUIRE(scene.setParent(cup, table));
    const Entity chair = scene.createEntity("Chair");
    const Entity gone = scene.createEntity("Gone");

    Selection selection;
    selection.add(scene.uuid(chair));
    selection.add(scene.uuid(cup));
    selection.add(scene.uuid(table));
    selection.add(scene.uuid(gone));
    scene.destroyEntity(gone);
    selection.prune(scene);
    CHECK(selection.size() == 3);

    const std::vector<Entity> roots = devex::tools::detail::selectedRoots(scene, selection);
    REQUIRE(roots.size() == 2);
    CHECK(roots[0] == table);
    CHECK(roots[1] == chair);
    CHECK(devex::tools::detail::isUnderAny(scene, cup, selection));
}

TEST_CASE("Clicks on a prefab instance go down from its outermost root to the entity clicked", "[tools][selection][prefab]")
{
    const Prefabs prefabs;
    Scene scene;
    const auto pair = devex::scene::instantiatePrefab(scene, pairPrefab);
    REQUIRE(pair.has_value());
    const Uuid left = devex::scene::derivePrefabUuid(scene.uuid(*pair), pairLeft);
    const Entity leftLamp = scene.findEntity(left);
    REQUIRE(leftLamp.isValid());
    const Entity bulb = scene.firstChild(leftLamp);
    REQUIRE(bulb.isValid());
    const Entity loose = scene.createEntity("Loose");

    using devex::tools::detail::clickTarget;
    CHECK(clickTarget(scene, bulb, Uuid{}) == *pair);
    CHECK(clickTarget(scene, bulb, scene.uuid(*pair)) == leftLamp);
    CHECK(clickTarget(scene, bulb, left) == bulb);
    CHECK(clickTarget(scene, bulb, scene.uuid(bulb)) == bulb);
    // Something else selected starts again from the outermost instance.
    CHECK(clickTarget(scene, bulb, scene.uuid(loose)) == *pair);
    CHECK(clickTarget(scene, loose, scene.uuid(bulb)) == loose);
    CHECK(devex::tools::detail::outermostTarget(scene, bulb) == *pair);
}

TEST_CASE("Copies are named after their original, counting on", "[tools][selection]")
{
    Scene scene;
    const Entity crates = scene.createEntity("Crates");
    for (const char* name : {"Crate 1", "Crate 2", "Crate"})
    {
        REQUIRE(scene.setParent(scene.createEntity(name), crates));
    }
    using devex::tools::detail::uniqueChildName;
    CHECK(uniqueChildName(scene, crates, "Crate 1") == "Crate 3");
    CHECK(uniqueChildName(scene, crates, "Crate") == "Crate 3");
    CHECK(uniqueChildName(scene, crates, "Lamp") == "Lamp");
    // Names given to earlier copies of the same paste count as taken.
    CHECK(uniqueChildName(scene, crates, "Crate 1", {"Crate 3"}) == "Crate 4");
    CHECK(uniqueChildName(scene, Entity{}, "Crates") == "Crates 2");
}
