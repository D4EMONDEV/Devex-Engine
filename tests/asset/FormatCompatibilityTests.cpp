// What keeps the promise of docs/compatibilite.md: the files of every version stay readable. The folders of
// tests/data/formats are never edited: each one holds what a released version wrote.

#include <devex/asset/Artifact.hpp>
#include <devex/asset/Project.hpp>
#include <devex/asset/TerrainMaps.hpp>
#include <devex/asset/import/AnimatorFile.hpp>
#include <devex/asset/import/CurveFile.hpp>
#include <devex/asset/import/MaterialFile.hpp>
#include <devex/asset/import/MetaFile.hpp>
#include <devex/asset/import/ShaderGraphFile.hpp>
#include <devex/asset/import/SpriteFramesFile.hpp>
#include <devex/asset/import/ThemeFile.hpp>
#include <devex/asset/import/TilesetFile.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/File.hpp>
#include <devex/core/Path.hpp>
#include <devex/core/Uuid.hpp>
#include <devex/math/Math.hpp>
#include <devex/platform/Process.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Scene.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/serialization/Text.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using namespace devex;

namespace {

constexpr std::string_view everyComponent = "every_component.dvxscene";
constexpr std::string_view smallTerrain = "small.dvxterrain";
constexpr std::uint32_t smallTerrainResolution = 33;

[[nodiscard]] fs::path formats()
{
    return fs::path(DEVEX_TEST_DATA_DIRECTORY) / "formats";
}

// Set to record what this version writes, after a change that is meant.
[[nodiscard]] bool updating()
{
    return !platform::environmentVariable("DEVEX_UPDATE_FORMATS").empty();
}

[[nodiscard]] std::vector<fs::path> versionFolders()
{
    std::vector<fs::path> folders;
    for (const fs::directory_entry& entry : fs::directory_iterator(formats()))
    {
        if (entry.is_directory())
        {
            folders.push_back(entry.path());
        }
    }
    std::ranges::sort(folders);
    return folders;
}

[[nodiscard]] std::string kindOf(const reflection::FieldInfo& field)
{
    std::string kind(reflection::toString(field.kind));
    if (field.list != nullptr)
    {
        kind += "[]";
    }
    if (!field.enumNames.empty())
    {
        kind += '(';
        for (std::size_t name = 0; name < field.enumNames.size(); ++name)
        {
            kind += std::format("{}{}", name != 0 ? "," : "", field.enumNames[name]);
        }
        kind += ')';
    }
    return kind;
}

[[nodiscard]] std::vector<std::string> enumNamesOf(std::string_view kind)
{
    std::vector<std::string> names;
    const std::size_t open = kind.find('(');
    if (open != std::string_view::npos)
    {
        kind = kind.substr(open + 1, kind.size() - open - 2);
        while (!kind.empty())
        {
            const std::size_t comma = kind.find(',');
            names.emplace_back(kind.substr(0, comma));
            kind = comma == std::string_view::npos ? std::string_view{} : kind.substr(comma + 1);
        }
    }
    return names;
}

// The fields of the engine as files hold them: game code comes and goes.
template <typename Visit>
void forEachSavedField(Visit visit)
{
    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        if (type.layout != nullptr)
        {
            continue;
        }
        for (const reflection::FieldInfo& field : type.type->fields)
        {
            if (!field.runtime)
            {
                visit(type, field);
            }
        }
    }
}

// Away from its default, for a file to prove that the value came back.
void changeValue(const reflection::FieldInfo& field, void* component)
{
    if (field.list != nullptr)
    {
        return;
    }
    void* const value = field.address(component);
    switch (field.kind)
    {
    case reflection::ValueKind::Bool:
        *static_cast<bool*>(value) = !*static_cast<bool*>(value);
        break;
    case reflection::ValueKind::Int32:
        *static_cast<std::int32_t*>(value) += 1;
        break;
    case reflection::ValueKind::UInt32:
        *static_cast<std::uint32_t*>(value) += 1;
        break;
    case reflection::ValueKind::Float:
        *static_cast<float*>(value) += 0.5f;
        break;
    case reflection::ValueKind::Vec2:
        *static_cast<math::Vec2*>(value) += math::Vec2{0.5f};
        break;
    case reflection::ValueKind::Vec3:
        *static_cast<math::Vec3*>(value) += math::Vec3{0.5f};
        break;
    case reflection::ValueKind::Vec4:
        *static_cast<math::Vec4*>(value) += math::Vec4{0.5f};
        break;
    case reflection::ValueKind::Enum:
        if (field.enumSize == 1)
        {
            *static_cast<std::uint8_t*>(value) = static_cast<std::uint8_t>(field.enumNames.size() - 1);
        }
        else
        {
            *static_cast<std::uint32_t*>(value) = static_cast<std::uint32_t>(field.enumNames.size() - 1);
        }
        break;
    default:
        break;
    }
}

