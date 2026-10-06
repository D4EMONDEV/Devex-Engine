// The inspector of entities, made with the interface of the engine as Godot's Inspector is made with
// its controls: a card per component, folded from its header, and a row per property whose control
// fits its value. Numbers are dragged sideways or typed, the axes of vectors show in their colours,
// colours open a picker of the engine, and the fields of assets and entities take what FileSystem and
// the scene tree drop on them. Several entities show the components they share, with a dash where
// their values differ; a change goes to all of them, as one step to undo.
#include "InspectorUi.hpp"

#include <devex/asset/Project.hpp>
#include <devex/core/Path.hpp>
#include <devex/particles/ParticleWorld.hpp>
#include <devex/scene/ComponentRegistry.hpp>
#include <devex/scene/Components.hpp>
#include <devex/scene/FieldValue.hpp>
#include <devex/scene/NavigationComponents.hpp>
#include <devex/scene/Prefab.hpp>
#include <devex/scene/SceneSerializer.hpp>
#include <devex/scene/UiComponents.hpp>
#include <devex/tools/SceneCommands.hpp>
#include <devex/ui/Color.hpp>
#include <devex/ui/Theme.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>

namespace devex::tools::detail {

using reflection::ValueKind;
using scene::Entity;
using scene::UiRect;
using namespace rects;
using Button = PanelButton;

namespace {

// The image the panel is drawn into, among the interface surfaces of the editor.
constexpr std::uint32_t inspectorSurface = 6;
constexpr std::array<std::string_view, 4> axisLetters{"x", "y", "z", "w"};

[[nodiscard]] void* fieldAddress(const scene::Scene& edited, Entity entity, const PropertyRow& row)
{
    const void* const component = entity.isValid() ? row.type->find(edited, entity) : nullptr;
    return component != nullptr ? row.field->address(const_cast<void*>(component)) : nullptr;
}

// The value the row edits: the field, or its element.
[[nodiscard]] void* valueAddress(const scene::Scene& edited, Entity entity, const PropertyRow& row)
{
    void* const address = fieldAddress(edited, entity, row);
    if (address == nullptr || !row.element)
    {
        return address;
    }
    return *row.element < row.field->list->size(address) ? row.field->list->element(address, *row.element) : nullptr;
}

[[nodiscard]] std::size_t numberCount(const reflection::FieldInfo& field) noexcept
{
    switch (field.kind)
    {
    case ValueKind::Vec2:
        return 2;
    case ValueKind::Vec3:
    case ValueKind::Quat:
        return 3;
    case ValueKind::Vec4:
        return 4;
    default:
        return 1;
    }
}

// The numbers a value shows: itself, the axes of a vector, or the angles of a rotation in degrees.
[[nodiscard]] std::array<float, 4> numbersOf(const reflection::FieldInfo& field, const void* address)
{
    switch (field.kind)
    {
    case ValueKind::Int32:
        return {static_cast<float>(*static_cast<const std::int32_t*>(address)), 0.0f, 0.0f, 0.0f};
    case ValueKind::UInt32:
        return {static_cast<float>(*static_cast<const std::uint32_t*>(address)), 0.0f, 0.0f, 0.0f};
    case ValueKind::Float: {
        const float value = *static_cast<const float*>(address);
        return {field.angle ? math::degrees(value) : value, 0.0f, 0.0f, 0.0f};
    }
    case ValueKind::Vec2: {
        const auto& value = *static_cast<const math::Vec2*>(address);
        return {value.x, value.y, 0.0f, 0.0f};
    }
    case ValueKind::Vec3: {
        const auto& value = *static_cast<const math::Vec3*>(address);
        return {value.x, value.y, value.z, 0.0f};
    }
    case ValueKind::Vec4: {
        const auto& value = *static_cast<const math::Vec4*>(address);
        return {value.x, value.y, value.z, value.w};
    }
    case ValueKind::Quat: {
        const math::Vec3 degrees = math::degrees(math::eulerAngles(*static_cast<const math::Quat*>(address)));
        return {degrees.x, degrees.y, degrees.z, 0.0f};
    }
    default:
        return {};
    }
}

// Writes one of the numbers of a value, keeping the others.
void writeNumber(const reflection::FieldInfo& field, void* address, std::size_t index, float value)
{
    const auto axis = static_cast<math::Vec4::length_type>(index);
    switch (field.kind)
    {
    case ValueKind::Int32:
        *static_cast<std::int32_t*>(address) = static_cast<std::int32_t>(
            std::clamp<long long>(std::llround(value), std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
        break;
    case ValueKind::UInt32:
        *static_cast<std::uint32_t*>(address) =
            static_cast<std::uint32_t>(std::clamp<long long>(std::llround(value), 0, std::numeric_limits<std::uint32_t>::max()));
        break;
    case ValueKind::Float:
        *static_cast<float*>(address) = field.angle ? math::radians(value) : value;
        break;
    case ValueKind::Vec2:
        (*static_cast<math::Vec2*>(address))[axis] = value;
        break;
    case ValueKind::Vec3:
        (*static_cast<math::Vec3*>(address))[axis] = value;
        break;
    case ValueKind::Vec4:
        (*static_cast<math::Vec4*>(address))[axis] = value;
        break;
    case ValueKind::Quat: {
        auto& rotation = *static_cast<math::Quat*>(address);
        math::Vec3 degrees = math::degrees(math::eulerAngles(rotation));
        degrees[axis] = value;
        rotation = math::quatFromEulerAngles(math::radians(degrees));
        break;
    }
    default:
        break;
    }
}

[[nodiscard]] math::Vec4 colorOf(const reflection::FieldInfo& field, const void* address)
{
    if (field.kind == ValueKind::Vec3)
    {
        const auto& color = *static_cast<const math::Vec3*>(address);
        return math::Vec4{color.x, color.y, color.z, 1.0f};
    }
    return *static_cast<const math::Vec4*>(address);
}

void writeColor(const reflection::FieldInfo& field, void* address, math::Vec4 color)
{
    if (field.kind == ValueKind::Vec3)
    {
        *static_cast<math::Vec3*>(address) = math::Vec3{color.x, color.y, color.z};
    }
    else
    {
        *static_cast<math::Vec4*>(address) = color;
    }
}

// How the numbers of a field are dragged and written.
[[nodiscard]] scene::UiNumberField numberSettings(const reflection::FieldInfo& field)
{
    switch (field.kind)
    {
    case ValueKind::Int32:
        return {.step = 1.0f, .dragSpeed = 0.1f, .decimals = 0};
    case ValueKind::UInt32:
        return {.minValue = 0.0f, .maxValue = 4294967295.0f, .step = 1.0f, .dragSpeed = 0.1f, .decimals = 0};
    case ValueKind::Quat:
        return {.dragSpeed = 0.5f, .decimals = 1, .format = "{}°"};
    case ValueKind::Float:
        if (field.angle)
        {
            return {.dragSpeed = 0.5f, .decimals = 1, .format = "{}°"};
        }
        return {.dragSpeed = 0.01f, .decimals = 3};
    default:
        return {.dragSpeed = 0.01f, .decimals = 3};
    }
}

[[nodiscard]] bool isChoice(const reflection::FieldInfo& field) noexcept
{
    return field.kind == ValueKind::Enum || field.kind == ValueKind::AssetId || field.kind == ValueKind::Entity ||
           (field.kind == ValueKind::UInt32 && (field.physicsLayer || field.audioGroup)) ||
           (field.kind == ValueKind::String && field.sortingLayer);
}

// The types of assets an asset field takes from FileSystem: its own, or any.
[[nodiscard]] std::vector<std::string> acceptedAssets(const reflection::FieldInfo& field)
{
    std::vector<std::string> accepted;
    if (!field.assetType.empty())
    {
        accepted.push_back(std::format("asset:{}", field.assetType));
        return accepted;
    }
    for (std::uint8_t value = static_cast<std::uint8_t>(asset::AssetType::Mesh); asset::isAssetType(value); ++value)
    {
        accepted.push_back(std::format("asset:{}", asset::toString(static_cast<asset::AssetType>(value))));
    }
    return accepted;
}

// Whether the entity has the component from its prefab, which cannot remove it.
[[nodiscard]] bool hasFromPrefab(const scene::Scene& scene, Entity entity, const scene::ComponentType& type)
{
    const Entity instance = scene::owningPrefabInstance(scene, entity);
    if (!instance.isValid() || !scene.has<scene::PrefabEntity>(entity) || !scene.get<scene::PrefabInstance>(instance).resolved)
    {
        return false;
    }
    const std::shared_ptr<const scene::Scene> base = scene::prefabBase(scene.get<scene::PrefabInstance>(instance).prefab, scene.uuid(instance));
    const Entity original = base != nullptr ? base->findEntity(scene.uuid(entity)) : Entity{};
    return original.isValid() && type.find(*base, original) != nullptr;
}

} // namespace

InspectorUi::InspectorUi()
    : FormUi(inspectorSurface)
{
}

template <typename Write>
void InspectorUi::each(const scene::Scene& edited, std::span<const Entity> inspected, const PropertyRow& row, const Write& write)
{
    for (const Entity entity : inspected)
    {
        if (void* const address = valueAddress(edited, entity, row))
        {
            write(address, entity == inspected.back());
        }
    }
}

void InspectorUi::build(ToolsState& state, EditorUiKit& kit)
{
    built = true;
    const Entity root = add({}, "Inspector", whole());
    buildForm(root, whole());

    componentMenu = menu("Component menu", font * 14.0f);
    removeComponent = menuItem(kit, componentMenu, Icon::Trash, "Remove Component");
    revertMenu = menu("Revert menu", font * 15.0f);
    revert = menuItem(kit, revertMenu, Icon::Undo, "Revert to Prefab Value");
    buildColorPopup();

    // Entities come from the scene tree, and assets from FileSystem, named by their type so that a
    // field lights up only for what it takes.
    panel.setKeyboardNavigation(false);
    panel.setDragIn([](const EditorDrag& payload) -> std::optional<std::pair<std::string, std::string>> {
        if (payload.is(entityPayload, 16))
        {
            std::array<std::uint8_t, 16> bytes{};
            std::memcpy(bytes.data(), payload.payload.data(), bytes.size());
            return std::pair{std::string("entity"), uuidFromBytes(bytes).toString()};
        }
        if (payload.is(assetPayload, sizeof(AssetPayload)))
        {
            AssetPayload asset;
            std::memcpy(&asset, payload.payload.data(), sizeof(asset));
            return std::pair{std::format("asset:{}", asset::toString(asset.type)), uuidFromBytes(asset.uuid).toString()};
        }
        return std::nullopt;
    });

    // The previews of the pages: the textures of the project, and the sprites cut from them.
    kit.setAssetImages(
        [&state](asset::AssetId id) { return state.textures ? state.textures(id) : render::TextureHandle{}; },
        [&state](asset::AssetId id) {
            const math::Extent2D size = state.textureSizes ? state.textureSizes(id) : math::Extent2D{};
            return math::Vec2{static_cast<float>(size.width), static_cast<float>(size.height)};
        },
        [&state](asset::AssetId id) -> std::optional<ui::SpriteImage> {
            // Only a sprite is read as one, which the database tells.
            const asset::AssetInfo* const info = state.database != nullptr ? state.database->find(id) : nullptr;
            if (info == nullptr || info->type != asset::AssetType::Sprite || !state.sprites)
            {
                return std::nullopt;
            }
            const std::shared_ptr<const asset::SpriteData> sprite = state.sprites(id);
            if (sprite == nullptr || !state.textures)
            {
                return ui::SpriteImage{};
            }
            return ui::SpriteImage{.texture = state.textures(sprite->texture),
                                   .uv = sprite->uvRect(),
                                   .size = math::Vec2{static_cast<float>(sprite->width), static_cast<float>(sprite->height)}};
        });
}

std::string InspectorUi::signatureOf(const scene::Scene& edited, std::span<const Entity> inspected) const
{
    const scene::ComponentRegistry& registry = scene::componentRegistry();
    std::string text = std::format("{}|{}|{}|", static_cast<const void*>(&edited), font, registry.generation());
    for (const Entity entity : inspected)
    {
        text += edited.uuid(entity).toString();
        text += ',';
    }
    const Entity active = inspected.back();
    const bool single = inspected.size() == 1;
    for (const scene::ComponentType& type : registry.types())
    {
        if (!std::ranges::all_of(inspected, [&](Entity entity) { return type.find(edited, entity) != nullptr; }))
        {
            continue;
        }
        text += type.name();
        text += ':';
        // The lists of one entity show a row per element.
        const void* const component = single ? type.find(edited, active) : nullptr;
        for (const reflection::FieldInfo& field : type.type->fields)
        {
            if (component != nullptr && field.list != nullptr && !field.hidden && !field.runtime)
            {
                text += std::format("{},", field.list->size(field.address(component)));
            }
        }
        text += ';';
    }
    if (single)
    {
        if (const auto* const preserved = edited.tryGet<scene::PreservedComponents>(active))
        {
            text += std::format("|{}", preserved->sections.size());
        }
        if (const Entity instance = scene::owningPrefabInstance(edited, active); instance.isValid())
        {
            text += std::format("|{}{}", edited.uuid(instance).toString(), edited.get<scene::PrefabInstance>(instance).resolved ? 1 : 0);
        }
    }
    return text;
}

void InspectorUi::clear(ToolsState& state, const scene::Scene& edited)
{
    // What is under way ends as it stands, before its rows go, unless the types it edits went with
    // the code of the game.
    const bool sameTypes = scene::componentRegistry().generation() == generation;
    for (PropertyRow& row : rows)
    {
        if (sameTypes)
        {
            commit(state, edited, row);
        }
    }
    generation = scene::componentRegistry().generation();
    ui::UiWorld& world = panel.world();
    for (const Entity popup : {componentMenu, revertMenu})
    {
        world.closePopup(scene(), popup);
    }
    // What a page made outside the content goes with it.
    clearForm();
    colorRow.reset();
    revertRow.reset();
    menuSection.reset();
    naming = false;
    rows.clear();
    nameIcon = nameField = nameMark = uuidText = countText = Entity{};
    prefabIcon = prefabLead = prefabTitle = prefabTail = Entity{};
    openPrefab = revertPrefab = makeLocal = Button{};
    addComponent = Button{};
}

void InspectorUi::rebuild(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const Entity> inspected)
{
    clear(state, edited);
    buildTop(kit, edited, inspected);
    const bool shared = inspected.size() > 1;
    const Entity active = inspected.back();
    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        if (!std::ranges::all_of(inspected, [&](Entity entity) { return type.find(edited, entity) != nullptr; }))
        {
            continue;
        }
        const std::string name(type.name());
        Section& section = addComponentSection(kit, name, componentIcon(name), &type);
        int group = -1;
        for (const reflection::FieldInfo& field : type.type->fields)
        {
            if (!field.group.empty())
            {
                section.groups.push_back(field.group);
                group = static_cast<int>(section.groups.size()) - 1;
                // The header of a group, folded as the card is.
                const Entity heading = add(section.card, "Group", wide(std::round(line * 0.95f)), "group");
                scene().add<scene::UiImage>(heading);
                scene().add<scene::UiButton>(heading);
                scene().add<scene::UiFoldout>(heading);
                text(heading, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {std::min(line, 28.0f), 0.0f}, .offsetMax = {0.0f, 0.0f}},
                     field.group, "dim", true);
                section.lines.push_back(Line{.entity = heading, .group = group, .heading = true});
            }
            if (field.hidden || field.runtime)
            {
                continue;
            }
            addProperty(kit, section, type, field, std::nullopt, group, shared);
            if (!shared && field.list != nullptr)
            {
                const void* const address = field.address(type.find(edited, active));
                for (std::size_t index = 0; index < field.list->size(address); ++index)
                {
                    addProperty(kit, section, type, field, index, group, false);
                }
            }
            if (!shared && name == "UiRect" && field.name == "style")
            {
                addExtras(kit, section, group);
            }
        }
        if (!shared && (name == "ParticleEmitter" || name == "NavMeshSurface"))
        {
            addExtras(kit, section, -1);
        }
        if (!shared && name == "Tilemap")
        {
            addTilePainter(*this, kit, section);
        }
    }

