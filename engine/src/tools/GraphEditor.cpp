#include "GraphEditor.hpp"
#include "ToolsState.hpp"

#include <devex/core/File.hpp>
#include <devex/core/Log.hpp>
#include <devex/scene/UiComponents.hpp>

#include <cmath>
#include <format>

namespace devex::tools::detail
{

void syncGraphGrid(scene::Scene& scene, scene::Entity layer, std::vector<scene::Entity>& lines,
                   const GraphView& view, math::Vec2 size, math::Vec4 color)
{
    float step = 32.0f * view.scale;
    while (step < 18.0f)
        step *= 2.0f;
    std::size_t used = 0;
    const auto line = [&](math::Vec2 from, math::Vec2 to)
    {
        if (used == lines.size())
        {
            const scene::Entity made = scene.createEntity("Line");
            static_cast<void>(scene.setParent(made, layer));
            scene.add<scene::UiRect>(
                made, scene::UiRect{.anchorMin = {0.0f, 0.0f}, .anchorMax = {0.0f, 0.0f}});
            scene.add<scene::UiImage>(made, scene::UiImage{.raycastTarget = false});
            lines.push_back(made);
        }
        const scene::Entity shown = lines[used++];
        auto& rect = scene.get<scene::UiRect>(shown);
        rect.visible = true;
        rect.offsetMin = from;
        rect.offsetMax = to;
        scene.get<scene::UiImage>(shown).color = color;
    };
    for (float x = std::fmod(view.origin.x, step); x < size.x && used < 400; x += step)
        line({std::round(x), 0.0f}, {std::round(x) + 1.0f, size.y});
    for (float y = std::fmod(view.origin.y, step); y < size.y && used < 400; y += step)
        line({0.0f, std::round(y)}, {size.x, std::round(y) + 1.0f});
    for (std::size_t index = used; index < lines.size(); ++index)
        scene.get<scene::UiRect>(lines[index]).visible = false;
}

bool saveGraphAsset(ToolsState& state, asset::AssetId asset, const std::filesystem::path& file,
                    std::string_view text, std::filesystem::file_time_type& time,
                    std::string& error)
{
    if (auto written = core::writeTextFile(file, text); !written)
    {
        error = std::format("Not saved: {}", written.error().message);
        return false;
    }
    time = core::lastWriteTime(file);
    error.clear();
    if (auto queued = state.database->reimport(asset); !queued)
        DEVEX_LOG_WARNING("{}", queued.error());
    return true;
}

} // namespace devex::tools::detail