[[nodiscard]] std::string schemaText()
{
    std::string schema;
    forEachSavedField([&](const scene::ComponentType& type, const reflection::FieldInfo& field) {
        schema += std::format("{}.{} {}\n", type.name(), field.name, kindOf(field));
    });
    return schema;
}

// Freezes what this version writes in a folder of its own, once.
void freezeThisVersion()
{
    const fs::path folder = formats() / std::string(core::version());
    fs::create_directories(folder);
    if (const fs::path file = folder / everyComponent; !fs::exists(file))
    {
        scene::Scene scene;
        for (const scene::ComponentType& type : scene::componentRegistry().types())
        {
            if (type.layout == nullptr)
            {
                void* const component = type.emplace(scene, scene.createEntity(std::string(type.name())));
                for (const reflection::FieldInfo& field : type.type->fields)
                {
                    if (!field.runtime)
                    {
                        changeValue(field, component);
                    }
                }
            }
        }
        REQUIRE(core::writeTextFile(file, scene::saveScene(scene)));
    }
    if (const fs::path file = folder / smallTerrain; !fs::exists(file))
    {
        asset::TerrainMaps maps;
        maps.resolution = smallTerrainResolution;
        const std::size_t samples = maps.sampleCount();
        maps.heights.resize(samples);
        for (std::size_t sample = 0; sample < samples; ++sample)
        {
            maps.heights[sample] = static_cast<float>(sample) * 0.25f;
        }
        maps.paint.assign(samples * asset::maxTerrainLayers, 7);
        maps.holes.assign(samples, 0);
        maps.grass.assign(samples, 9);
        REQUIRE(core::writeFileAtomically(file, asset::encodeTerrain(maps)));
    }
}

// A folder of its own, removed afterwards.
class TemporaryProject
{
public:
    explicit TemporaryProject(std::string_view header)
        : m_root(fs::temp_directory_path() / ("devex-format-" + core::Uuid::generate().toString()))
    {
        add("Game.dvxproj", header);
    }

    ~TemporaryProject()
    {
        std::error_code error;
        fs::remove_all(m_root, error);
    }

    TemporaryProject(const TemporaryProject&) = delete;
    TemporaryProject& operator=(const TemporaryProject&) = delete;

    void add(const fs::path& file, std::string_view text) const
    {
        fs::create_directories((m_root / file).parent_path());
        REQUIRE(core::writeTextFile(m_root / file, text));
    }

    [[nodiscard]] fs::path file() const
    {
        return m_root / "Game.dvxproj";
    }

private:
    fs::path m_root;
};

} // namespace

TEST_CASE("Versions compare by their numbers", "[format]")
{
    CHECK(core::compareVersions("0.9.0", "0.10.0") < 0);
    CHECK(core::compareVersions("1.2.3", "1.2.2") > 0);
    CHECK(core::compareVersions("1.2", "1.2.0") == 0);
    CHECK(core::compareVersions("", "0.1.0") < 0);
    CHECK(core::compareVersions("0.1.0", "") > 0);
    CHECK(core::compareVersions("", "") == 0);
    CHECK(core::compareVersions("not a version", "0.0.1") < 0);
    CHECK(core::compareVersions("1.2.", "0.0.1") < 0);
}