    // Components whose type is not registered, such as those of game code that is not loaded.
    if (!shared)
    {
        if (const auto* const preserved = edited.tryGet<scene::PreservedComponents>(active))
        {
            for (const serialization::TextSection& kept : preserved->sections)
            {
                const serialization::TextValue* const typeValue = kept.findAttribute("type");
                const std::string* const typeName = typeValue != nullptr ? serialization::asString(*typeValue) : nullptr;
                Section& section = addSection(kit, std::format("{} (not loaded)", typeName != nullptr ? *typeName : "?"),
                                              EntityIcon{icons::Puzzle, themeColors().gameCode}, nullptr);
                section.locked = true;
                scene().get<UiRect>(section.title).style = "dim";
                tooltip(section.header, "The game code that defines this component is not loaded. It is kept in the scene and comes "
                                        "back with the code.");
            }
        }
    }

    addComponent = button(kit, content, Icon::Plus, "Add Component", "button", -1.0f, line);
    tooltip(addComponent.entity, shared ? "Adds components to the entities that lack them" : "Adds components to the entity");
    add(content, "End", wide(font * 0.5f));
}

void InspectorUi::buildTop(EditorUiKit& kit, const scene::Scene& edited, std::span<const Entity> inspected)
{
    const float iconSize = std::round(font * 1.3f);
    const Entity top = add(content, "Top", wide(std::round(line * 1.1f)));
    nameIcon = icon(kit, top, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {font * 0.2f, -iconSize * 0.5f},
                                     .offsetMax = {font * 0.2f + iconSize, iconSize * 0.5f}},
                    Icon::Box, {});
    const float left = font * 0.2f + iconSize + font * 0.5f;
    if (inspected.size() > 1)
    {
        countText = text(top, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {left, 0.0f}, .offsetMax = {0.0f, 0.0f}},
                         "", "text", true, scene::TextAlign::Left, font * 1.1f);
        const Entity hint = text(content, wide(std::round(font * 2.8f)), "A dash marks values that differ; changes apply to all of them.",
                                 "dim");
        scene().get<scene::UiText>(hint).wrap = true;
        return;
    }
    nameField = field(top, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {left, 1.0f}, .offsetMax = {0.0f, -1.0f}},
                      "", "Name");
    scene().add<scene::UiContextMenu>(nameField);
    nameMark = add(top, "Changed", UiRect{.anchorMin = {0.0f, 0.2f}, .anchorMax = {0.0f, 0.8f}, .offsetMin = {left - 5.0f, 0.0f},
                                          .offsetMax = {left - 3.0f, 0.0f}, .visible = false},
                   "mark");
    scene().add<scene::UiImage>(nameMark, scene::UiImage{.raycastTarget = false});
    uuidText = text(content, wide(std::round(font * 1.2f)), "", "dim", false, scene::TextAlign::Left, std::round(font * 0.85f));
    scene().get<scene::UiText>(uuidText).font = EditorUiKit::monoFont();
    scene().get<scene::UiText>(uuidText).raycastTarget = true;
    tooltip(uuidText, "The UUID of the entity, which scenes and undo steps refer to");

    // The prefab the entity comes from: its name, and what the instance can do with it.
    const Entity active = inspected.back();
    if (!scene::owningPrefabInstance(edited, active).isValid())
    {
        return;
    }
    const Entity card = add(content, "Prefab", wide(pad * 2.0f + line * 2.0f + gap * 2.0f), "card");
    scene().add<scene::UiImage>(card);
    scene().add<scene::UiLayout>(card, scene::UiLayout{.kind = scene::UiLayoutKind::Column,
                                                       .spacing = gap * 2.0f,
                                                       .padding = math::Vec4{pad},
                                                       .align = scene::TextAlign::Left});
    const Entity from = add(card, "From", wide(line));
    scene().add<scene::UiLayout>(from, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                       .spacing = font * 0.35f,
                                                       .padding = {font * 0.3f, 0.0f, 0.0f, 0.0f},
                                                       .align = scene::TextAlign::Left});
    prefabIcon = icon(kit, from, middle({iconSize, iconSize}), Icon::Package, {});
    prefabLead = text(from, middle({1.0f, line}), "", "dim");
    prefabTitle = text(from, middle({1.0f, line}), "", "text", true);
    prefabTail = text(from, middle({1.0f, line}), "", "dim");
    const Entity actions = add(card, "Actions", wide(line));
    scene().add<scene::UiLayout>(actions, scene::UiLayout{.kind = scene::UiLayoutKind::Row, .spacing = font * 0.4f, .align = scene::TextAlign::Left});
    openPrefab = button(kit, actions, Icon::ExternalLink, "Open", "button", 0.0f, line - 2.0f);
    tooltip(openPrefab.entity, "Opens the prefab in a tab; its changes reach every instance once saved");
    revertPrefab = button(kit, actions, Icon::Undo, "Revert All", "button", 0.0f, line - 2.0f);
    tooltip(revertPrefab.entity, "Reverts every override of the instance except the placement of its root; added entities stay");
    makeLocal = button(kit, actions, Icon::Unlink, "Make Local", "button", 0.0f, line - 2.0f);
    tooltip(makeLocal.entity, "Turns the instance into ordinary entities, no longer linked to the prefab");
}

