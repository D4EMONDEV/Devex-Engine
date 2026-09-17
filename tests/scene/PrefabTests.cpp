#include <devex/asset/AssetId.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <format>
#include <string>
#include <unordered_map>
#include <vector>

using devex::asset::AssetId;
using devex::core::Uuid;
using devex::math::Vec3;
using devex::scene::Entity;
using devex::scene::PointLight;
using devex::scene::PrefabEntity;
using devex::scene::PrefabInstance;
using devex::scene::Scene;
using devex::scene::Transform;

namespace {

[[nodiscard]] Uuid uuid(const char* text)
{
    return *Uuid::parse(text);
}

const AssetId lampPrefab{uuid("a0000000-0000-4000-8000-000000000001")};
const AssetId pairPrefab{uuid("a0000000-0000-4000-8000-000000000002")};
const AssetId groupPrefab{uuid("a0000000-0000-4000-8000-000000000003")};
const AssetId missingPrefab{uuid("a0000000-0000-4000-8000-00000000dead")};

const Uuid lampRoot = uuid("b0000000-0000-4000-8000-000000000001");
const Uuid lampBulb = uuid("b0000000-0000-4000-8000-000000000002");
const Uuid pairLeft = uuid("c0000000-0000-4000-8000-000000000001");
const Uuid pairRight = uuid("c0000000-0000-4000-8000-000000000002");
const Uuid pairRoot = uuid("c0000000-0000-4000-8000-000000000003");
const Uuid instanceUuid = uuid("d0000000-0000-4000-8000-000000000001");

// A lamp: a post with a bulb that lights.
[[nodiscard]] std::string lampText(float bulbHeight = 2.0f)
{
    return std::format(R"([scene format=1]

[entity uuid="{}" name="Lamp"]

[component type="Transform"]
position = vec3(0, 0, 0)

[entity uuid="{}" name="Bulb"]
parent = "{}"

[component type="Transform"]
position = vec3(0, {}, 0)

[component type="PointLight"]
intensity = 800
range = 10
)",
                       lampRoot, lampBulb, lampRoot, bulbHeight);
}

// Two lamps under a root, the right one brighter.
[[nodiscard]] std::string pairText()
{
    return std::format(R"([scene format=1]

[entity uuid="{}" name="Pair"]

[component type="Transform"]

[entity uuid="{}" name="Left"]
parent = "{}"
prefab = asset("{}")

[override type="Transform"]
position = vec3(-1, 0, 0)

[entity uuid="{}" name="Right"]
parent = "{}"
prefab = asset("{}")

[override type="Transform"]
position = vec3(1, 0, 0)

[override target="{}" type="PointLight"]
intensity = 1600
)",
                       pairRoot, pairLeft, pairRoot, lampPrefab.uuid, pairRight, pairRoot, lampPrefab.uuid,
                       lampBulb);
}

// A scene loader over texts in memory, set for the duration of a test.
class PrefabSources
{
public:
    PrefabSources()
    {
        devex::scene::setPrefabSourceLoader([this](AssetId id) -> devex::core::Result<std::string> {
            ++m_reads;
            const auto found = m_texts.find(id);
            if (found == m_texts.end())
            {
                return devex::core::makeError(devex::core::ErrorCode::NotFound, "no prefab {}", id.uuid);
            }
            return found->second;
        });
        set(lampPrefab, lampText());
        set(pairPrefab, pairText());
    }

    ~PrefabSources()
    {
        devex::scene::setPrefabSourceLoader({});
    }

    PrefabSources(const PrefabSources&) = delete;
    PrefabSources& operator=(const PrefabSources&) = delete;

    void set(AssetId id, std::string text)
    {
        m_texts.insert_or_assign(id, std::move(text));
    }

    [[nodiscard]] int reads() const noexcept
    {
        return m_reads;
    }

private:
    std::unordered_map<AssetId, std::string> m_texts;
    int m_reads = 0;
};