TEST_CASE("No field that scenes hold disappears without a former name", "[format]")
{
    const fs::path file = formats() / "schema.txt";
    if (updating())
    {
        freezeThisVersion();
        REQUIRE(core::writeTextFile(file, schemaText()));
        SUCCEED("Recorded");
        return;
    }
    const core::Result<std::string> text = core::readTextFile(file);
    REQUIRE(text);

    std::set<std::string> listed;
    std::string_view rest = *text;
    while (!rest.empty())
    {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest = end == std::string_view::npos ? std::string_view{} : rest.substr(end + 1);
        if (!line.empty() && line.back() == '\r')
        {
            line.remove_suffix(1);
        }
        if (line.empty())
        {
            continue;
        }
        const std::size_t dot = line.find('.');
        const std::size_t space = line.find(' ', dot);
        REQUIRE(space != std::string_view::npos);
        const std::string_view typeName = line.substr(0, dot);
        const std::string_view fieldName = line.substr(dot + 1, space - dot - 1);
        const std::string_view kind = line.substr(space + 1);

        const scene::ComponentType* const type = scene::componentRegistry().find(typeName);
        if (type == nullptr)
        {
            FAIL_CHECK(std::format("The component {} is gone and scenes that hold it would lose it. If it was renamed, say so with type.was(\"{}\"); "
                                   "if it was removed on purpose, record it with DEVEX_UPDATE_FORMATS=1 (see docs/compatibilite.md).",
                                   typeName, typeName));
            continue;
        }
        const reflection::FieldInfo* const field = type->type->findField(fieldName);
        if (field == nullptr)
        {
            FAIL_CHECK(std::format("The field {}.{} is gone and scenes would lose its value. If it was renamed, say so with .was = \"{}\"; "
                                   "if it was removed on purpose, record it with DEVEX_UPDATE_FORMATS=1 (see docs/compatibilite.md).",
                                   typeName, fieldName, fieldName));
            continue;
        }
        const std::string now = kindOf(*field);
        if (now.substr(0, now.find('(')) != kind.substr(0, kind.find('(')))
        {
            FAIL_CHECK(std::format("The field {}.{} went from {} to {}: scenes hold the former. Keep the kind, or give the format a step (see docs/compatibilite.md).",
                                   typeName, fieldName, kind, now));
        }
        for (const std::string& name : enumNamesOf(kind))
        {
            if (std::ranges::find(field->enumNames, std::string_view(name)) == field->enumNames.end())
            {
                FAIL_CHECK(std::format("The value {} of {}.{} is gone: scenes hold it.", name, typeName, fieldName));
            }
        }
        listed.insert(std::format("{}.{}", type->name(), field->name));
    }

    forEachSavedField([&](const scene::ComponentType& type, const reflection::FieldInfo& field) {
        const std::string name = std::format("{}.{}", type.name(), field.name);
        if (!listed.contains(name))
        {
            FAIL_CHECK(std::format("{} is new: run this test with DEVEX_UPDATE_FORMATS=1 to record it in {}.", name, core::toUtf8(file)));
        }
    });
}

TEST_CASE("The scene of every version reads without losing a value", "[format]")
{
    if (updating())
    {
        SUCCEED("Recording");
        return;
    }
    for (const fs::path& folder : versionFolders())
    {
        DYNAMIC_SECTION(core::toUtf8(folder.filename()))
        {
            const core::Result<std::string> text = core::readTextFile(folder / everyComponent);
            REQUIRE(text);
            core::Result<scene::Scene> scene = scene::loadScene(*text, scene::PrefabLoading::KeepUnresolved);
            REQUIRE(scene);
            const core::Result<serialization::TextDocument> before = serialization::parseText(*text);
            const core::Result<serialization::TextDocument> after = serialization::parseText(scene::saveScene(*scene));
            REQUIRE(before);
            REQUIRE(after);

            const auto attribute = [](const serialization::TextSection& section, std::string_view name) {
                const std::string* const value = serialization::stringAttribute(section, name);
                return value != nullptr ? *value : std::string();
            };
            std::map<std::pair<std::string, std::string>, const serialization::TextSection*> saved;
            std::string entity;
            for (const serialization::TextSection& section : after->sections)
            {
                if (section.type == "entity")
                {
                    entity = attribute(section, "uuid");
                }
                else if (section.type == "component")
                {
                    saved[{entity, attribute(section, "type")}] = &section;
                }
            }
            for (const serialization::TextSection& section : before->sections)
            {
                if (section.type == "entity")
                {
                    entity = attribute(section, "uuid");
                    continue;
                }
                if (section.type != "component")
                {
                    continue;
                }
                const std::string typeName = attribute(section, "type");
                const scene::ComponentType* const type = scene::componentRegistry().find(typeName);
                if (type == nullptr)
                {
                    FAIL_CHECK(std::format("Scenes of this version hold the component {}, which nothing reads now: say its new name with type.was(\"{}\").", typeName, typeName));
                    continue;
                }
                const auto found = saved.find({entity, std::string(type->name())});
                REQUIRE(found != saved.end());
                for (const serialization::TextProperty& property : section.properties)
                {
                    const reflection::FieldInfo* const field = type->type->findField(property.key);
                    if (field == nullptr)
                    {
                        FAIL_CHECK(std::format("Scenes of this version hold {}.{}, which nothing reads now: say its new name with .was = \"{}\".", typeName, property.key, property.key));
                        continue;
                    }
                    const auto written = std::ranges::find_if(found->second->properties, [&](const serialization::TextProperty& other) { return other.key == field->name; });
                    REQUIRE(written != found->second->properties.end());
                    INFO(typeName << "." << property.key);
                    CHECK(serialization::formatValue(written->value) == serialization::formatValue(property.value));
                }
            }
        }
    }
}