Section& InspectorUi::addComponentSection(EditorUiKit& kit, std::string name, const EntityIcon& look, const scene::ComponentType* type)
{
    Section& section = addSection(kit, std::move(name), look, type);
    if (type != nullptr)
    {
        const float size = std::round(headerHeight * 0.8f);
        section.menu = toolButton(kit, section.header, Icon::Ellipsis,
                                  UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-size - 3.0f, -size * 0.5f},
                                         .offsetMax = {-3.0f, size * 0.5f}});
        scene().add<scene::UiContextMenu>(section.header, scene::UiContextMenu{.popup = scene().reference(componentMenu)});
    }
    return section;
}

void InspectorUi::addProperty(EditorUiKit& kit, Section& section, const scene::ComponentType& type, const reflection::FieldInfo& field,
                              std::optional<std::size_t> element, int group, bool shared)
{
    const ThemeColors& colors = themeColors();
    PropertyRow row{.type = &type, .field = &field, .element = element};
    const bool listHeader = field.list != nullptr && !element;
    row.row = add(section.card, "Property", wide(line));
    scene().add<scene::UiContextMenu>(row.row);
    const float indent = (group >= 0 ? font * 1.0f : font * 0.35f) + (element ? font * 1.0f : 0.0f);
    row.labelBox = add(row.row, "Label", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 1.0f}, .offsetMin = {indent, 0.0f},
                                               .offsetMax = {labelWidth, 0.0f}, .clipChildren = true});
    row.label = text(row.labelBox, whole(), element ? std::format("{}", *element) : displayName(field.name), "label");
    tooltip(row.labelBox, element ? std::format("{}.{}[{}]", type.name(), field.name, *element) : std::format("{}.{}", type.name(), field.name));
    row.mark = add(row.row, "Changed", UiRect{.anchorMin = {0.0f, 0.2f}, .anchorMax = {0.0f, 0.8f}, .offsetMin = {0.0f, 0.0f},
                                             .offsetMax = {2.0f, 0.0f}, .visible = false},
                   "mark");
    scene().add<scene::UiImage>(row.mark, scene::UiImage{.raycastTarget = false});
    const float button = line - 6.0f;
    row.editor = add(row.row, "Editor", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {labelWidth, 2.0f},
                                              .offsetMax = {element ? -button - gap * 2.0f : 0.0f, -2.0f}});
    if (element)
    {
        row.button = toolButton(kit, row.row, Icon::Minus,
                                UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-button, -button * 0.5f},
                                       .offsetMax = {0.0f, button * 0.5f}});
        tooltip(row.button.entity, "Remove this element");
    }

    if (listHeader && shared)
    {
        row.kind = ControlKind::ReadOnly;
        row.control = text(row.editor, whole(math::Vec4{font * 0.3f, 0.0f, 0.0f, 0.0f}), "One entity at a time", "dim");
    }
    else if (listHeader)
    {
        row.kind = ControlKind::ListHeader;
        row.control = text(row.editor, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {font * 0.3f, 0.0f},
                                              .offsetMax = {-button, 0.0f}},
                           "", "dim");
        row.button = toolButton(kit, row.editor, Icon::Plus,
                                UiRect{.anchorMin = {1.0f, 0.5f}, .anchorMax = {1.0f, 0.5f}, .offsetMin = {-button, -button * 0.5f},
                                       .offsetMax = {0.0f, button * 0.5f}});
        tooltip(row.button.entity, "Add an element");
    }
    else if (field.kind == ValueKind::Bool)
    {
        row.kind = ControlKind::Toggle;
        const float size = std::round(font * 1.3f);
        row.control = add(row.editor, "Toggle", UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -size * 0.5f},
                                                      .offsetMax = {size, size * 0.5f}},
                          "toggle");
        scene().add<scene::UiImage>(row.control);
        scene().add<scene::UiToggle>(row.control);
        scene().add<scene::UiButton>(row.control);
        row.detail = text(row.editor, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {size + font * 0.5f, 0.0f},
                                             .offsetMax = {0.0f, 0.0f}},
                          "", "dim");
    }
    else if (field.kind == ValueKind::Uuid)
    {
        row.kind = ControlKind::ReadOnly;
        row.control = text(row.editor, whole(math::Vec4{font * 0.3f, 0.0f, 0.0f, 0.0f}), "", "dim", false, scene::TextAlign::Left,
                           std::round(font * 0.9f));
        scene().get<scene::UiText>(row.control).font = EditorUiKit::monoFont();
    }
    else if (field.color && (field.kind == ValueKind::Vec3 || field.kind == ValueKind::Vec4))
    {
        row.kind = ControlKind::Color;
        const float hexSize = std::round(font * 0.9f);
        const float hexWidth = kit.textWidth(EditorUiKit::monoFont(), field.kind == ValueKind::Vec4 ? "#FFFFFFFF" : "#FFFFFF", hexSize) + font * 0.4f;
        row.control = add(row.editor, "Swatch", UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {0.0f, 0.0f},
                                                      .offsetMax = {-hexWidth - font * 0.3f, 0.0f}},
                          "swatch");
        scene().add<scene::UiImage>(row.control);
        scene().add<scene::UiButton>(row.control);
        tooltip(row.control, "Choose the colour");
        if (field.kind == ValueKind::Vec4)
        {
            // The opacity, as a bar along the bottom of the swatch.
            row.opacity = add(row.control, "Opacity", UiRect{.anchorMin = {0.0f, 1.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {3.0f, -4.0f},
                                                            .offsetMax = {-3.0f, -2.0f}});
            scene().add<scene::UiImage>(row.opacity, scene::UiImage{.raycastTarget = false});
        }
        row.detail = text(row.editor, UiRect{.anchorMin = {1.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}, .offsetMin = {-hexWidth, 0.0f},
                                             .offsetMax = {0.0f, 0.0f}},
                          "", "dim", false, scene::TextAlign::Right, hexSize);
        scene().get<scene::UiText>(row.detail).font = EditorUiKit::monoFont();
    }
    else if (isChoice(field))
    {
        row.kind = ControlKind::Choice;
        row.control = add(row.editor, "Choice", whole(), "dropdown");
        scene().add<scene::UiImage>(row.control);
        scene().add<scene::UiText>(row.control, scene::UiText{.text = "",
                                                              .font = EditorUiKit::regularFont(),
                                                              .size = font,
                                                              .verticalAlign = scene::TextVerticalAlign::Middle,
                                                              .wrap = false});
        scene().add<scene::UiDropdown>(row.control);
        const math::Vec4 accent = linearColor(colors.accent);
        if (field.kind == ValueKind::AssetId || field.kind == ValueKind::Entity)
        {
            scene().add<scene::UiDropTarget>(row.control,
                                             scene::UiDropTarget{.accepts = field.kind == ValueKind::Entity ? std::vector<std::string>{"entity"}
                                                                                                             : acceptedAssets(field),
                                                                 .highlightColor = math::Vec4{accent.x, accent.y, accent.z, 0.35f}});
        }
    }
    else if (field.kind == ValueKind::String)
    {
        row.kind = ControlKind::Text;
        row.control = PanelBuilder::field(row.editor, whole(), "", "");
    }
    else
    {
        row.kind = ControlKind::Numbers;
        row.count = numberCount(field);
        scene().add<scene::UiLayout>(row.editor, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                                 .spacing = font * 0.3f,
                                                                 .equalSize = true,
                                                                 .align = scene::TextAlign::Left});
        const std::array<math::Vec4, 4> axisColors{colors.axisX, colors.axisY, colors.axisZ, colors.textDim};
        const scene::UiNumberField settings = numberSettings(field);
        for (std::size_t index = 0; index < row.count; ++index)
        {
            row.numbers[index] = numberBox(row.editor, row.count > 1 ? axisLetters[index] : std::string_view{}, axisColors[index], settings);
        }
    }
    section.lines.push_back(Line{.entity = row.row, .group = group});
    rows.push_back(std::move(row));
}