class WarningCapture
{
public:
    WarningCapture()
        : m_sink(devex::core::addLogSink([this](const devex::core::LogRecord& record) {
            if (record.level >= devex::core::LogLevel::Warning)
            {
                m_warnings.emplace_back(record.message);
            }
        }))
    {
    }

    ~WarningCapture()
    {
        devex::core::removeLogSink(m_sink);
    }

    WarningCapture(const WarningCapture&) = delete;
    WarningCapture& operator=(const WarningCapture&) = delete;

    [[nodiscard]] bool contains(std::string_view part) const
    {
        for (const std::string& warning : m_warnings)
        {
            if (warning.find(part) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<std::string> m_warnings;
    devex::core::LogSinkId m_sink;
};

[[nodiscard]] std::string sceneWithInstance(AssetId prefab, std::string_view overrides = {})
{
    return std::format("[scene format=1]\n\n[entity uuid=\"{}\" name=\"Instance\"]\nprefab = asset(\"{}\")\n{}{}",
                       instanceUuid, prefab.uuid, overrides.empty() ? "" : "\n", overrides);
}

[[nodiscard]] Scene load(std::string_view text)
{
    devex::core::Result<Scene> scene = devex::scene::loadScene(text);
    REQUIRE(scene.has_value());
    return std::move(*scene);
}

[[nodiscard]] Scene reload(const Scene& scene)
{
    return load(devex::scene::saveScene(scene));
}

} // namespace

TEST_CASE("An instance loads the entities of its prefab with derived UUIDs", "[scene][prefab]")
{
    PrefabSources sources;
    const Scene scene = load(sceneWithInstance(lampPrefab));

    REQUIRE(scene.entityCount() == 2);
    const Entity root = scene.findEntity(instanceUuid);
    REQUIRE(root.isValid());
    CHECK(scene.name(root) == "Instance");
    REQUIRE(scene.has<PrefabInstance>(root));
    CHECK(scene.get<PrefabInstance>(root).prefab == lampPrefab);
    CHECK(scene.get<PrefabEntity>(root).instance == root);
    CHECK(scene.get<PrefabEntity>(root).source == lampRoot);
    CHECK(devex::scene::isPrefabInstanceRoot(scene, root));

    const Uuid bulbUuid = devex::scene::derivePrefabUuid(instanceUuid, lampBulb);
    CHECK(bulbUuid != lampBulb);
    CHECK(bulbUuid == devex::scene::derivePrefabUuid(instanceUuid, lampBulb));
    const Entity bulb = scene.findEntity(bulbUuid);
    REQUIRE(bulb.isValid());
    CHECK(scene.parent(bulb) == root);
    CHECK(scene.name(bulb) == "Bulb");
    CHECK(scene.get<Transform>(bulb).position == Vec3(0.0f, 2.0f, 0.0f));
    CHECK(scene.get<PointLight>(bulb).intensity == 800.0f);
    CHECK(scene.get<PrefabEntity>(bulb).instance == root);
    CHECK(scene.get<PrefabEntity>(bulb).source == lampBulb);
    CHECK_FALSE(devex::scene::isPrefabInstanceRoot(scene, bulb));
    CHECK(devex::scene::owningPrefabInstance(scene, bulb) == root);
}

TEST_CASE("An unchanged instance is saved without overrides", "[scene][prefab]")
{
    PrefabSources sources;
    const std::string text = sceneWithInstance(lampPrefab);
    const std::string saved = devex::scene::saveScene(load(text));
    CHECK(saved == text);
}

TEST_CASE("Saving an instance writes the fields, names and components it changes", "[scene][prefab]")
{
    PrefabSources sources;
    Scene scene = load(sceneWithInstance(lampPrefab));
    const Entity root = scene.findEntity(instanceUuid);
    const Entity bulb = scene.findEntity(devex::scene::derivePrefabUuid(instanceUuid, lampBulb));
    scene.get<Transform>(root).position = Vec3(4.0f, 0.0f, -2.0f);
    scene.get<PointLight>(bulb).intensity = 1200.0f;
    scene.setName(bulb, "Warm bulb");
    scene.add<devex::scene::MeshRenderer>(bulb).mesh = devex::asset::builtin::sphereMesh;

    const std::string saved = devex::scene::saveScene(scene);
    CHECK(saved.find("[override type=\"Transform\"]\nposition = vec3(4, 0, -2)\n\n") != std::string::npos);
    CHECK(saved.find("rotation") == std::string::npos);
    CHECK(saved.find(std::format("[override target=\"{}\" name=\"Warm bulb\"]", lampBulb)) != std::string::npos);
    CHECK(saved.find(std::format("[override target=\"{}\" type=\"PointLight\"]\nintensity = 1200\n", lampBulb)) !=
          std::string::npos);
    CHECK(saved.find(std::format("[override target=\"{}\" type=\"MeshRenderer\"]\nmesh = asset(", lampBulb)) !=
          std::string::npos);
    CHECK(saved.find("[component") == std::string::npos);

    const Scene loaded = load(saved);
    const Entity loadedBulb = loaded.findEntity(devex::scene::derivePrefabUuid(instanceUuid, lampBulb));
    REQUIRE(loadedBulb.isValid());
    CHECK(loaded.get<Transform>(loaded.findEntity(instanceUuid)).position == Vec3(4.0f, 0.0f, -2.0f));
    CHECK(loaded.name(loadedBulb) == "Warm bulb");
    CHECK(loaded.get<PointLight>(loadedBulb).intensity == 1200.0f);
    CHECK(loaded.get<PointLight>(loadedBulb).range == 10.0f);
    CHECK(loaded.get<devex::scene::MeshRenderer>(loadedBulb).mesh == devex::asset::builtin::sphereMesh);
    CHECK(devex::scene::saveScene(loaded) == saved);
}

TEST_CASE("Changes to a prefab reach its instances, under their overrides", "[scene][prefab]")
{
    PrefabSources sources;
    const std::string text = sceneWithInstance(lampPrefab, std::format("[override target=\"{}\" type=\"PointLight\"]\n"
                                                                       "intensity = 300\n",
                                                                       lampBulb));
    const Uuid bulbUuid = devex::scene::derivePrefabUuid(instanceUuid, lampBulb);
    {
        const Scene scene = load(text);
        CHECK(scene.get<Transform>(scene.findEntity(bulbUuid)).position.y == 2.0f);
    }
    sources.set(lampPrefab, lampText(3.5f));
    const Scene scene = load(text);
    const Entity bulb = scene.findEntity(bulbUuid);
    CHECK(scene.get<Transform>(bulb).position.y == 3.5f);
    CHECK(scene.get<PointLight>(bulb).intensity == 300.0f);
}

TEST_CASE("Entities added under the entities of an instance are saved with it", "[scene][prefab]")
{
    PrefabSources sources;
    Scene scene = load(sceneWithInstance(lampPrefab));
    const Uuid bulbUuid = devex::scene::derivePrefabUuid(instanceUuid, lampBulb);
    const Entity added = scene.createEntity("Moth");
    scene.add<Transform>(added);
    REQUIRE(scene.setParent(added, scene.findEntity(bulbUuid)).has_value());
    const Uuid addedUuid = scene.uuid(added);

    const std::string saved = devex::scene::saveScene(scene);
    CHECK(saved.find(std::format("[entity uuid=\"{}\" name=\"Moth\"]\nparent = \"{}\"", addedUuid, bulbUuid)) !=
          std::string::npos);

    const Scene loaded = load(saved);
    const Entity loadedAdded = loaded.findEntity(addedUuid);
    REQUIRE(loadedAdded.isValid());
    CHECK(loaded.parent(loadedAdded) == loaded.findEntity(bulbUuid));
    CHECK_FALSE(loaded.has<PrefabEntity>(loadedAdded));
    CHECK(devex::scene::saveScene(loaded) == saved);

    SECTION("An added entity whose parent left the prefab moves under the instance")
    {
        sources.set(lampPrefab, std::format("[scene format=1]\n\n[entity uuid=\"{}\" name=\"Lamp\"]\n\n"
                                            "[component type=\"Transform\"]\n",
                                            lampRoot));
        WarningCapture warnings;
        const Scene withoutBulb = load(saved);
        CHECK(warnings.contains("no longer exists"));
        CHECK(withoutBulb.parent(withoutBulb.findEntity(addedUuid)) == withoutBulb.findEntity(instanceUuid));
    }
}

TEST_CASE("Prefabs contain instances of other prefabs, overridden at every level", "[scene][prefab]")
{
    PrefabSources sources;
    Scene scene = load(sceneWithInstance(pairPrefab));

    // Lamps × 2 with their bulbs, under the root of the pair.
    REQUIRE(scene.entityCount() == 5);
    const Entity root = scene.findEntity(instanceUuid);
    const Uuid rightUuid = devex::scene::derivePrefabUuid(instanceUuid, pairRight);
    // In the pair, the bulb of the right lamp has a UUID derived from the lamp's.
    const Uuid rightBulbInPair = devex::scene::derivePrefabUuid(pairRight, lampBulb);
    const Uuid rightBulbUuid = devex::scene::derivePrefabUuid(instanceUuid, rightBulbInPair);
    const Entity right = scene.findEntity(rightUuid);
    const Entity rightBulb = scene.findEntity(rightBulbUuid);
    REQUIRE(right.isValid());
    REQUIRE(rightBulb.isValid());
    CHECK(scene.get<Transform>(right).position.x == 1.0f);
    CHECK(scene.get<PointLight>(rightBulb).intensity == 1600.0f);
    // Every entity is saved by the outermost instance, and the inner instances stay marked.
    CHECK(scene.get<PrefabEntity>(rightBulb).instance == root);
    CHECK(scene.get<PrefabEntity>(right).instance == root);
    CHECK(scene.get<PrefabInstance>(right).prefab == lampPrefab);
    CHECK_FALSE(devex::scene::isPrefabInstanceRoot(scene, right));
    CHECK(devex::scene::prefabUses(pairPrefab, lampPrefab));
    CHECK_FALSE(devex::scene::prefabUses(lampPrefab, pairPrefab));
    CHECK(devex::scene::prefabReferences(pairText()) == std::vector{lampPrefab});

    scene.get<PointLight>(rightBulb).intensity = 50.0f;
    const std::string saved = devex::scene::saveScene(scene);
    CHECK(saved.find(std::format("[override target=\"{}\" type=\"PointLight\"]\nintensity = 50\n", rightBulbInPair)) !=
          std::string::npos);
    const Scene loaded = load(saved);
    CHECK(loaded.get<PointLight>(loaded.findEntity(rightBulbUuid)).intensity == 50.0f);
    CHECK(loaded.get<Transform>(loaded.findEntity(rightUuid)).position.x == 1.0f);
}

TEST_CASE("The roots of a prefab with several roots are grouped under the instance", "[scene][prefab]")
{
    PrefabSources sources;
    sources.set(groupPrefab, std::format("[scene format=1]\n\n[entity uuid=\"{}\" name=\"A\"]\n\n"
                                         "[entity uuid=\"{}\" name=\"B\"]\n",
                                         lampRoot, lampBulb));
    Scene scene = load(sceneWithInstance(groupPrefab));
    REQUIRE(scene.entityCount() == 3);
    const Entity root = scene.findEntity(instanceUuid);
    CHECK(scene.has<Transform>(root));
    CHECK(scene.get<PrefabEntity>(root).source.isNil());
    const Entity first = scene.firstChild(root);
    REQUIRE(first.isValid());
    CHECK(scene.name(first) == "A");
    CHECK(scene.name(scene.nextSibling(first)) == "B");

    scene.get<Transform>(root).position.z = 5.0f;
    const Scene loaded = reload(scene);
    CHECK(loaded.get<Transform>(loaded.findEntity(instanceUuid)).position.z == 5.0f);
    CHECK(loaded.entityCount() == 3);
}

TEST_CASE("An instance whose prefab is missing is kept as it was written", "[scene][prefab]")
{
    PrefabSources sources;
    const std::string text =
        sceneWithInstance(missingPrefab, std::format("[override target=\"{}\" type=\"PointLight\"]\nintensity = 5\n\n"
                                                     "[override target=\"{}\" name=\"Kept\"]\n",
                                                     lampBulb, lampBulb));
    WarningCapture warnings;
    const Scene scene = load(text);
    CHECK(warnings.contains("cannot load prefab"));
    REQUIRE(scene.entityCount() == 1);
    const Entity root = scene.findEntity(instanceUuid);
    CHECK_FALSE(scene.get<PrefabInstance>(root).resolved);
    CHECK(devex::scene::saveScene(scene) == text);

    // Syntax checks do not read prefabs.
    const int reads = sources.reads();
    const devex::core::Result<Scene> checked =
        devex::scene::loadScene(sceneWithInstance(lampPrefab), devex::scene::PrefabLoading::KeepUnresolved);
    REQUIRE(checked.has_value());
    CHECK(sources.reads() == reads);
    CHECK_FALSE(checked->get<PrefabInstance>(checked->findEntity(instanceUuid)).resolved);
}

TEST_CASE("A prefab that contains itself is not loaded forever", "[scene][prefab]")
{
    PrefabSources sources;
    sources.set(groupPrefab, std::format("[scene format=1]\n\n[entity uuid=\"{}\" name=\"Loop\"]\n\n"
                                         "[entity uuid=\"{}\" name=\"Inner\"]\nparent = \"{}\"\nprefab = asset(\"{}\")\n",
                                         lampRoot, lampBulb, lampRoot, groupPrefab.uuid));
    WarningCapture warnings;
    const Scene scene = load(sceneWithInstance(groupPrefab));
    CHECK(warnings.contains("contains itself"));
    CHECK(scene.entityCount() >= 2);
    CHECK(devex::scene::prefabUses(groupPrefab, groupPrefab));
}

TEST_CASE("Game code instantiates prefabs", "[scene][prefab]")
{
    PrefabSources sources;
    Scene scene;
    const Entity parent = scene.createEntity("Parent");
    const devex::core::Result<Entity> lamp = devex::scene::instantiatePrefab(scene, pairPrefab, parent);
    REQUIRE(lamp.has_value());
    CHECK(scene.parent(*lamp) == parent);
    CHECK(scene.entityCount() == 6);
    CHECK(devex::scene::isPrefabInstanceRoot(scene, *lamp));
    const devex::core::Result<Entity> other = devex::scene::instantiatePrefab(scene, pairPrefab);
    REQUIRE(other.has_value());
    CHECK(scene.entityCount() == 11);
    CHECK_FALSE(devex::scene::instantiatePrefab(scene, missingPrefab).has_value());
    CHECK(reload(scene).entityCount() == 11);
}

TEST_CASE("Instances are rebuilt when their prefab changes", "[scene][prefab]")
{
    PrefabSources sources;
    Scene scene = load(sceneWithInstance(pairPrefab));
    const Entity root = scene.findEntity(instanceUuid);
    scene.get<Transform>(root).position.x = 7.0f;
    const Entity added = scene.createEntity("Sign");
    REQUIRE(scene.setParent(added, root).has_value());
    const Uuid addedUuid = scene.uuid(added);
    const Entity other = scene.createEntity("Unrelated");
    const Uuid otherUuid = scene.uuid(other);

    const std::vector<devex::scene::PrefabInstanceSnapshot> snapshots =
        devex::scene::snapshotPrefabInstances(scene, std::vector{lampPrefab});
    REQUIRE(snapshots.size() == 1);
    CHECK(snapshots.front().root == instanceUuid);
    CHECK(snapshots.front().nextSibling == otherUuid);

    sources.set(lampPrefab, lampText(9.0f));
    CHECK(devex::scene::rebuildPrefabInstances(scene, snapshots) == 1);

    const Entity rebuilt = scene.findEntity(instanceUuid);
    REQUIRE(rebuilt.isValid());
    CHECK(scene.nextSibling(rebuilt) == scene.findEntity(otherUuid));
    CHECK(scene.get<Transform>(rebuilt).position.x == 7.0f);
    CHECK(scene.parent(scene.findEntity(addedUuid)) == rebuilt);
    const Uuid bulb = devex::scene::derivePrefabUuid(instanceUuid, devex::scene::derivePrefabUuid(pairRight, lampBulb));
    CHECK(scene.get<Transform>(scene.findEntity(bulb)).position.y == 9.0f);
    CHECK(scene.get<PointLight>(scene.findEntity(bulb)).intensity == 1600.0f);

    CHECK(devex::scene::snapshotPrefabInstances(scene, std::vector{groupPrefab}).empty());
}

TEST_CASE("Instances are unpacked, reverted and copied as ordinary entities", "[scene][prefab]")
{
    PrefabSources sources;
    Scene scene = load(sceneWithInstance(lampPrefab));
    const Entity root = scene.findEntity(instanceUuid);
    const Uuid bulbUuid = devex::scene::derivePrefabUuid(instanceUuid, lampBulb);
    const Entity bulb = scene.findEntity(bulbUuid);
    scene.get<Transform>(root).position.y = 1.0f;
    scene.get<PointLight>(bulb).intensity = 42.0f;
    scene.setName(bulb, "Renamed");

    SECTION("Unpacking")
    {
        const std::string unpacked = devex::scene::saveUnpackedEntityTree(scene, root);
        CHECK(unpacked.find("prefab") == std::string::npos);
        CHECK(unpacked.find("[override") == std::string::npos);
        scene.destroyEntity(root);
        const devex::core::Result<Entity> local = devex::scene::loadEntityTree(scene, unpacked, Entity{});
        REQUIRE(local.has_value());
        CHECK_FALSE(scene.has<PrefabInstance>(*local));
        CHECK_FALSE(scene.has<PrefabEntity>(scene.findEntity(bulbUuid)));
        CHECK(scene.get<PointLight>(scene.findEntity(bulbUuid)).intensity == 42.0f);
    }
    SECTION("Reverting")
    {
        const std::string reverted = devex::scene::saveRevertedPrefabInstance(scene, root);
        CHECK(reverted.find("Renamed") == std::string::npos);
        CHECK(reverted.find("PointLight") == std::string::npos);
        scene.destroyEntity(root);
        const devex::core::Result<Entity> restored = devex::scene::loadEntityTree(scene, reverted, Entity{});
        REQUIRE(restored.has_value());
        CHECK(scene.get<Transform>(*restored).position.y == 1.0f);
        const Entity restoredBulb = scene.findEntity(bulbUuid);
        CHECK(scene.name(restoredBulb) == "Bulb");
        CHECK(scene.get<PointLight>(restoredBulb).intensity == 800.0f);
    }
    SECTION("Copying an entity of the instance")
    {
        const std::string copy = devex::scene::saveEntityTree(scene, bulb);
        CHECK(copy.find("prefab") == std::string::npos);
        CHECK(copy.find("[component type=\"PointLight\"]") != std::string::npos);
    }
}
