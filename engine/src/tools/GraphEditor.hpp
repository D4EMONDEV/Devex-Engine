#pragma once

#include <devex/asset/AssetId.hpp>
#include <devex/core/Export.hpp>
#include <devex/math/Math.hpp>
#include <devex/scene/Entity.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace devex::scene
{
class Scene;
}

namespace devex::tools::detail
{

struct ToolsState;

// A completed edit remembers the last saved snapshot; a new edit discards the redo branch.
template <typename T> class GraphHistory
{
  public:
    void record(const T& previous)
    {
        m_undo.push_back(previous);
        if (m_undo.size() > 200)
            m_undo.erase(m_undo.begin());
        m_redo.clear();
    }

    bool undo(T& current) { return restore(current, m_undo, m_redo); }
    bool redo(T& current) { return restore(current, m_redo, m_undo); }
    [[nodiscard]] bool canUndo() const noexcept { return !m_undo.empty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !m_redo.empty(); }

    void clear()
    {
        m_undo.clear();
        m_redo.clear();
    }

  private:
    std::vector<T> m_undo;
    std::vector<T> m_redo;

    static bool restore(T& current, std::vector<T>& from, std::vector<T>& to)
    {
        if (from.empty())
            return false;
        to.push_back(current);
        current = std::move(from.back());
        from.pop_back();
        return true;
    }
};

// Graph coordinates use the same pan/zoom convention in both node editors.
struct GraphView
{
    math::Vec2 origin{0.0f};
    float scale = 1.0f;

    [[nodiscard]] math::Vec2 toArea(math::Vec2 point) const noexcept
    {
        return origin + point * scale;
    }
    [[nodiscard]] math::Vec2 toGraph(math::Vec2 point) const noexcept
    {
        return (point - origin) / scale;
    }
};

DEVEX_API void syncGraphGrid(scene::Scene& scene, scene::Entity layer,
                             std::vector<scene::Entity>& lines, const GraphView& view,
                             math::Vec2 size, math::Vec4 color);

// Writes a graph document and queues its import. Each panel owns validation and its saved snapshot.
[[nodiscard]] bool saveGraphAsset(ToolsState& state, asset::AssetId asset,
                                  const std::filesystem::path& file, std::string_view text,
                                  std::filesystem::file_time_type& time, std::string& error);

} // namespace devex::tools::detail