void InspectorUi::addExtras(EditorUiKit& kit, Section& section, int group)
{
    const float iconSize = std::round(font * 1.1f);
    if (section.name == "UiRect")
    {
        // Under the style of an element: what the style does, or why it does nothing, and the way to
        // the theme that holds it.
        const Entity status = add(section.card, "Style status", wide(line));
        section.styleIcon = icon(kit, status, UiRect{.anchorMin = {0.0f, 0.5f}, .anchorMax = {0.0f, 0.5f}, .offsetMin = {0.0f, -iconSize * 0.5f},
                                                     .offsetMax = {iconSize, iconSize * 0.5f}},
                                 Icon::Palette, "icon_accent");
        section.styleText = text(status, UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {1.0f, 1.0f}}, "", "dim");
        scene().get<scene::UiText>(section.styleText).raycastTarget = true;
        section.styleOpen = button(kit, status, std::nullopt, "Open", "button", 0.0f, line - 4.0f);
        UiRect& open = scene().get<UiRect>(section.styleOpen.entity);
        const float width = open.offsetMax.x - open.offsetMin.x;
        open.anchorMin = open.anchorMax = {1.0f, 0.5f};
        open.offsetMin.x = -width;
        open.offsetMax.x = 0.0f;
        section.styleLine = section.lines.size();
        section.lines.push_back(Line{.entity = status, .group = group});
        return;
    }
    if (section.name == "ParticleEmitter")
    {
        // Plays the emitter again, or stops it, in the scene shown: the preview of the editor, or the game.
        const Entity controls = add(section.card, "Particles", wide(line));
        scene().add<scene::UiLayout>(controls, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                               .spacing = font * 0.4f,
                                                               .padding = {font * 0.35f, 0.0f, 0.0f, 0.0f},
                                                               .align = scene::TextAlign::Left});
        section.restart = button(kit, controls, Icon::Refresh, "Restart", "button", 0.0f, line - 4.0f);
        tooltip(section.restart.entity, "Emits again from the start of a cycle, as when the game starts");
        section.stop = button(kit, controls, Icon::Square, "Stop", "button", 0.0f, line - 4.0f);
        tooltip(section.stop.entity, "Stops emitting and clears the particles; Restart plays it again");
        section.particles = text(controls, middle({font * 9.0f, line}), "", "dim");
        section.lines.push_back(Line{.entity = controls, .group = group});
        return;
    }
    if (section.name == "NavMeshSurface")
    {
        section.navInfo = text(section.card, wide(line), "", "dim");
        section.lines.push_back(Line{.entity = section.navInfo, .group = group});
        section.navWarning = text(section.card, wide(line), "The settings changed since the bake: bake again.", "warning");
        section.navWarningLine = section.lines.size();
        section.lines.push_back(Line{.entity = section.navWarning, .group = group});
        const Entity actions = add(section.card, "Bake", wide(line));
        scene().add<scene::UiLayout>(actions, scene::UiLayout{.kind = scene::UiLayoutKind::Row,
                                                              .spacing = font * 0.4f,
                                                              .padding = {font * 0.35f, 0.0f, 0.0f, 0.0f},
                                                              .align = scene::TextAlign::Left});
        section.bake = button(kit, actions, Icon::Footprints, "Bake", "primary", 0.0f, line - 4.0f);
        tooltip(section.bake.entity, "Finds where agents fit on the colliders that stay put, and writes the navigation mesh next to the scene");
        section.clear = button(kit, actions, Icon::Close, "Clear", "button", 0.0f, line - 4.0f);
        section.lines.push_back(Line{.entity = actions, .group = group});
        section.navStatus = text(section.card, wide(std::round(line * 1.6f)), "", "dim");
        scene().get<scene::UiText>(section.navStatus).wrap = true;
        section.navStatusLine = section.lines.size();
        section.lines.push_back(Line{.entity = section.navStatus, .group = group});
    }
}

void InspectorUi::sync(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const Entity> inspected)
{
    const ThemeColors& colors = themeColors();
    const ui::UiWorld& world = panel.world();
    const Entity active = inspected.back();
    const core::Uuid uuid = edited.uuid(active);
    const bool single = inspected.size() == 1;
    const bool editing = state.playState == PlayState::Editing;
    labelWidth = std::clamp(std::round(panel.size().x * 0.38f), font * 5.5f, font * 13.0f);

    const EntityIcon look = entityIcon(edited, active);
    scene::UiImage& glyph = scene().get<scene::UiImage>(nameIcon);
    glyph.texture = kit.icon(iconOf(look.icon));
    glyph.color = linearColor(look.color);

    // The entity as its prefab makes it, which overridden values differ from.
    const Entity instance = single ? scene::owningPrefabInstance(edited, active) : Entity{};
    std::shared_ptr<const scene::Scene> base;
    Entity prefabEntity;
    if (instance.isValid() && edited.has<scene::PrefabEntity>(active) && edited.get<scene::PrefabInstance>(instance).resolved)
    {
        base = scene::prefabBase(edited.get<scene::PrefabInstance>(instance).prefab, edited.uuid(instance));
        prefabEntity = base != nullptr ? base->findEntity(uuid) : Entity{};
    }

    if (single)
    {
        if (world.editedField() != nameField)
        {
            scene().get<scene::UiText>(nameField).text = edited.name(active);
        }
        prefabName.reset();
        if (prefabEntity.isValid() && active != instance && base->name(prefabEntity) != edited.name(active))
        {
            prefabName = base->name(prefabEntity);
        }
        scene().get<UiRect>(nameMark).visible = prefabName.has_value();
        scene().get<scene::UiContextMenu>(nameField).popup =
            prefabName && editing ? scene().reference(revertMenu) : scene::EntityRef{};
        scene().get<scene::UiText>(uuidText).text = uuid.toString();
        if (instance.isValid() && prefabIcon.isValid())
        {
            const scene::PrefabInstance& prefab = edited.get<scene::PrefabInstance>(instance);
            const std::string name = sceneAssetName(state, prefab.prefab);
            scene::UiImage& package = scene().get<scene::UiImage>(prefabIcon);
            package.color = linearColor(prefab.resolved ? colors.prefab : colors.error);
            fitText(kit, prefabLead, prefab.resolved ? (active == instance ? "Instance of" : "From") : "Missing prefab");
            scene().get<UiRect>(prefabLead).style = prefab.resolved ? "dim" : "error";
            fitText(kit, prefabTitle, name, true);
            fitText(kit, prefabTail, prefab.resolved && active != instance ? std::format("in {}", edited.name(instance)) : std::string{});
            tooltip(prefabLead, prefab.resolved ? "" : "The prefab cannot be loaded. The instance keeps its overrides until it comes back.");
            const bool unlocked = editing && state.mode == ToolsMode::Editor;
            enable(openPrefab, unlocked);
            enable(revertPrefab, unlocked && prefab.resolved);
            enable(makeLocal, unlocked && prefab.resolved);
        }
    }
    else
    {
        scene().get<scene::UiText>(countText).text = std::format("{} entities", inspected.size());
    }

    const ui::ElementStyle style = single ? ui::styleOf(edited, active, state.themes) : ui::ElementStyle{};
    for (Section& section : sections)
    {
        if (section.type == nullptr)
        {
            continue;
        }
        section.fromPrefab = std::ranges::any_of(inspected, [&](Entity entity) { return hasFromPrefab(edited, entity, *section.type); });
        // A component the prefab does not have is marked on its header.
        scene().get<UiRect>(section.added).visible = prefabEntity.isValid() && section.type->find(*base, prefabEntity) == nullptr;
        syncExtras(state, kit, edited, active, section, style);
    }
    for (PropertyRow& row : rows)
    {
        syncRow(state, edited, inspected, row, style, base.get(), prefabEntity, editing);
    }
    syncEntityColor(edited, inspected);
    layoutCards();
}