TEST_CASE("A scene of a newer format is refused, one of an older format reads", "[format]")
{
    const core::Result<scene::Scene> newer = scene::loadScene(std::format("[scene format={}]\n", scene::sceneFormatVersion + 1));
    REQUIRE_FALSE(newer);
    CHECK(newer.error().message.find("newer version of Devex") != std::string::npos);
    // A format without a step fails here: each one that was ever written stays readable.
    for (std::int64_t format = 1; format <= scene::sceneFormatVersion; ++format)
    {
        INFO("format " << format);
        CHECK(scene::loadScene(std::format("[scene format={}]\n", format)));
    }
}

TEST_CASE("The files of every version read", "[format]")
{
    if (updating())
    {
        SUCCEED("Recording");
        return;
    }
    const auto read = [](const fs::path& file) -> core::Result<void> {
        const core::Result<std::string> text = core::readTextFile(file);
        if (!text)
        {
            return std::unexpected(text.error());
        }
        const auto outcome = [](const auto& parsed) -> core::Result<void> {
            if (!parsed)
            {
                return std::unexpected(parsed.error());
            }
            return {};
        };
        const std::string extension = core::toUtf8(file.extension());
        if (extension == ".dvxmat")
        {
            return outcome(asset::parseMaterialFile(*text));
        }
        if (extension == ".dvxanimator")
        {
            return outcome(asset::parseAnimatorFile(*text));
        }
        if (extension == ".dvxcurve")
        {
            return outcome(asset::parseCurveFile(*text));
        }
        if (extension == ".dvxframes")
        {
            return outcome(asset::parseSpriteFramesFile(*text));
        }
        if (extension == ".dvxtileset")
        {
            return outcome(asset::parseTilesetFile(*text));
        }
        if (extension == ".dvxtheme")
        {
            return outcome(asset::parseThemeFile(*text));
        }
        if (extension == ".dvxshadergraph")
        {
            return outcome(asset::parseShaderGraphFile(*text));
        }
        if (extension == ".dvxmeta")
        {
            return outcome(asset::parseMetaFile(*text));
        }
        return core::makeError(core::ErrorCode::Unsupported, "this test does not know this kind of file: add its reader");
    };

    for (const fs::path& folder : versionFolders())
    {
        std::set<std::string> kinds;
        for (const fs::directory_entry& entry : fs::directory_iterator(folder))
        {
            const fs::path& file = entry.path();
            const std::string extension = core::toUtf8(file.extension());
            kinds.insert(extension);
            if (extension == ".dvxscene" || extension == ".dvxterrain" || extension == ".dvxproj" || entry.is_directory())
            {
                continue;
            }
            INFO(core::toUtf8(file));
            const core::Result<void> result = read(file);
            if (!result)
            {
                FAIL_CHECK(result.error().message);
            }
        }
        INFO(core::toUtf8(folder));
        for (const std::string_view kind : {".dvxmat", ".dvxanimator", ".dvxcurve", ".dvxframes", ".dvxtileset", ".dvxtheme", ".dvxshadergraph", ".dvxmeta", ".dvxproj"})
        {
            CHECK(kinds.contains(std::string(kind)));
        }
    }
}

