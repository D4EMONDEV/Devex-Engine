#pragma once

#include "EditorUi.hpp"

#include <devex/asset/SpriteData.hpp>
#include <devex/asset/TilesetData.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace devex::tools::detail
{

struct FormUi;
struct Section;
struct ToolsState;

struct TileAtlasRegion
{
    std::uint32_t tile = 0;
    asset::AssetId sprite;
    math::Vec2 min{0};
    math::Vec2 max{0};
};

// Original texture coordinates, including gaps and tiles that were left out of the tileset.
struct DEVEX_API TileAtlasSource
{
    asset::AssetId texture;
    math::Vec2 pixels{0};
    std::vector<TileAtlasRegion> regions;

    // Half-open rectangles: a shared edge belongs to the tile on its right or below it.
    [[nodiscard]] std::uint32_t tileAt(math::Vec2 pixel) const noexcept;
};

using PaletteSpriteSource = std::function<std::shared_ptr<const asset::SpriteData>(asset::AssetId)>;
[[nodiscard]] DEVEX_API std::vector<TileAtlasSource>
makeTileAtlases(std::span<const asset::TileData> tiles, const PaletteSpriteSource& sprites);

// A pixel-accurate view of an atlas. Zoom keeps the pixel under the pointer still; panning is
// bounded so the image cannot be lost outside the inspector.
struct DEVEX_API TileAtlasView
{
    math::Vec2 pixels{1};
    math::Vec2 viewport{1};
    math::Vec2 offset{0};
    float scale = 1;
    bool fitted = true;

    void fit() noexcept;
    void resize(math::Vec2 size) noexcept;
    void zoom(float factor, math::Vec2 at) noexcept;
    void pan(math::Vec2 delta) noexcept;
    [[nodiscard]] math::Vec2 pixelAt(math::Vec2 at) const noexcept;

  private:
    void clamp() noexcept;
};

// One continuous source image in the Paint group, rather than a rearranged grid of thumbnails.
class TileAtlasPalette
{
  public:
    void build(FormUi& ui, EditorUiKit& kit, Section& section);
    void show(FormUi& ui, Section& section, bool visible);
    void sync(FormUi& ui, Section& section, const ToolsState& state,
              const std::shared_ptr<const asset::TilesetData>& tileset, std::uint32_t tile);
    [[nodiscard]] std::optional<std::uint32_t> answer(FormUi& ui, ToolsState& state);

  private:
    [[nodiscard]] const TileAtlasSource* source() const noexcept;
    void place(FormUi& ui, std::uint32_t tile);

    std::vector<TileAtlasSource> m_sources;
    std::shared_ptr<const asset::TilesetData> m_tileset;
    std::uint64_t m_revision = 0;
    double m_retryAt = 0;
    bool m_missing = false;
    std::size_t m_source = 0;
    std::uint32_t m_lastTile = 0;
    std::optional<std::uint32_t> m_hovered;
    TileAtlasView m_view;
    std::optional<Mouse> m_panning;
    math::Vec2 m_lastPointer{0};
    std::vector<scene::Entity> m_lines;
    scene::Entity m_sourceRow;
    scene::Entity m_sourceChoice;
    scene::Entity m_toolbar;
    scene::Entity m_percent;
    scene::Entity m_pane;
    scene::Entity m_sheet;
    scene::Entity m_image;
    scene::Entity m_selected;
    scene::Entity m_hover;
    scene::Entity m_tileChoice;
    std::vector<scene::Entity> m_gridLines;
    PanelButton m_zoomOut;
    PanelButton m_zoomIn;
    PanelButton m_fit;
    PanelButton m_grid;
    bool m_showGrid = false;
};

} // namespace devex::tools::detail
