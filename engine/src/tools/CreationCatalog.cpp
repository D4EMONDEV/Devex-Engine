#include "CreationCatalog.hpp"

#include <devex/asset/AssetId.hpp>
#include <devex/core/File.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/Physics2DComponents.hpp>
#include <devex/scene/PhysicsComponents.hpp>
#include <devex/serialization/Text.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

namespace devex::tools::detail {
namespace {

using Category = CreationCategory;

// A component of the engine as the window shows it.
struct ComponentInfo
{
    std::string_view type;
    std::string_view name;
    Category category;
    std::string_view description;
    // What it needs beside it to do anything, added with it.
    std::array<std::string_view, 3> needs{};
};

// In the order the window lists them.
constexpr std::array engineComponents{
    ComponentInfo{"MeshRenderer", "Mesh", Category::ThreeD, "Draws a mesh with its materials, and casts its shadows."},
    ComponentInfo{"SkinnedMeshRenderer", "Skinned mesh", Category::ThreeD,
                  "Draws a mesh bent by the bones of a skeleton, as characters are."},
    ComponentInfo{"Camera", "Camera", Category::ThreeD,
                  "The view the game is drawn from; the primary camera of the scene is the one the game uses."},
    ComponentInfo{"DirectionalLight", "Directional light", Category::ThreeD,
                  "Light from far away in one direction, as the sun gives, with its shadows."},
    ComponentInfo{"PointLight", "Point light", Category::ThreeD, "Light from a point in every direction, as a bulb gives."},
    ComponentInfo{"SpotLight", "Spot light", Category::ThreeD, "Light from a point within a cone, as a torch gives."},
    ComponentInfo{"Environment", "Environment", Category::ThreeD,
                  "The sky, the light it gives the scene, and how the image is exposed and finished."},
    ComponentInfo{"SpriteRenderer", "Sprite", Category::TwoD, "Draws a sprite of a texture, flat in the XY plane."},
    ComponentInfo{"SpriteAnimator", "Sprite animator", Category::TwoD,
                  "Plays the animations of a sprite frames file on the sprite of its entity.", {"SpriteRenderer"}},
    ComponentInfo{"Tilemap", "Tilemap", Category::TwoD, "A grid of cells painted with the tiles of a tileset."},
    ComponentInfo{"Canvas", "Canvas", Category::Interface,
                  "The root of an interface, drawn over the game; its elements are placed inside it."},
    ComponentInfo{"UiRect", "Element", Category::Interface,
                  "A rectangle of an interface, placed by its anchors and offsets in its parent."},
    ComponentInfo{"UiImage", "Image", Category::Interface,
                  "Fills its rectangle with a colour or a texture, its corners rounded or in nine parts."},
    ComponentInfo{"UiText", "Text", Category::Interface,
                  "Letters of a font that stay sharp at any size, with rich text and wrapping."},
    ComponentInfo{"UiButton", "Button", Category::Interface,
                  "Answers the pointer, the keyboard and the pad with the action it carries.", {"UiImage"}},
    ComponentInfo{"UiInput", "Text field", Category::Interface, "A field the player types into, with its selection.",
                  {"UiImage", "UiText"}},
    ComponentInfo{"UiSlider", "Slider", Category::Interface, "A value dragged between two bounds.", {"UiImage"}},
    ComponentInfo{"UiToggle", "Toggle", Category::Interface, "A box ticked and unticked.", {"UiImage"}},
    ComponentInfo{"UiDropdown", "Dropdown", Category::Interface, "A button that opens the list of its options.",
                  {"UiImage", "UiText"}},
    ComponentInfo{"UiLayout", "Layout", Category::Interface, "Places its children in a row, a column or a grid."},
    ComponentInfo{"UiScroll", "Scroll", Category::Interface,
                  "Moves what it holds with the wheel and a scrollbar, cut to its rectangle."},
    ComponentInfo{"UiBinding", "Binding", Category::Interface,
                  "Writes the value of a field of a component into its text, every frame.", {"UiText"}},
    ComponentInfo{"UiPopup", "Popup", Category::Interface, "Stands over the rest while open: a menu, or a modal.",
                  {"UiImage"}},
    ComponentInfo{"UiContextMenu", "Context menu", Category::Interface,
                  "Opens a popup menu under the pointer on a right click."},
    ComponentInfo{"UiTooltip", "Tooltip", Category::Interface, "A line of help once the pointer rests on the element."},
    ComponentInfo{"UiSplitter", "Splitter", Category::Interface,
                  "Shares its room between two children, with a bar the pointer drags."},
    ComponentInfo{"UiFoldout", "Foldout", Category::Interface, "Shows or hides an element, with an arrow that says which."},
    ComponentInfo{"UiVirtualList", "Virtual list", Category::Interface,
                  "A long list with entities only for the rows in view."},
    ComponentInfo{"UiTable", "Table", Category::Interface, "Lines its rows up in columns that resize and sort."},
    ComponentInfo{"UiTableRow", "Table row", Category::Interface, "A row of the table above it."},
    ComponentInfo{"UiDragSource", "Drag source", Category::Interface,
                  "Lets the pointer carry the element to a drop target."},
    ComponentInfo{"UiDropTarget", "Drop target", Category::Interface, "Takes what is dropped on it, when it accepts its type."},
    ComponentInfo{"UiNumberField", "Number field", Category::Interface,
                  "A number dragged sideways, or typed once clicked, as in an inspector.", {"UiImage", "UiText", "UiInput"}},
    ComponentInfo{"UiColorPicker", "Color picker", Category::Interface,
                  "Chooses a colour in a square of saturation and brightness, with bars of hue and opacity."},
    ComponentInfo{"UiPlot", "Plot", Category::Interface,
                  "Draws a series of values as a line or as bars: a graph, a curve, the wave of a sound."},
    ComponentInfo{"RigidBody", "Rigid body", Category::Physics, "Moved by the physics: gravity, forces and collisions."},
    ComponentInfo{"BoxCollider", "Box collider", Category::Physics, "A box the physics collides with."},
    ComponentInfo{"SphereCollider", "Sphere collider", Category::Physics, "A sphere the physics collides with."},
    ComponentInfo{"CapsuleCollider", "Capsule collider", Category::Physics, "A capsule the physics collides with."},
    ComponentInfo{"CylinderCollider", "Cylinder collider", Category::Physics, "A cylinder the physics collides with."},
    ComponentInfo{"MeshCollider", "Mesh collider", Category::Physics,
                  "The triangles of the mesh of its entity, for the physics to collide with.", {"MeshRenderer"}},
    ComponentInfo{"CharacterController", "Character", Category::Physics,
                  "Walks, climbs steps and slides along walls, moved by its script rather than by forces."},
    ComponentInfo{"RigidBody2D", "Rigid body 2D", Category::Physics2D,
                  "Moved by the physics of the plane: gravity, forces and collisions."},
    ComponentInfo{"BoxCollider2D", "Box collider 2D", Category::Physics2D, "A rectangle the physics of the plane collides with."},
    ComponentInfo{"CircleCollider2D", "Circle collider 2D", Category::Physics2D, "A circle the physics of the plane collides with."},
    ComponentInfo{"CapsuleCollider2D", "Capsule collider 2D", Category::Physics2D,
                  "A capsule the physics of the plane collides with."},
    ComponentInfo{"PolygonCollider2D", "Polygon collider 2D", Category::Physics2D,
                  "A convex polygon the physics of the plane collides with."},
    ComponentInfo{"TilemapCollider2D", "Tilemap collider", Category::Physics2D,
                  "The solid tiles of the tilemap of its entity, for the physics of the plane.", {"Tilemap"}},
    ComponentInfo{"CharacterController2D", "Character 2D", Category::Physics2D,
                  "Runs, jumps and lands on slopes and platforms, moved by its script."},
    ComponentInfo{"AudioSource", "Audio source", Category::Audio, "Plays a clip, from its place in the world or everywhere."},
    ComponentInfo{"AudioListener", "Audio listener", Category::Audio, "Where the game hears from."},
    ComponentInfo{"Animator", "Animator", Category::Animation,
                  "Plays a state machine of animations, on a skeleton or on a sprite."},
    ComponentInfo{"Tweener", "Tweener", Category::Animation,
                  "Moves a field of a component from one value to another over time."},
    ComponentInfo{"ParticleEmitter", "Particles", Category::Effects, "Emits particles: fire, smoke, sparks, rain."},
    ComponentInfo{"TrailRenderer", "Trail", Category::Effects, "Leaves a ribbon behind its entity as it moves."},
    ComponentInfo{"NavMeshSurface", "Navigation surface", Category::Navigation,
                  "Where agents walk, baked from the static colliders around it."},
    ComponentInfo{"NavMeshAgent", "Navigation agent", Category::Navigation,
                  "Walks to a destination along the navigation mesh, around the other agents."},
    ComponentInfo{"NavMeshObstacle", "Navigation obstacle", Category::Navigation,
                  "Cuts the navigation mesh while the game plays, for agents to walk around it."},
    ComponentInfo{"Transform", "Transform", Category::General,
                  "Where the entity stands, how it is turned and how large it is, from its parent."},
};

[[nodiscard]] const ComponentInfo* engineComponent(std::string_view type) noexcept
{
    const auto found = std::ranges::find(engineComponents, type, &ComponentInfo::type);
    return found != engineComponents.end() ? &*found : nullptr;
}

// Interfaces are placed by their rectangles, not in space.
[[nodiscard]] bool isInterface(std::string_view type) noexcept
{
    return type == "Canvas" || type.starts_with("Ui");
}

[[nodiscard]] std::string lowered(std::string_view text)
{
    std::string result(text);
    std::ranges::transform(result, result.begin(), [](char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return result;
}

void addPreset(std::vector<CreationEntry>& entries, std::string_view name, Category category, std::string_view description,
               std::vector<std::string> components,
               std::function<void(scene::Scene&, scene::Entity)> adjust = {})
{
    std::string key = "preset:";
    key += name;
    entries.push_back(CreationEntry{.key = std::move(key),
                                    .name = std::string(name),
                                    .category = category,
                                    .description = std::string(description),
                                    .components = std::move(components),
                                    .adjust = std::move(adjust)});
}

void presetMesh(scene::Scene& scene, scene::Entity entity, asset::AssetId mesh)
{
    scene.get<scene::MeshRenderer>(entity).mesh = mesh;
}

} // namespace

std::string_view categoryName(CreationCategory category) noexcept
{
    switch (category)
    {
    case Category::General:
        return "General";
    case Category::ThreeD:
        return "3D";
    case Category::TwoD:
        return "2D";
    case Category::Interface:
        return "Interface";
    case Category::Physics:
        return "Physics";
    case Category::Physics2D:
        return "Physics 2D";
    case Category::Audio:
        return "Audio";
    case Category::Animation:
        return "Animation";
    case Category::Effects:
        return "Effects";
    case Category::Navigation:
        return "Navigation";
    case Category::GameCode:
        return "Game code";
    case Category::Count:
        break;
    }
    return "General";
}

std::vector<CreationEntry> creationCatalog(const scene::ComponentRegistry& registry)
{
    std::vector<CreationEntry> entries;
    // What several components make together, or one with other values than its own.
    addPreset(entries, "Empty", Category::General, "An entity with a place in the world and nothing else, to group others.",
              {"Transform"});
    addPreset(entries, "Cube", Category::ThreeD, "A cube of one meter, with the default material.", {"Transform", "MeshRenderer"},
              [](scene::Scene& scene, scene::Entity entity) { presetMesh(scene, entity, asset::builtin::cubeMesh); });
    addPreset(entries, "Sphere", Category::ThreeD, "A sphere of one meter, with the default material.",
              {"Transform", "MeshRenderer"},
              [](scene::Scene& scene, scene::Entity entity) { presetMesh(scene, entity, asset::builtin::sphereMesh); });
    addPreset(entries, "Plane", Category::ThreeD, "A flat square to stand on, with the default material.",
              {"Transform", "MeshRenderer"},
              [](scene::Scene& scene, scene::Entity entity) { presetMesh(scene, entity, asset::builtin::planeMesh); });
    addPreset(entries, "2D camera", Category::TwoD,
              "An orthographic camera in front of the XY plane that keeps the colours of sprites as drawn.",
              {"Transform", "Camera"}, [](scene::Scene& scene, scene::Entity entity) {
                  scene.get<scene::Transform>(entity).position.z = 10.0f;
                  scene.get<scene::Camera>(entity) = scene::Camera{.projection = scene::Projection::Orthographic,
                                                                   .primary = false,
                                                                   .tonemapper = scene::Tonemapper::None,
                                                                   .antialiasing = scene::Antialiasing::None,
                                                                   .ambientOcclusion = 0.0f,
                                                                   .bloom = 0.0f};
              });
    addPreset(entries, "Static box", Category::Physics, "A cube that stays where it is and that the physics collides with.",
              {"Transform", "MeshRenderer", "BoxCollider"},
              [](scene::Scene& scene, scene::Entity entity) { presetMesh(scene, entity, asset::builtin::cubeMesh); });
    addPreset(entries, "Rigid box", Category::Physics, "A cube that falls, rolls and pushes, moved by the physics.",
              {"Transform", "MeshRenderer", "RigidBody", "BoxCollider"},
              [](scene::Scene& scene, scene::Entity entity) { presetMesh(scene, entity, asset::builtin::cubeMesh); });
    addPreset(entries, "Rigid sphere", Category::Physics, "A ball that falls and rolls, moved by the physics.",
              {"Transform", "MeshRenderer", "RigidBody", "SphereCollider"},
              [](scene::Scene& scene, scene::Entity entity) { presetMesh(scene, entity, asset::builtin::sphereMesh); });
    addPreset(entries, "Trigger zone", Category::Physics,
              "A box that bodies go through, which tells the game when they enter and leave it.",
              {"Transform", "BoxCollider"}, [](scene::Scene& scene, scene::Entity entity) {
                  scene.get<scene::BoxCollider>(entity) = scene::BoxCollider{.size = {2.0f, 2.0f, 2.0f}, .trigger = true};
              });
    addPreset(entries, "Rigid box 2D", Category::Physics2D, "A rectangle that falls and pushes, moved by the physics of the plane.",
              {"Transform", "RigidBody2D", "BoxCollider2D"});
    addPreset(entries, "Rigid circle 2D", Category::Physics2D, "A circle that falls and rolls, moved by the physics of the plane.",
              {"Transform", "RigidBody2D", "CircleCollider2D"});
    addPreset(entries, "Trigger zone 2D", Category::Physics2D,
              "A rectangle that bodies go through, which tells the game when they enter and leave it.",
              {"Transform", "BoxCollider2D"}, [](scene::Scene& scene, scene::Entity entity) {
                  scene.get<scene::BoxCollider2D>(entity) = scene::BoxCollider2D{.size = {2.0f, 2.0f}, .trigger = true};
              });

    // The components, each an entity of its own as each type is a node in Godot.
    for (const ComponentInfo& info : engineComponents)
    {
        if (registry.find(info.type) == nullptr)
        {
            continue;
        }
        CreationEntry entry{.key = "component:" + std::string(info.type),
                            .name = std::string(info.name),
                            .component = std::string(info.type),
                            .category = info.category,
                            .description = std::string(info.description)};
        const bool placed = info.type != "Environment" && info.type != "Canvas" && !isInterface(info.type);
        if (placed && info.type != "Transform")
        {
            entry.components.emplace_back("Transform");
        }
        if (isInterface(info.type) && info.type != "Canvas" && info.type != "UiRect")
        {
            entry.components.emplace_back("UiRect");
        }
        for (const std::string_view need : info.needs)
        {
            if (!need.empty())
            {
                entry.components.emplace_back(need);
            }
        }
        entry.components.push_back(entry.component);
        if (info.type == "DirectionalLight")
        {
            entry.adjust = [](scene::Scene& scene, scene::Entity entity) {
                scene.get<scene::Transform>(entity).rotation = math::angleAxis(math::radians(-50.0f), math::Vec3{1.0f, 0.0f, 0.0f});
            };
        }
        else if (info.type == "SpotLight")
        {
            entry.adjust = [](scene::Scene& scene, scene::Entity entity) {
                scene.get<scene::Transform>(entity).rotation = math::angleAxis(math::radians(-90.0f), math::Vec3{1.0f, 0.0f, 0.0f});
            };
        }
        else if (info.type == "Camera")
        {
            // A camera added to a scene does not take over the one the game already uses.
            entry.adjust = [](scene::Scene& scene, scene::Entity entity) { scene.get<scene::Camera>(entity).primary = false; };
        }
        else if (info.type == "MeshRenderer")
        {
            entry.adjust = [](scene::Scene& scene, scene::Entity entity) { presetMesh(scene, entity, asset::builtin::cubeMesh); };
        }
        entries.push_back(std::move(entry));
    }

    // The components of the game, written in its code.
    for (const scene::ComponentType& type : registry.types())
    {
        if (engineComponent(type.name()) != nullptr)
        {
            continue;
        }
        entries.push_back(CreationEntry{.key = "component:" + std::string(type.name()),
                                        .name = std::string(type.name()),
                                        .component = std::string(type.name()),
                                        .category = Category::GameCode,
                                        .description = "A component of the game's code.",
                                        .components = {"Transform", std::string(type.name())}});
    }
    return entries;
}

int matchScore(const CreationEntry& entry, std::string_view search)
{
    const std::string name = lowered(entry.name);
    const std::string component = lowered(entry.component);
    const std::string category = lowered(categoryName(entry.category));
    const std::string description = lowered(entry.description);
    int total = 0;
    std::size_t start = 0;
    bool any = false;
    const std::string wanted = lowered(search);
    while (start < wanted.size())
    {
        const std::size_t end = std::min(wanted.find(' ', start), wanted.size());
        const std::string_view word = std::string_view(wanted).substr(start, end - start);
        start = end + 1;
        if (word.empty())
        {
            continue;
        }
        any = true;
        // The start of the name first, then the start of one of its words, then anywhere in it.
        int score = 0;
        if (name.starts_with(word))
        {
            score = 400 - static_cast<int>(std::min<std::size_t>(name.size() - word.size(), 99));
        }
        else if (name.find(" " + std::string(word)) != std::string::npos)
        {
            score = 300;
        }
        else if (name.find(word) != std::string::npos)
        {
            score = 200;
        }
        else if (component.find(word) != std::string::npos)
        {
            score = 150;
        }
        else if (category.find(word) != std::string::npos)
        {
            score = 60;
        }
        else if (description.find(word) != std::string::npos)
        {
            score = 40;
        }
        if (score == 0)
        {
            return 0;
        }
        total += score;
    }
    return any ? total : 1;
}

void buildEntry(const CreationEntry& entry, const scene::ComponentRegistry& registry, scene::Scene& scene,
                scene::Entity entity)
{
    if (std::ranges::find(entry.components, "Transform") == entry.components.end())
    {
        scene.remove<scene::Transform>(entity);
    }
    for (const std::string& name : entry.components)
    {
        if (const scene::ComponentType* const type = registry.find(name))
        {
            static_cast<void>(type->emplace(scene, entity));
        }
    }
    if (entry.adjust)
    {
        entry.adjust(scene, entity);
    }
}

CreationMemory loadCreationMemory(const std::filesystem::path& file)
{
    CreationMemory memory;
    const core::Result<std::string> text = core::readTextFile(file);
    if (!text)
    {
        return memory;
    }
    const core::Result<serialization::TextDocument> document = serialization::parseText(*text);
    if (!document)
    {
        return memory;
    }
    for (const serialization::TextSection& section : document->sections)
    {
        const serialization::TextValue* const value = section.findAttribute("entry");
        const std::string* const key = value != nullptr ? serialization::asString(*value) : nullptr;
        if (key == nullptr)
        {
            continue;
        }
        if (section.type == "favorite")
        {
            memory.favorites.push_back(*key);
        }
        else if (section.type == "recent" && memory.recent.size() < maxRecentCreations)
        {
            memory.recent.push_back(*key);
        }
    }
    return memory;
}

core::Result<void> saveCreationMemory(const CreationMemory& memory, const std::filesystem::path& file)
{
    serialization::TextDocument document;
    document.sections.push_back({.type = "creation", .attributes = {{"format", serialization::TextValue(std::int64_t{1})}}});
    for (const std::string& key : memory.favorites)
    {
        document.sections.push_back({.type = "favorite", .attributes = {{"entry", serialization::TextValue(key)}}});
    }
    for (const std::string& key : memory.recent)
    {
        document.sections.push_back({.type = "recent", .attributes = {{"entry", serialization::TextValue(key)}}});
    }
    return core::writeTextFile(file, serialization::writeText(document));
}

void rememberCreation(CreationMemory& memory, std::string_view key)
{
    std::erase(memory.recent, key);
    memory.recent.insert(memory.recent.begin(), std::string(key));
    if (memory.recent.size() > maxRecentCreations)
    {
        memory.recent.resize(maxRecentCreations);
    }
}

void toggleFavorite(CreationMemory& memory, std::string_view key)
{
    if (std::erase(memory.favorites, key) == 0)
    {
        memory.favorites.emplace_back(key);
    }
}

} // namespace devex::tools::detail