TEST_CASE("The terrain of every version reads: it is sculpted by hand and comes back from nowhere", "[format]")
{
    if (updating())
    {
        SUCCEED("Recording");
        return;
    }
    for (const fs::path& folder : versionFolders())
    {
        INFO(core::toUtf8(folder));
        const core::Result<std::vector<std::byte>> bytes = core::readBinaryFile(folder / smallTerrain);
        REQUIRE(bytes);
        const core::Result<asset::TerrainMaps> terrain = asset::decodeTerrain(*bytes);
        // A layout that changes without a reader of the former one fails here.
        REQUIRE(terrain);
        REQUIRE(terrain->resolution == smallTerrainResolution);
        REQUIRE(terrain->heights.size() == terrain->sampleCount());
        CHECK(terrain->heights[100] == 25.0f);
        CHECK(terrain->paint.front() == 7);
        CHECK(terrain->grass.back() == 9);
    }
}

TEST_CASE("The project of every version opens", "[format]")
{
    if (updating())
    {
        SUCCEED("Recording");
        return;
    }
    for (const fs::path& folder : versionFolders())
    {
        INFO(core::toUtf8(folder));
        const core::Result<asset::Project> project = asset::loadProject(folder / "project.dvxproj");
        REQUIRE(project);
        CHECK(project->name == "Frozen");
        CHECK(asset::projectAge(*project) != asset::ProjectAge::Newer);
    }
}

TEST_CASE("An older project is copied, then says this version", "[format]")
{
    const TemporaryProject folder("[project format=1 name=\"Game\"]\n");
    folder.add("assets/level.dvxscene", "[scene format=1]\n");
    folder.add("assets/level.dvxscene.dvxmeta", "meta");
    folder.add("assets/hills.dvxterrain", "sculpted");
    folder.add("assets/level.dvxnavmesh", "baked");
    folder.add("code/Player.cs", "code");
    folder.add("code/bin/built.dvxmat", "built");
    folder.add(".devex/cache/x.dvxmat", "cache");

    core::Result<asset::Project> project = asset::loadProject(folder.file());
    REQUIRE(project);
    CHECK(project->engineVersion.empty());
    CHECK(asset::projectAge(*project) == asset::ProjectAge::Older);

    const core::Result<fs::path> backup = asset::upgradeProject(*project);
    REQUIRE(backup);
    CHECK(backup->parent_path().filename() == "backups");
    CHECK(backup->filename() == "older");
    CHECK(fs::exists(*backup / "Game.dvxproj"));
    CHECK(fs::exists(*backup / "assets/level.dvxscene"));
    CHECK(fs::exists(*backup / "assets/level.dvxscene.dvxmeta"));
    CHECK(fs::exists(*backup / "assets/hills.dvxterrain"));
    CHECK_FALSE(fs::exists(*backup / "assets/level.dvxnavmesh"));
    CHECK_FALSE(fs::exists(*backup / "code"));
    CHECK_FALSE(fs::exists(*backup / ".devex"));

    const core::Result<std::string> copied = core::readTextFile(*backup / "Game.dvxproj");
    REQUIRE(copied);
    CHECK(copied->find("engine=") == std::string::npos);

    const core::Result<asset::Project> upgraded = asset::loadProject(folder.file());
    REQUIRE(upgraded);
    CHECK(upgraded->engineVersion == core::version());
    CHECK(asset::projectAge(*upgraded) == asset::ProjectAge::Current);

    // A copy never replaces an earlier one.
    const core::Result<fs::path> second = asset::backUpProject(*project);
    REQUIRE(second);
    CHECK(second->filename() != backup->filename());
}

TEST_CASE("Saving the project of a newer version does not make it older", "[format]")
{
    const TemporaryProject folder("[project format=1 engine=\"99.0.0\" name=\"Game\"]\n");
    core::Result<asset::Project> project = asset::loadProject(folder.file());
    REQUIRE(project);
    CHECK(asset::projectAge(*project) == asset::ProjectAge::Newer);
    REQUIRE(asset::saveProject(*project));

    const core::Result<asset::Project> saved = asset::loadProject(folder.file());
    REQUIRE(saved);
    CHECK(saved->engineVersion == "99.0.0");
}

TEST_CASE("A new project says this version", "[format]")
{
    const TemporaryProject folder("[project format=1 name=\"Game\"]\n");
    asset::Project project;
    project.file = folder.file();
    project.name = "Game";
    REQUIRE(asset::saveProject(project));
    const core::Result<asset::Project> saved = asset::loadProject(folder.file());
    REQUIRE(saved);
    CHECK(saved->engineVersion == core::version());
}