void InspectorUi::syncRow(const ToolsState& state, const scene::Scene& edited, std::span<const Entity> inspected, PropertyRow& row,
                          const ui::ElementStyle& style, const scene::Scene* base, Entity prefabEntity, bool editing)
{
    const ui::UiWorld& world = panel.world();
    const reflection::FieldInfo& field = *row.field;
    const Entity active = inspected.back();
    const bool shared = inspected.size() > 1;
    // The label and the editor follow the width of the panel.
    scene().get<UiRect>(row.labelBox).offsetMax.x = labelWidth - font * 0.4f;
    scene().get<UiRect>(row.editor).offsetMin.x = labelWidth;
    const void* const address = valueAddress(edited, active, row);
    if (address == nullptr)
    {
        return;
    }

    // A value that differs from the prefab's is marked, and its menu reverts it.
    row.prefabValue.reset();
    if (base != nullptr && prefabEntity.isValid() && !row.element)
    {
        if (const void* const prefabComponent = row.type->find(*base, prefabEntity))
        {
            serialization::TextValue value = scene::writeFieldValue(field, field.address(prefabComponent));
            if (value != scene::writeFieldValue(field, fieldAddress(edited, active, row)))
            {
                row.prefabValue = std::move(value);
            }
        }
    }
    scene().get<UiRect>(row.mark).visible = row.prefabValue.has_value();
    scene::UiText& label = scene().get<scene::UiText>(row.label);
    label.font = row.prefabValue ? EditorUiKit::boldFont() : EditorUiKit::regularFont();
    scene().get<UiRect>(row.label).style = row.prefabValue ? "text" : "label";
    scene().get<scene::UiContextMenu>(row.row).popup =
        row.prefabValue && editing ? scene().reference(revertMenu) : scene::EntityRef{};

    // A field the style of the element sets is written by the theme every frame: it shows the value
    // of the theme and cannot be changed here, which would not hold.
    row.themed = !shared && !row.element && style.sets(row.type->name(), field.name);
    const bool open = !row.themed;
    scene().get<UiRect>(row.editor).opacity = open ? 1.0f : 0.5f;
    tooltip(row.editor, row.themed ? std::format("Set by the style '{}' of the theme of the canvas.\nChange it in the theme, or leave "
                                                 "the style to set it here.",
                                                 style.name)
                                   : std::string{});
    if (row.button.entity.isValid())
    {
        scene().get<scene::UiButton>(row.button.entity).interactable = open;
    }
    if (row.editing)
    {
        // The controls hold the value being edited.
        return;
    }

    // With several entities, whether each of them has the same value.
    const auto differs = [&]() {
        if (!shared)
        {
            return false;
        }
        // Several entities never show the elements of a list, so the value is the whole field.
        const serialization::TextValue reference = scene::writeFieldValue(field, address);
        return std::ranges::any_of(inspected, [&](Entity entity) {
            const void* const other = valueAddress(edited, entity, row);
            return other != nullptr && scene::writeFieldValue(field, other) != reference;
        });
    };
    switch (row.kind)
    {
    case ControlKind::Toggle: {
        const bool value = *static_cast<const bool*>(address);
        const bool mixed = differs();
        scene::UiToggle& toggle = scene().get<scene::UiToggle>(row.control);
        toggle.value = value && !mixed;
        toggle.interactable = open;
        scene().get<scene::UiButton>(row.control).interactable = open;
        scene().get<scene::UiText>(row.detail).text = mixed ? std::string(mixedDash) : value ? "On" : "Off";
        break;
    }
    case ControlKind::Numbers: {
        const std::array<float, 4> values = numbersOf(field, address);
        if (field.kind == ValueKind::Quat)
        {
            row.degrees = math::Vec3{values[0], values[1], values[2]};
        }
        const scene::UiNumberField settings = numberSettings(field);
        for (std::size_t index = 0; index < row.count; ++index)
        {
            bool mixed = false;
            for (const Entity entity : inspected)
            {
                if (const void* const other = valueAddress(edited, entity, row))
                {
                    mixed |= std::abs(numbersOf(field, other)[index] - values[index]) > 1e-4f;
                }
            }
            scene::UiNumberField& number = scene().get<scene::UiNumberField>(row.numbers[index]);
            number.interactable = open;
            scene().get<scene::UiButton>(row.numbers[index]).interactable = open;
            if (world.editedField() == row.numbers[index])
            {
                continue;
            }
            number.value = values[index];
            number.format = mixed ? std::string(mixedDash) : settings.format;
        }
        break;
    }
    case ControlKind::Color: {
        const math::Vec4 color = colorOf(field, address);
        scene().get<scene::UiImage>(row.control).color = math::Vec4{color.x, color.y, color.z, 1.0f};
        scene().get<scene::UiButton>(row.control).interactable = open;
        if (row.opacity.isValid())
        {
            UiRect& bar = scene().get<UiRect>(row.opacity);
            bar.anchorMax.x = std::clamp(color.w, 0.0f, 1.0f);
            bar.visible = color.w < 0.999f;
            scene().get<scene::UiImage>(row.opacity).color = math::Vec4{1.0f, 1.0f, 1.0f, 0.85f};
        }
        scene().get<scene::UiText>(row.detail).text = differs() ? std::string(mixedDash) : hexOf(color, field.kind == ValueKind::Vec4);
        break;
    }
    case ControlKind::Text: {
        scene::UiInput& input = scene().get<scene::UiInput>(row.control);
        input.interactable = open;
        if (world.editedField() != row.control)
        {
            const bool mixed = differs();
            scene().get<scene::UiText>(row.control).text = mixed ? std::string{} : *static_cast<const std::string*>(address);
            input.placeholder = mixed ? std::string(mixedDash) : std::string{};
        }
        break;
    }
    case ControlKind::Choice:
        scene().get<scene::UiDropdown>(row.control).interactable = open;
        fillChoices(state, edited, row, address, differs());
        break;
    case ControlKind::ReadOnly:
        if (field.kind == ValueKind::Uuid)
        {
            scene().get<scene::UiText>(row.control).text =
                differs() ? std::string(mixedDash) : static_cast<const core::Uuid*>(address)->toString();
        }
        break;
    case ControlKind::ListHeader: {
        const std::size_t count = field.list->size(address);
        scene().get<scene::UiText>(row.control).text = std::format("{} {}", count, count == 1 ? "element" : "elements");
        break;
    }
    }
}

void InspectorUi::fillChoices(const ToolsState& state, const scene::Scene& edited, PropertyRow& row, const void* address, bool mixed)
{
    const ui::UiWorld& world = panel.world();
    const reflection::FieldInfo& field = *row.field;
    scene::UiDropdown& dropdown = scene().get<scene::UiDropdown>(row.control);
    // Long lists are only made when they may open: under the pointer, or already open.
    const bool full = world.hovered() == row.control || world.focused() == row.control || world.listedDropdown() == row.control;
    std::vector<std::string> options;
    std::int32_t selected = -1;
    std::string placeholder;
    row.assets.clear();
    row.references.clear();
    row.indices.clear();
    row.names.clear();

    if (field.kind == ValueKind::Enum)
    {
        const std::uint32_t current = reflection::readEnumIndex(field, address);
        for (std::uint32_t index = 0; index < field.enumNames.size(); ++index)
        {
            options.push_back(displayName(field.enumNames[index]));
            row.indices.push_back(index);
        }
        selected = current < options.size() ? static_cast<std::int32_t>(current) : -1;
        placeholder = std::to_string(current);
    }
    else if (field.kind == ValueKind::UInt32)
    {
        // The named layers of collision or the groups of sound of the project; the unnamed ones stay
        // out, but the first layer and the chosen value.
        const std::uint32_t current = *static_cast<const std::uint32_t*>(address);
        std::vector<std::string> names;
        if (field.physicsLayer)
        {
            const asset::PhysicsSettings settings = state.database != nullptr ? state.database->project().physics : asset::PhysicsSettings{};
            names.assign(settings.layerNames.begin(), settings.layerNames.end());
        }
        else
        {
            const asset::AudioSettings settings = state.database != nullptr ? state.database->project().audio : asset::AudioSettings{};
            names.assign(settings.groupNames.begin(), settings.groupNames.end());
        }
        for (std::uint32_t index = 0; index < names.size(); ++index)
        {
            if (index != current && (index != 0 || !field.physicsLayer) && names[index].empty())
            {
                continue;
            }
            if (index == current)
            {
                selected = static_cast<std::int32_t>(options.size());
            }
            options.push_back(names[index].empty() ? std::format("{}: (unused)", index) : std::format("{}: {}", index, names[index]));
            row.indices.push_back(index);
        }
        placeholder = std::format("{}: (unused)", current);
    }
    else if (field.kind == ValueKind::String)
    {
        // The sorting layers of the project, back to front. A name the project does not know stays as
        // it is, and draws in "Default".
        const std::string& current = *static_cast<const std::string*>(address);
        const asset::SortingSettings settings = state.database != nullptr ? state.database->project().sorting : asset::SortingSettings{};
        for (const std::string& name : settings.layers)
        {
            if (name == current || (current.empty() && name == "Default"))
            {
                selected = static_cast<std::int32_t>(options.size());
            }
            options.push_back(name);
            row.names.push_back(name);
        }
        placeholder = current.empty() ? std::string("Default") : std::format("{} (unknown: Default)", current);
    }
    else if (field.kind == ValueKind::AssetId)
    {
        const asset::AssetId value = *static_cast<const asset::AssetId*>(address);
        const std::optional<asset::AssetType> type = field.assetType.empty() ? std::nullopt : asset::parseAssetType(field.assetType);
        if (full)
        {
            row.assets.push_back(asset::AssetId{});
            if (!type || *type == asset::AssetType::Mesh)
            {
                for (const BuiltinAsset& builtin : builtinMeshAssets())
                {
                    row.assets.push_back(builtin.id);
                }
            }
            if (state.database != nullptr)
            {
                for (const asset::AssetInfo& info : state.database->assets(type))
                {
                    row.assets.push_back(info.id);
                }
            }
        }
        else
        {
            row.assets.push_back(value);
        }
        for (const asset::AssetId id : row.assets)
        {
            if (id == value)
            {
                selected = static_cast<std::int32_t>(options.size());
            }
            options.push_back(assetLabel(state, id));
        }
        placeholder = assetLabel(state, value);
        std::string where;
        if (state.database != nullptr && value.isValid())
        {
            if (const std::optional<asset::SourceFile> source = state.database->sourceOf(value))
            {
                where = source->path;
            }
        }
        tooltip(row.control, where.empty() ? std::string("Choose an asset, or drop one from FileSystem") : where);
    }
    else if (field.kind == ValueKind::Entity)
    {
        const scene::EntityRef value = *static_cast<const scene::EntityRef*>(address);
        const Entity target = edited.resolve(value);
        const auto labelOf = [&](const scene::EntityRef& reference, Entity entity) {
            return reference.isNil() ? std::string("(none)") : entity.isValid() ? edited.name(entity) : std::string("(missing)");
        };
        std::vector<std::string> labels;
        row.references.push_back(scene::EntityRef{});
        labels.emplace_back("(none)");
        if (full)
        {
            // The entities of the scene as its tree shows them, indented by depth.
            const std::function<void(Entity, int)> walk = [&](Entity entity, int depth) {
                for (; entity.isValid(); entity = edited.nextSibling(entity))
                {
                    row.references.push_back(edited.reference(entity));
                    labels.push_back(std::string(static_cast<std::size_t>(depth) * 3, ' ') + edited.name(entity));
                    walk(edited.firstChild(entity), depth + 1);
                }
            };
            walk(edited.firstRoot(), 0);
        }
        else if (!value.isNil())
        {
            row.references.push_back(value);
            labels.push_back(labelOf(value, target));
        }
        for (std::size_t index = 0; index < row.references.size(); ++index)
        {
            if (row.references[index] == value)
            {
                selected = static_cast<std::int32_t>(index);
            }
        }
        options = std::move(labels);
        placeholder = labelOf(value, target);
        tooltip(row.control, value.isNil() ? std::string("Choose an entity, or drop one from the scene tree") : value.uuid.toString());
    }

    if (mixed)
    {
        selected = -1;
        placeholder = std::string(mixedDash);
    }
    if (dropdown.options != options)
    {
        dropdown.options = std::move(options);
    }
    dropdown.selected = selected;
    dropdown.placeholder = std::move(placeholder);
}

