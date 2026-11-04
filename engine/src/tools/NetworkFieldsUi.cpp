// The fields a NetworkSync keeps the same on every player's game, in the Inspector: those of the
// components of the entity, to check one by one, as Godot's replication panel lists its properties.
#include "InspectorUi.hpp"

#include <devex/scene/FieldValue.hpp>
#include <devex/scene/NetworkComponents.hpp>
#include <devex/tools/SceneCommands.hpp>

#include <algorithm>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace devex::tools::detail {

using scene::Entity;

void InspectorUi::addNetworkFields(Section& section, const scene::Scene& edited, Entity active)
{
    const auto* const sync = edited.tryGet<scene::NetworkSync>(active);
    if (sync == nullptr)
    {
        return;
    }
    std::vector<std::string> listed = sync->fields;
    const auto addRow = [&](int group, std::string fullName, std::string label) {
        NetworkFieldRow& made = networkRows.emplace_back();
        made.name = std::move(fullName);
        made.form = formRow(section, std::move(label));
        section.lines.back().group = group;
        made.control = toggle(made.form.editor);
        tooltip(made.form.label, std::format("Its authority keeps {} the same on the other players' games", made.name));
        std::erase(listed, made.name);
    };
    for (const scene::ComponentType& type : scene::componentRegistry().types())
    {
        if (type.find(edited, active) == nullptr || type.name() == "NetworkSync")
        {
            continue;
        }
        int group = -1;
        for (const reflection::FieldInfo& field : type.type->fields)
        {
            if (field.hidden || field.runtime || field.list != nullptr)
            {
                continue;
            }
            if (group < 0)
            {
                group = addGroup(section, std::string(type.name()));
            }
            addRow(group, std::format("{}.{}", type.name(), field.name), displayName(field.name));
        }
    }
    // What it lists and the entity no longer has: a way to take them off.
    if (!listed.empty())
    {
        const int group = addGroup(section, "Not on this entity");
        const std::vector<std::string> missing = listed;
        for (const std::string& name : missing)
        {
            addRow(group, name, name);
        }
    }
}

void InspectorUi::syncNetworkFields(const scene::Scene& edited, Entity active)
{
    const auto* const sync = edited.tryGet<scene::NetworkSync>(active);
    if (sync == nullptr)
    {
        return;
    }
    for (const NetworkFieldRow& row : networkRows)
    {
        setToggle(row.control, std::ranges::contains(sync->fields, row.name));
    }
}

void InspectorUi::answerNetworkFields(ToolsState& state, scene::Scene& edited, Entity active)
{
    const ui::UiWorld& world = panel.world();
    auto* const sync = edited.tryGet<scene::NetworkSync>(active);
    const scene::ComponentType* const type = scene::componentRegistry().find("NetworkSync");
    const reflection::FieldInfo* const fields = type != nullptr ? type->type->findField("fields") : nullptr;
    if (sync == nullptr || fields == nullptr)
    {
        return;
    }
    for (const NetworkFieldRow& row : networkRows)
    {
        if (!world.wasChanged(row.control))
        {
            continue;
        }
        serialization::TextValue before = scene::writeFieldValue(*fields, fields->address(sync));
        if (scene().get<scene::UiToggle>(row.control).value)
        {
            if (!std::ranges::contains(sync->fields, row.name))
            {
                sync->fields.push_back(row.name);
            }
        }
        else
        {
            std::erase(sync->fields, row.name);
        }
        serialization::TextValue after = scene::writeFieldValue(*fields, fields->address(sync));
        state.history.recordApplied(makeSetFieldCommand(edited.uuid(active), "NetworkSync", "fields", std::move(before), std::move(after)));
    }
}

} // namespace devex::tools::detail
