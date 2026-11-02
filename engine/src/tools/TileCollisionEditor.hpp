#pragma once

#include "EditorUi.hpp"

#include <devex/asset/TilesetData.hpp>

#include <array>
#include <optional>
#include <vector>

namespace devex::tools::detail
{

struct InspectorUi;
struct Section;
struct ToolsState;

// An outline edited over a tile preview. Invalid and unfinished drawings stay in a draft until
// Apply; the tile and its running physics keep the last valid outline.
class TileCollisionEditor
{
  public:
    void build(InspectorUi& ui, EditorUiKit& kit, Section& section, asset::AssetId asset,
               const asset::TileData& tile);
    void sync(InspectorUi& ui, ToolsState& state, const asset::TileData& tile);
    [[nodiscard]] bool answer(InspectorUi& ui, const ToolsState& state, asset::TileData& tile);

  private:
    struct Draft
    {
        std::vector<math::Vec2> points;
        bool closed = true;
        bool oneWay = false;
        bool operator==(const Draft&) const = default;
    };
    void remember();
    void restore(const asset::TileData& tile);
    [[nodiscard]] bool changed() const;

    asset::AssetId m_asset;
    std::uint32_t m_tile = 0;
    Draft m_draft;
    Draft m_saved;
    std::vector<Draft> m_undo;
    std::vector<Draft> m_redo;
    std::optional<std::size_t> m_drag;
    std::optional<std::size_t> m_selected;
    bool m_snapPixels = true;
    float m_side = 200.0f;
    math::Vec2 m_pixels{16.0f};
    scene::Entity m_preview;
    scene::Entity m_canvas;
    scene::Entity m_image;
    scene::Entity m_outline;
    std::array<scene::Entity, 14> m_grid{};
    std::array<scene::Entity, asset::maxTileCollisionPoints> m_handles{};
    scene::Entity m_oneWay;
    scene::Entity m_snap;
    scene::Entity m_status;
    PanelButton m_draw;
    PanelButton m_rectangle;
    PanelButton m_finish;
    PanelButton m_apply;
    PanelButton m_revert;
    PanelButton m_undoButton;
    PanelButton m_redoButton;
};

} // namespace devex::tools::detail