void InspectorUi::syncExtras(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, Entity active, Section& section,
                             const ui::ElementStyle& style)
{
    if (section.styleLine)
    {
        Line& status = section.lines[*section.styleLine];
        status.shown = !style.name.empty();
        const float iconSize = std::round(font * 1.1f);
        UiRect& iconRect = scene().get<UiRect>(section.styleIcon);
        iconRect.offsetMin.x = labelWidth;
        iconRect.offsetMax.x = labelWidth + iconSize;
        UiRect& textRect = scene().get<UiRect>(section.styleText);
        textRect.offsetMin.x = labelWidth + iconSize + font * 0.4f;
        textRect.offsetMax.x = -(scene().get<UiRect>(section.styleOpen.entity).offsetMax.x - scene().get<UiRect>(section.styleOpen.entity).offsetMin.x) -
                               font * 0.4f;

        section.themeFile.reset();
        std::string themeName = "the theme";
        if (state.database != nullptr && style.theme.isValid())
        {
            if (const std::optional<asset::SourceFile> source = state.database->sourceOf(style.theme))
            {
                section.themeFile = state.database->project().absolutePath(source->path);
                const std::size_t slash = source->path.find_last_of('/');
                themeName = slash == std::string::npos ? source->path : source->path.substr(slash + 1);
            }
        }
        scene::UiImage& glyph = scene().get<scene::UiImage>(section.styleIcon);
        std::string shown;
        std::string explained;
        if (!style.theme.isValid())
        {
            glyph.texture = kit.icon(Icon::TriangleAlert);
            scene().get<UiRect>(section.styleIcon).style = "icon_warning";
            scene().get<UiRect>(section.styleText).style = "warning";
            shown = "No theme on the canvas";
            explained = std::format("The canvas above this element names no theme, so the style '{}' sets nothing.", style.name);
        }
        else if (style.data == nullptr || style.style == nullptr)
        {
            glyph.texture = kit.icon(Icon::TriangleAlert);
            scene().get<UiRect>(section.styleIcon).style = "icon_warning";
            scene().get<UiRect>(section.styleText).style = "warning";
            shown = "Not in the theme";
            explained = style.data == nullptr ? std::format("The theme of the canvas, {}, is not loaded.", themeName)
                                              : std::format("{} has no style named '{}'.", themeName, style.name);
        }
        else
        {
            // Short, so that the way to the theme stays in view in a narrow panel; the name of the theme
            // is in the tooltip.
            glyph.texture = kit.icon(Icon::Palette);
            scene().get<UiRect>(section.styleIcon).style = "icon_accent";
            scene().get<UiRect>(section.styleText).style = "dim";
            const std::size_t count = style.style->values.size();
            shown = std::format("{} {}", count, count == 1 ? "field" : "fields");
            explained = std::format("The style '{}' of {} sets {} {} of this element: they show greyed out.", style.name, themeName, count,
                                    count == 1 ? "field" : "fields");
        }
        scene().get<scene::UiText>(section.styleText).text = shown;
        tooltip(section.styleText, explained);
        scene().get<UiRect>(section.styleOpen.entity).visible = section.themeFile.has_value();
        tooltip(section.styleOpen.entity, std::format("Open {} in the Script screen", themeName));
    }
    if (section.particles.isValid())
    {
        particles::ParticleWorld* const world = state.particleWorld;
        scene().get<scene::UiText>(section.particles).text =
            world != nullptr ? std::format("{} particles", world->particleCount(active)) : std::string{};
        enable(section.restart, world != nullptr);
        enable(section.stop, world != nullptr);
    }
    if (section.painter != nullptr)
    {
        syncTilePainter(*this, state, kit, edited, active, section);
    }
    if (section.navInfo.isValid())
    {
        const NavMeshSummary summary = navMeshSummary(state, edited.get<scene::NavMeshSurface>(active));
        scene().get<scene::UiText>(section.navInfo).text = summary.text;
        section.lines[*section.navWarningLine].shown = summary.stale;
        section.lines[*section.navStatusLine].shown = !state.navMeshBakeStatus.empty();
        scene().get<scene::UiText>(section.navStatus).text = state.navMeshBakeStatus;
        scene().get<UiRect>(section.navStatus).style = state.navMeshBakeFailed ? "error" : "dim";
        const bool editing = state.playState == PlayState::Editing;
        enable(section.bake, editing);
        enable(section.clear, editing && edited.get<scene::NavMeshSurface>(active).navMesh.isValid());
    }
}

void InspectorUi::syncEntityColor(const scene::Scene& edited, std::span<const Entity> inspected)
{
    if (!colorRow)
    {
        return;
    }
    const ui::UiWorld& world = panel.world();
    const PropertyRow& row = rows[*colorRow];
    const void* const address = valueAddress(edited, inspected.back(), row);
    if (address == nullptr)
    {
        return;
    }
    const bool alpha = row.field->kind == ValueKind::Vec4;
    const math::Vec4 color = colorOf(*row.field, address);
    scene::UiColorPicker& chosen = scene().get<scene::UiColorPicker>(picker);
    chosen.alpha = alpha;
    chosen.interactable = !row.themed;
    if (world.held() != picker)
    {
        chosen.color = color;
    }
    if (world.editedField() != hexField)
    {
        scene().get<scene::UiText>(hexField).text = hexOf(color, alpha);
    }
    const std::size_t count = alpha ? 4 : 3;
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        scene().get<UiRect>(channels[index]).visible = index < count;
        if (index >= count || world.editedField() == channels[index] || world.held() == channels[index])
        {
            continue;
        }
        bool mixed = false;
        for (const Entity entity : inspected)
        {
            if (const void* const other = valueAddress(edited, entity, row))
            {
                mixed |= std::abs(colorOf(*row.field, other)[static_cast<math::Vec4::length_type>(index)] -
                                  color[static_cast<math::Vec4::length_type>(index)]) > 1e-4f;
            }
        }
        scene::UiNumberField& number = scene().get<scene::UiNumberField>(channels[index]);
        number.value = color[static_cast<math::Vec4::length_type>(index)];
        number.format = mixed ? std::string(mixedDash) : std::string("{}");
    }
    const scene::UiLayout& layout = scene().get<scene::UiLayout>(colorPopup);
    UiRect& popup = scene().get<UiRect>(colorPopup);
    const float height = layout.padding.y + layout.padding.w + font * 11.0f + line + (line - 4.0f) + layout.spacing * 2.0f;
    popup.offsetMax.y = popup.offsetMin.y + height;
}

void InspectorUi::beginEdit(const scene::Scene& edited, std::span<const Entity> inspected, PropertyRow& row)
{
    if (row.editing)
    {
        return;
    }
    row.editing = true;
    row.starts.clear();
    for (const Entity entity : inspected)
    {
        if (const void* const address = fieldAddress(edited, entity, row))
        {
            row.starts.emplace_back(edited.uuid(entity), scene::writeFieldValue(*row.field, address));
        }
    }
}

void InspectorUi::commit(ToolsState& state, const scene::Scene& edited, PropertyRow& row)
{
    if (!row.editing)
    {
        return;
    }
    row.editing = false;
    std::vector<std::unique_ptr<Command>> commands;
    for (auto& [uuid, start] : row.starts)
    {
        const void* const address = fieldAddress(edited, edited.findEntity(uuid), row);
        if (address == nullptr)
        {
            continue;
        }
        serialization::TextValue end = scene::writeFieldValue(*row.field, address);
        if (start != end)
        {
            commands.push_back(makeSetFieldCommand(uuid, std::string(row.type->name()), row.field->name, std::move(start), std::move(end)));
        }
    }
    row.starts.clear();
    const std::size_t count = commands.size();
    if (count == 1)
    {
        state.history.recordApplied(std::move(commands.front()));
    }
    else if (count > 1)
    {
        state.history.recordApplied(
            makeCompositeCommand(std::move(commands), std::format("Edit {} of {} entities", displayName(row.field->name), count)));
    }
}

bool InspectorUi::isActive(std::size_t index)
{
    const ui::UiWorld& world = panel.world();
    const Entity held = world.held();
    const Entity typed = world.editedField();
    const auto busy = [&](Entity entity) { return entity.isValid() && (entity == held || entity == typed); };
    const PropertyRow& row = rows[index];
    if (busy(row.control) || std::ranges::any_of(row.numbers, busy))
    {
        return true;
    }
    return colorRow == index && (busy(picker) || busy(hexField) || std::ranges::any_of(channels, busy));
}

void InspectorUi::answerRow(ToolsState& state, scene::Scene& edited, std::span<const Entity> inspected, std::size_t index)
{
    ui::UiWorld& world = panel.world();
    PropertyRow& row = rows[index];
    const reflection::FieldInfo& field = *row.field;
    // A change of one click is one step to undo, taken at once.
    const auto set = [&](const auto& write) {
        beginEdit(edited, inspected, row);
        each(edited, inspected, row, write);
        commit(state, edited, row);
    };
    switch (row.kind)
    {
    case ControlKind::Toggle:
        if (world.wasChanged(row.control))
        {
            const bool value = scene().get<scene::UiToggle>(row.control).value;
            set([&](void* address, bool) { *static_cast<bool*>(address) = value; });
        }
        break;
    case ControlKind::Numbers:
        for (std::size_t axis = 0; axis < row.count; ++axis)
        {
            if (!world.wasChanged(row.numbers[axis]))
            {
                continue;
            }
            // A drag is one step once let go; the others follow the active entity in this number only.
            const float value = scene().get<scene::UiNumberField>(row.numbers[axis]).value;
            beginEdit(edited, inspected, row);
            if (field.kind == ValueKind::Quat)
            {
                row.degrees[static_cast<math::Vec3::length_type>(axis)] = value;
            }
            each(edited, inspected, row, [&](void* address, bool active) {
                if (field.kind == ValueKind::Quat && active)
                {
                    *static_cast<math::Quat*>(address) = math::quatFromEulerAngles(math::radians(row.degrees));
                }
                else
                {
                    writeNumber(field, address, axis, value);
                }
            });
        }
        break;
    case ControlKind::Color:
        if (world.wasClicked(row.control) && !row.themed)
        {
            colorRow = index;
            hexTyping = false;
            std::optional<math::Vec2> at;
            if (const ui::LaidOutRect* const rect = world.canvases().empty() ? nullptr : world.canvases().front().layout.find(row.control))
            {
                at = math::Vec2{rect->min.x, rect->max.y + 2.0f};
            }
            world.openPopup(scene(), colorPopup, at);
        }
        break;
    case ControlKind::Text:
        if (world.editedField() == row.control)
        {
            row.typing = true;
        }
        else if (std::exchange(row.typing, false) && !panel.input().cancelPressed)
        {
            // Written once the field is left, with Enter or a click elsewhere; Escape drops it.
            const std::string typed = scene().get<scene::UiText>(row.control).text;
            const void* const address = valueAddress(edited, inspected.back(), row);
            const bool mixed = !scene().get<scene::UiInput>(row.control).placeholder.empty();
            if (address != nullptr && (mixed ? !typed.empty() : *static_cast<const std::string*>(address) != typed))
            {
                set([&](void* target, bool) { *static_cast<std::string*>(target) = typed; });
            }
        }
        break;
    case ControlKind::Choice: {
        std::optional<std::size_t> chosen;
        if (world.wasChanged(row.control))
        {
            const std::int32_t selected = scene().get<scene::UiDropdown>(row.control).selected;
            if (selected >= 0)
            {
                chosen = static_cast<std::size_t>(selected);
            }
        }
        if (chosen && field.kind == ValueKind::Enum && *chosen < row.indices.size())
        {
            set([&](void* address, bool) { reflection::writeEnumIndex(field, address, row.indices[*chosen]); });
        }
        else if (chosen && field.kind == ValueKind::UInt32 && *chosen < row.indices.size())
        {
            set([&](void* address, bool) { *static_cast<std::uint32_t*>(address) = row.indices[*chosen]; });
        }
        else if (chosen && field.kind == ValueKind::String && *chosen < row.names.size())
        {
            set([&](void* address, bool) { *static_cast<std::string*>(address) = row.names[*chosen]; });
        }
        else if (chosen && field.kind == ValueKind::AssetId && *chosen < row.assets.size())
        {
            set([&](void* address, bool) { *static_cast<asset::AssetId*>(address) = row.assets[*chosen]; });
        }
        else if (chosen && field.kind == ValueKind::Entity && *chosen < row.references.size())
        {
            set([&](void* address, bool) { *static_cast<scene::EntityRef*>(address) = row.references[*chosen]; });
        }
        // An asset dropped from FileSystem, or an entity from the scene tree; the target only lights
        // up for what the field takes.
        const ui::Drop* const dropped = world.wasDropped(row.control) ? world.dropped() : nullptr;
        const std::optional<core::Uuid> uuid = dropped != nullptr && !row.themed ? core::Uuid::parse(dropped->data) : std::nullopt;
        if (uuid && field.kind == ValueKind::AssetId)
        {
            set([&](void* address, bool) { *static_cast<asset::AssetId*>(address) = asset::AssetId{*uuid}; });
        }
        else if (uuid && field.kind == ValueKind::Entity)
        {
            set([&](void* address, bool) { *static_cast<scene::EntityRef*>(address) = scene::EntityRef{*uuid}; });
        }
        break;
    }
    case ControlKind::ListHeader:
        if (world.wasClicked(row.button.entity))
        {
            beginEdit(edited, inspected, row);
            if (void* const list = fieldAddress(edited, inspected.back(), row))
            {
                field.list->resize(list, field.list->size(list) + 1);
            }
            commit(state, edited, row);
        }
        break;
    case ControlKind::ReadOnly:
        break;
    }
    // An element removed from its list.
    if (row.element && world.wasClicked(row.button.entity))
    {
        beginEdit(edited, inspected, row);
        if (void* const list = fieldAddress(edited, inspected.back(), row); list != nullptr && *row.element < field.list->size(list))
        {
            field.list->erase(list, *row.element);
        }
        commit(state, edited, row);
    }
}

void InspectorUi::answerEntityColor(scene::Scene& edited, std::span<const Entity> inspected)
{
    ui::UiWorld& world = panel.world();
    if (colorRow && !world.isPopupOpen(scene(), colorPopup))
    {
        colorRow.reset();
        hexTyping = false;
    }
    if (!colorRow)
    {
        return;
    }
    PropertyRow& row = rows[*colorRow];
    const reflection::FieldInfo& field = *row.field;
    // The square and its bars give the whole colour to every entity.
    if (world.wasChanged(picker))
    {
        const math::Vec4 chosen = scene().get<scene::UiColorPicker>(picker).color;
        beginEdit(edited, inspected, row);
        each(edited, inspected, row, [&](void* address, bool) { writeColor(field, address, chosen); });
    }
    // The hexadecimal, read once the field is left; a colour brighter than white keeps its intensity.
    if (world.editedField() == hexField)
    {
        hexTyping = true;
    }
    else if (std::exchange(hexTyping, false) && !panel.input().cancelPressed)
    {
        const std::string typed = scene().get<scene::UiText>(hexField).text;
        std::string_view digits = typed;
        while (!digits.empty() && (digits.front() == '#' || digits.front() == ' '))
        {
            digits.remove_prefix(1);
        }
        const bool writesAlpha = digits.size() == 4 || digits.size() == 8;
        if (const std::optional<math::Vec4> read = ui::colorFromHex(typed))
        {
            beginEdit(edited, inspected, row);
            each(edited, inspected, row, [&](void* address, bool) {
                const math::Vec4 current = colorOf(field, address);
                const float intensity = std::max({1.0f, current.x, current.y, current.z});
                const math::Vec4 linear = ui::linearFromSrgb(*read);
                writeColor(field, address,
                           math::Vec4{linear.x * intensity, linear.y * intensity, linear.z * intensity, writesAlpha ? read->w : current.w});
            });
        }
    }
    // The numbers of the value, one channel each.
    for (std::size_t index = 0; index < channels.size(); ++index)
    {
        if (!world.wasChanged(channels[index]))
        {
            continue;
        }
        const float value = scene().get<scene::UiNumberField>(channels[index]).value;
        beginEdit(edited, inspected, row);
        each(edited, inspected, row, [&](void* address, bool) {
            math::Vec4 color = colorOf(field, address);
            color[static_cast<math::Vec4::length_type>(index)] = value;
            writeColor(field, address, color);
        });
    }
}

void InspectorUi::answerMenus(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const Entity> inspected)
{
    ui::UiWorld& world = panel.world();
    const Entity active = inspected.back();
    const core::Uuid uuid = edited.uuid(active);

    // The menu of a component, from its button or from a right click on its header.
    for (std::size_t index = 0; index < sections.size(); ++index)
    {
        const Section& section = sections[index];
        if (section.menu.entity.isValid() && world.wasClicked(section.menu.entity))
        {
            menuSection = index;
            std::optional<math::Vec2> at;
            if (const ui::LaidOutRect* const rect =
                    world.canvases().empty() ? nullptr : world.canvases().front().layout.find(section.menu.entity))
            {
                at = math::Vec2{rect->max.x - font * 14.0f, rect->max.y + 2.0f};
            }
            world.openPopup(scene(), componentMenu, at);
        }
        if (panel.input().secondaryPressed && world.isPopupOpen(scene(), componentMenu) && world.contextTarget() == section.header)
        {
            menuSection = index;
        }
    }
    if (world.isPopupOpen(scene(), componentMenu) && menuSection)
    {
        const Section& section = sections[*menuSection];
        enable(removeComponent, !section.fromPrefab);
        tooltip(removeComponent.entity, section.fromPrefab                                   ? "The component comes from the prefab"
                                        : scene().get<UiRect>(section.added).visible ? "The prefab does not have this component: removing it reverts the override"
                                                                                           : "");
        fitMenu(componentMenu, font * 14.0f);
    }
    if (world.wasClicked(removeComponent.entity) && menuSection && sections[*menuSection].type != nullptr)
    {
        const std::string name(sections[*menuSection].type->name());
        if (inspected.size() == 1)
        {
            state.pendingCommand = makeRemoveComponentCommand(uuid, name);
        }
        else
        {
            std::vector<std::unique_ptr<Command>> commands;
            for (const Entity entity : inspected)
            {
                commands.push_back(makeRemoveComponentCommand(edited.uuid(entity), name));
            }
            state.pendingCommand = makeCompositeCommand(std::move(commands), std::format("Remove {} from {} entities", name, inspected.size()));
        }
    }
    if (!world.isPopupOpen(scene(), componentMenu))
    {
        menuSection.reset();
    }

    // The menu of a value that differs from its prefab's, or of a name.
    if (panel.input().secondaryPressed && world.isPopupOpen(scene(), revertMenu))
    {
        const Entity target = world.contextTarget();
        revertName = target == nameField;
        revertRow.reset();
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (rows[index].row == target)
            {
                revertRow = index;
            }
        }
        relabel(kit, revert, revertName ? "Revert to Prefab Name" : "Revert to Prefab Value");
        fitMenu(revertMenu, font * 15.0f);
    }
    if (world.wasClicked(revert.entity))
    {
        if (revertName && prefabName)
        {
            state.pendingCommand = makeRenameCommand(uuid, edited.name(active), *prefabName);
        }
        else if (revertRow && rows[*revertRow].prefabValue)
        {
            const PropertyRow& row = rows[*revertRow];
            if (const void* const address = fieldAddress(edited, active, row))
            {
                state.pendingCommand = makeSetFieldCommand(uuid, std::string(row.type->name()), row.field->name,
                                                           scene::writeFieldValue(*row.field, address), *row.prefabValue);
            }
        }
    }
}

void InspectorUi::answer(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, std::span<const Entity> inspected)
{
    ui::UiWorld& world = panel.world();
    const Entity active = inspected.back();
    const core::Uuid uuid = edited.uuid(active);

    // The name, renamed once the field is left; Escape drops what was typed.
    if (nameField.isValid())
    {
        if (world.editedField() == nameField)
        {
            naming = true;
        }
        else if (std::exchange(naming, false) && !panel.input().cancelPressed)
        {
            const std::string typed = scene().get<scene::UiText>(nameField).text;
            if (typed != edited.name(active))
            {
                state.pendingCommand = makeRenameCommand(uuid, edited.name(active), typed);
            }
        }
    }

    // Cards and groups folded from their headers.
    answerForm();

    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        answerRow(state, edited, inspected, index);
    }
    answerEntityColor(edited, inspected);
    // A drag, a typed number or a colour ends as one step once nothing holds it anymore.
    for (std::size_t index = 0; index < rows.size(); ++index)
    {
        if (rows[index].editing && !isActive(index))
        {
            commit(state, edited, rows[index]);
        }
    }
    answerMenus(state, kit, edited, inspected);

    // What the prefab of the instance does.
    const Entity instance = inspected.size() == 1 ? scene::owningPrefabInstance(edited, active) : Entity{};
    if (instance.isValid() && openPrefab.entity.isValid())
    {
        if (world.wasClicked(openPrefab.entity))
        {
            state.prefabToOpen = edited.get<scene::PrefabInstance>(instance).prefab;
        }
        else if (world.wasClicked(revertPrefab.entity))
        {
            state.pendingCommand = makeReplaceEntityTreeCommand(edited.uuid(instance), scene::saveRevertedPrefabInstance(edited, instance),
                                                                "Revert instance");
        }
        else if (world.wasClicked(makeLocal.entity))
        {
            state.pendingCommand = makeReplaceEntityTreeCommand(edited.uuid(instance), scene::saveUnpackedEntityTree(edited, instance),
                                                                "Make instance local");
        }
    }

    // What some components add under their properties.
    for (Section& section : sections)
    {
        if (section.painter != nullptr)
        {
            answerTilePainter(*this, state, section);
        }
        if (section.styleOpen.entity.isValid() && world.wasClicked(section.styleOpen.entity) && section.themeFile)
        {
            openInPreferredEditor(state, *section.themeFile);
        }
        if (section.restart.entity.isValid() && state.particleWorld != nullptr)
        {
            if (world.wasClicked(section.restart.entity))
            {
                state.particleWorld->play(edited, active);
            }
            else if (world.wasClicked(section.stop.entity))
            {
                state.particleWorld->stop(active, true);
            }
        }
        if (section.bake.entity.isValid())
        {
            if (world.wasClicked(section.bake.entity))
            {
                bakeNavMesh(state, edited, active);
            }
            else if (world.wasClicked(section.clear.entity))
            {
                clearNavMesh(state, edited, active);
            }
        }
    }

    // Components that some of them lack, added to those, from the window that creates entities.
    if (world.wasClicked(addComponent.entity))
    {
        std::vector<core::Uuid> targets;
        for (const Entity entity : inspected)
        {
            targets.push_back(edited.uuid(entity));
        }
        openAddComponent(state, std::move(targets));
    }
}

void InspectorUi::update(ToolsState& state, EditorUiKit& kit, scene::Scene& edited, core::Duration delta)
{
    const ThemeColors& colors = themeColors();
    kit.refreshTheme(colors);
    setFont(state.theme.fontSize);
    if (!built)
    {
        build(state, kit);
    }
    styleTooltips(colors);

    // What the inspector shows: a state or a transition of the Animator panel until another entity or
    // asset is chosen, the entities selected, a code file, an asset, or a word to choose something.
    AnimatorEditor& animator = state.animatorEditor;
    if (animator.inspecting && (state.selection.active() != animator.inspectedEntity || state.selectedAsset != animator.inspectedAsset))
    {
        animator.inspecting = false;
    }
    const Entity active = edited.findEntity(state.selection.active());
    if (state.selectedAsset.isValid() && (state.database == nullptr || state.database->find(state.selectedAsset) == nullptr))
    {
        state.selectedAsset = {};
    }
    std::string_view kind;
    if (animator.inspecting && state.showAnimator &&
        (animator.selected == AnimatorElement::State || animator.selected == AnimatorElement::Transition))
    {
        kind = "animator element";
    }
    else if (active.isValid())
    {
        kind = {};
    }
    else if (!state.selectedCode.empty())
    {
        kind = "code";
    }
    else if (state.selectedAsset.isValid())
    {
        switch (state.database->find(state.selectedAsset)->type)
        {
        case asset::AssetType::Texture:
            kind = "texture";
            break;
        case asset::AssetType::Model:
            kind = "model";
            break;
        case asset::AssetType::AudioClip:
            kind = "audio";
            break;
        case asset::AssetType::Curve:
            kind = "curve";
            break;
        case asset::AssetType::SpriteFrames:
            kind = "sprite frames";
            break;
        case asset::AssetType::Tileset:
            kind = "tileset";
            break;
        case asset::AssetType::Animator:
            kind = "animator";
            break;
        case asset::AssetType::Translation:
            kind = "translation";
            break;
        default:
            kind = "asset";
            break;
        }
    }
    else
    {
        kind = "none";
    }
    if (!kind.empty())
    {
        updatePage(state, kit, edited, kind, delta);
        return;
    }
    if (page)
    {
        page.reset();
        pageKind.clear();
        pageTarget.clear();
        signature.clear();
    }

    // The entities shown: the active one last, which the others follow.
    std::vector<Entity> inspected;
    if (state.selection.size() > 1)
    {
        for (const core::Uuid selected : state.selection.entities())
        {
            if (const Entity found = edited.findEntity(selected); found.isValid() && found != active)
            {
                inspected.push_back(found);
            }
        }
    }
    inspected.push_back(active);

    if (std::string wanted = signatureOf(edited, inspected); wanted != signature)
    {
        std::vector<core::Uuid> uuids;
        for (const Entity entity : inspected)
        {
            uuids.push_back(edited.uuid(entity));
        }
        // Another entity starts at the top; the same one keeps its place when its components change.
        if (uuids != shownEntities)
        {
            scene().get<scene::UiScroll>(scroll).offset = math::Vec2{0.0f};
            shownEntities = std::move(uuids);
        }
        rebuild(state, kit, edited, inspected);
        signature = std::move(wanted);
    }
    scene().get<scene::UiScroll>(scroll).speed = line * 3.0f;
    sync(state, kit, edited, inspected);
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answer(state, kit, edited, inspected);

    // A number shows that it is dragged sideways.
    const ui::UiWorld& world = panel.world();
    const Entity pointed = world.held().isValid() ? world.held() : world.hovered();
    if (pointed.isValid() && scene().isAlive(pointed) && scene().has<scene::UiNumberField>(pointed) && world.editedField() != pointed)
    {
        state.input.cursor = platform::Cursor::ResizeHorizontal;
    }
}

void InspectorUi::updatePage(ToolsState& state, EditorUiKit& kit, const scene::Scene& edited, std::string_view kind, core::Duration delta)
{
    if (!page || pageKind != kind)
    {
        page = kind == "animator element" ? makeAnimatorElementPage()
               : kind == "code"           ? makeCodePage()
               : kind == "texture"        ? makeTexturePage()
               : kind == "model"          ? makeModelPage()
               : kind == "audio"          ? makeAudioPage()
               : kind == "curve"          ? makeCurvePage()
               : kind == "sprite frames"  ? makeSpriteFramesPage()
               : kind == "tileset"        ? makeTilesetPage()
               : kind == "animator"       ? makeAnimatorPage()
               : kind == "translation"    ? makeTranslationPage()
               : kind == "asset"          ? makeAssetPage()
                                          : makeEmptyPage();
        pageKind = kind;
        signature.clear();
    }
    shownEntities.clear();
    std::string target = std::format("{}|{}|{}", kind, state.selectedAsset.uuid.toString(), core::toUtf8(state.selectedCode));
    if (std::string wanted = std::format("page|{}|{}|{}", font, target, page->signature(state)); wanted != signature)
    {
        // Another asset starts at the top; the same one keeps its place when what it holds changes.
        if (target != pageTarget)
        {
            scene().get<scene::UiScroll>(scroll).offset = math::Vec2{0.0f};
            pageTarget = std::move(target);
        }
        clear(state, edited);
        page->build(*this, state, kit);
        signature = std::move(wanted);
    }
    scene().get<scene::UiScroll>(scroll).speed = line * 3.0f;
    labelWidth = std::clamp(std::round(panel.size().x * 0.38f), font * 5.5f, font * 13.0f);
    page->sync(*this, state, kit);
    layoutCards();
    panel.update(kit, delta, UiPanel::zoomFor(font));
    answerForm();
    page->answer(*this, state, kit);

    const ui::UiWorld& world = panel.world();
    const Entity pointed = world.held().isValid() ? world.held() : world.hovered();
    if (pointed.isValid() && scene().isAlive(pointed) && scene().has<scene::UiNumberField>(pointed) && world.editedField() != pointed)
    {
        state.input.cursor = platform::Cursor::ResizeHorizontal;
    }
}

std::shared_ptr<InspectorUi> makeInspectorUi()
{
    return std::make_shared<InspectorUi>();
}

void updateInspectorUi(ToolsState& state, scene::Scene& scene, core::Duration delta)
{
    state.inspectorUi->update(state, *state.uiKit, scene, delta);
}

void renderInspector(ToolsState& state, render::RenderWorld& world)
{
    if (state.inspectorUi && state.uiKit)
    {
        state.inspectorUi->panel.render(*state.uiKit, world, linearColor(themeColors().panel));
    }
}

} // namespace devex::tools::detail
