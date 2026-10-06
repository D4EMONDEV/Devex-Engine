#include <devex/scene/TileTerrain.hpp>

#include <algorithm>
#include <array>
#include <climits>
#include <unordered_map>
#include <unordered_set>

namespace devex::scene {
namespace {

using asset::noTerrain;
using asset::TileNeighbor;
using asset::tileNeighborCount;

// Sides and corners read across a mirror: the right of a tile mirrored across is its left.
constexpr std::array<std::size_t, tileNeighborCount> acrossX{4, 3, 2, 1, 0, 7, 6, 5};
constexpr std::array<std::size_t, tileNeighborCount> acrossY{0, 7, 6, 5, 4, 3, 2, 1};

// How much a terrain asked for weighs against the tile chosen: the cells painted, then the tiles
// chosen already, then what the cells around name.
constexpr int paintedWeight = 10;
constexpr int chosenWeight = 5;
constexpr int aroundWeight = 1;

[[nodiscard]] std::uint64_t keyOf(math::IVec2 point) noexcept
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(point.x)) << 32) | static_cast<std::uint32_t>(point.y);
}

// Where cells meet, in half cells: the middle of cell (x, y) is (2x + 1, 2y + 1), and its sides and
// corners lie one step around it, shared with the cells across.
[[nodiscard]] math::IVec2 middleOf(math::IVec2 cell) noexcept
{
    return cell * 2 + math::IVec2{1};
}

[[nodiscard]] math::IVec2 pointOf(math::IVec2 cell, std::size_t neighbor) noexcept
{
    return middleOf(cell) + asset::neighborOffset(static_cast<TileNeighbor>(neighbor));
}

[[nodiscard]] TerrainPattern mirrored(TerrainPattern pattern, bool x, bool y) noexcept
{
    TerrainPattern result = pattern;
    for (std::size_t index = 0; index < tileNeighborCount; ++index)
    {
        std::size_t from = index;
        from = x ? acrossX[from] : from;
        from = y ? acrossY[from] : from;
        result.bits[index] = pattern.bits[from];
    }
    return result;
}

[[nodiscard]] TerrainPattern patternOf(const asset::TileData& tile, const asset::TerrainSetData& set) noexcept
{
    TerrainPattern pattern{.terrain = tile.terrain, .bits = tile.terrainBits};
    for (std::size_t index = 0; index < tileNeighborCount; ++index)
    {
        if (!asset::matchesNeighbor(set.mode, static_cast<TileNeighbor>(index)))
        {
            pattern.bits[index] = noTerrain;
        }
    }
    return pattern;
}

// A pattern the tiles of the set can show, and the cells that show it: a tile, mirrored or not, with
// its probability.
struct Variant
{
    std::uint16_t cell = 0;
    float probability = 1.0f;
};

struct Candidate
{
    TerrainPattern pattern;
    std::vector<Variant> variants;
    // Whether a tile shows it unmirrored: mirrored tiles only fill the patterns no tile draws.
    bool drawn = false;
};

[[nodiscard]] std::vector<Candidate> candidatesOf(const asset::TilesetData& tileset, std::int32_t terrainSet,
                                                  const asset::TerrainSetData& set)
{
    std::vector<Candidate> candidates;
    const auto add = [&](TerrainPattern pattern, std::uint16_t cell, float probability, bool mirror) {
        auto found = std::ranges::find(candidates, pattern, &Candidate::pattern);
        if (found == candidates.end())
        {
            candidates.push_back(Candidate{.pattern = pattern});
            found = candidates.end() - 1;
        }
        if (mirror && found->drawn)
        {
            return;
        }
        found->drawn = found->drawn || !mirror;
        found->variants.push_back({cell, probability});
    };
    std::array<std::pair<bool, bool>, 4> mirrors{{{false, false}, {true, false}, {false, true}, {true, true}}};
    for (const auto& [x, y] : mirrors)
    {
        if ((x && !set.mirrorX) || (y && !set.mirrorY))
        {
            continue;
        }
        for (const asset::TileData& tile : tileset.tiles)
        {
            const TerrainPattern pattern = patternOf(tile, set);
            if (tile.terrainSet != terrainSet || pattern == TerrainPattern{})
            {
                continue;
            }
            const auto cell = static_cast<std::uint16_t>((tile.id & tileIdMask) | (x ? tileFlipX : 0) | (y ? tileFlipY : 0));
            add(mirrored(pattern, x, y), cell, tile.probability, x || y);
        }
    }
    // Emptiness is a pattern too: what cells become when nothing of the terrain reaches them.
    if (std::ranges::find(candidates, TerrainPattern{}, &Candidate::pattern) == candidates.end())
    {
        candidates.push_back(Candidate{.pattern = {}, .variants = {{0, 1.0f}}, .drawn = true});
    }
    return candidates;
}

// The same cell always draws the same variant, so that painting again changes nothing.
[[nodiscard]] std::uint16_t pickVariant(const Candidate& candidate, math::IVec2 cell) noexcept
{
    if (candidate.variants.size() == 1)
    {
        return candidate.variants.front().cell;
    }
    float total = 0.0f;
    for (const Variant& variant : candidate.variants)
    {
        total += variant.probability;
    }
    if (!(total > 0.0f))
    {
        return candidate.variants.front().cell;
    }
    std::uint64_t mixed = keyOf(cell) + 0x9E3779B97F4A7C15ull;
    mixed = (mixed ^ (mixed >> 30)) * 0xBF58476D1CE4E5B9ull;
    mixed = (mixed ^ (mixed >> 27)) * 0x94D049BB133111EBull;
    mixed ^= mixed >> 31;
    float drawn = static_cast<float>(mixed >> 40) / static_cast<float>(1u << 24) * total;
    for (const Variant& variant : candidate.variants)
    {
        drawn -= variant.probability;
        if (drawn < 0.0f)
        {
            return variant.cell;
        }
    }
    return candidate.variants.back().cell;
}

struct Constraint
{
    std::int32_t terrain = noTerrain;
    int weight = 0;
};

} // namespace

std::optional<TerrainPattern> terrainPattern(const asset::TilesetData& tileset, std::int32_t terrainSet, std::uint16_t cell)
{
    if (cell == 0)
    {
        return TerrainPattern{};
    }
    const asset::TerrainSetData* const set = tileset.terrainSet(terrainSet);
    const asset::TileData* const tile = tileset.find(tileIdOf(cell));
    if (set == nullptr || tile == nullptr || tile->terrainSet != terrainSet)
    {
        return std::nullopt;
    }
    return mirrored(patternOf(*tile, *set), (cell & tileFlipX) != 0, (cell & tileFlipY) != 0);
}

std::pair<std::int32_t, std::int32_t> terrainOf(const asset::TilesetData& tileset, std::uint16_t cell)
{
    const asset::TileData* const tile = cell != 0 ? tileset.find(tileIdOf(cell)) : nullptr;
    if (tile == nullptr || tileset.terrainSet(tile->terrainSet) == nullptr)
    {
        return {noTerrain, noTerrain};
    }
    return {tile->terrainSet, tile->terrain};
}

std::vector<math::IVec2> paintTerrain(TileGrid& grid, const asset::TilesetData& tileset, std::span<const math::IVec2> cells,
                                      std::int32_t terrainSet, std::int32_t terrain, TerrainPaint mode)
{
    const asset::TerrainSetData* const set = tileset.terrainSet(terrainSet);
    if (set == nullptr || cells.empty() || (terrain != noTerrain && tileset.terrain(terrainSet, terrain) == nullptr))
    {
        return {};
    }
    const std::vector<Candidate> candidates = candidatesOf(tileset, terrainSet, *set);
    if (terrain != noTerrain &&
        std::ranges::none_of(candidates, [&](const Candidate& candidate) { return candidate.pattern.terrain == terrain; }))
    {
        return {};
    }
    std::array<bool, tileNeighborCount> matched{};
    for (std::size_t index = 0; index < tileNeighborCount; ++index)
    {
        matched[index] = asset::matchesNeighbor(set->mode, static_cast<TileNeighbor>(index));
    }

    // The cells painted, once each, then those around them, which may change to match.
    std::vector<math::IVec2> order;
    std::unordered_set<std::uint64_t> painted;
    for (const math::IVec2 cell : cells)
    {
        if (painted.insert(keyOf(cell)).second)
        {
            order.push_back(cell);
        }
    }
    const std::size_t paintedCount = order.size();
    std::unordered_set<std::uint64_t> listed = painted;
    for (std::size_t index = 0; index < paintedCount; ++index)
    {
        for (std::size_t neighbor = 0; neighbor < tileNeighborCount; ++neighbor)
        {
            const math::IVec2 next = order[index] + asset::neighborOffset(static_cast<TileNeighbor>(neighbor));
            if (listed.insert(keyOf(next)).second)
            {
                order.push_back(next);
            }
        }
    }
    // What each of them shows before the change.
    std::unordered_map<std::uint64_t, std::optional<TerrainPattern>> before;
    for (const math::IVec2 cell : order)
    {
        before.emplace(keyOf(cell), terrainPattern(tileset, terrainSet, grid.at(cell)));
    }
    const auto patternBefore = [&](math::IVec2 cell) -> const std::optional<TerrainPattern>& { return before.at(keyOf(cell)); };

    // What the change asks of the points where the cells meet; the first demand of a point stays.
    std::unordered_map<std::uint64_t, Constraint> constraints;
    const auto demand = [&](math::IVec2 point, std::int32_t value, int weight) {
        constraints.try_emplace(keyOf(point), Constraint{value, weight});
    };
    for (std::size_t index = 0; index < paintedCount; ++index)
    {
        demand(middleOf(order[index]), terrain, paintedWeight);
    }
    if (terrain == noTerrain)
    {
        // An emptied cell is empty all around: the cells around lose what joined them to it.
        for (std::size_t index = 0; index < paintedCount; ++index)
        {
            for (std::size_t neighbor = 0; neighbor < tileNeighborCount; ++neighbor)
            {
                if (matched[neighbor])
                {
                    demand(pointOf(order[index], neighbor), noTerrain, paintedWeight);
                }
            }
        }
    }
    else if (mode == TerrainPaint::Connect)
    {
        // The cells painted join the cells painted and those of the terrain around them: across a
        // side when the cell across joins, at a corner when the three cells there join.
        const auto joins = [&](math::IVec2 cell) {
            const std::optional<TerrainPattern>& pattern = patternBefore(cell);
            return painted.contains(keyOf(cell)) || (pattern && pattern->terrain == terrain);
        };
        for (std::size_t index = 0; index < paintedCount; ++index)
        {
            const math::IVec2 cell = order[index];
            for (std::size_t neighbor = 0; neighbor < tileNeighborCount; ++neighbor)
            {
                const math::IVec2 offset = asset::neighborOffset(static_cast<TileNeighbor>(neighbor));
                const bool corner = neighbor % 2 != 0;
                if (matched[neighbor] && joins(cell + offset) &&
                    (!corner || (joins(cell + math::IVec2{offset.x, 0}) && joins(cell + math::IVec2{0, offset.y}))))
                {
                    demand(pointOf(cell, neighbor), terrain, paintedWeight);
                }
            }
        }
    }
    else
    {
        // A path joins each cell to the one after it, when they touch.
        for (std::size_t index = 1; index < cells.size(); ++index)
        {
            const math::IVec2 step = cells[index] - cells[index - 1];
            for (std::size_t neighbor = 0; neighbor < tileNeighborCount; ++neighbor)
            {
                if (matched[neighbor] && asset::neighborOffset(static_cast<TileNeighbor>(neighbor)) == step)
                {
                    demand(pointOf(cells[index - 1], neighbor), terrain, paintedWeight);
                }
            }
        }
    }
    // The other sides and corners of the cells painted take the terrain most cells meeting there
    // name; on a tie, the one a cell left as it is names, then a terrain rather than none.
    for (std::size_t index = 0; index < paintedCount; ++index)
    {
        for (std::size_t neighbor = 0; neighbor < tileNeighborCount; ++neighbor)
        {
            const math::IVec2 point = pointOf(order[index], neighbor);
            if (!matched[neighbor] || constraints.contains(keyOf(point)))
            {
                continue;
            }
            struct Tally
            {
                std::int32_t terrain;
                int count;
                bool kept;
            };
            std::vector<Tally> tallies;
            for (std::size_t from = 0; from < tileNeighborCount; ++from)
            {
                const math::IVec2 twice = point - asset::neighborOffset(static_cast<TileNeighbor>(from)) - math::IVec2{1};
                if ((twice.x & 1) != 0 || (twice.y & 1) != 0)
                {
                    continue;
                }
                const math::IVec2 cell = twice / 2;
                const std::optional<TerrainPattern>& pattern = patternBefore(cell);
                const std::int32_t named = pattern ? pattern->bits[from] : noTerrain;
                auto tally = std::ranges::find(tallies, named, &Tally::terrain);
                if (tally == tallies.end())
                {
                    tallies.push_back({named, 0, false});
                    tally = tallies.end() - 1;
                }
                ++tally->count;
                tally->kept = tally->kept || !painted.contains(keyOf(cell));
            }
            const auto rank = [](const Tally& tally) {
                return tally.count * 4 + (tally.kept ? 2 : 0) + (tally.terrain != noTerrain ? 1 : 0);
            };
            const Tally* best = nullptr;
            for (const Tally& tally : tallies)
            {
                if (best == nullptr || rank(tally) > rank(*best) || (rank(tally) == rank(*best) && tally.terrain < best->terrain))
                {
                    best = &tally;
                }
            }
            if (best != nullptr)
            {
                demand(point, best->terrain, aroundWeight);
            }
        }
    }

    // Each cell in turn takes the pattern that breaks the lightest demands, keeping what nothing
    // asks of; what it takes binds the cells after it.
    std::vector<std::pair<math::IVec2, const Candidate*>> chosen;
    for (std::size_t index = 0; index < order.size(); ++index)
    {
        const math::IVec2 cell = order[index];
        const std::optional<TerrainPattern>& current = patternBefore(cell);
        if (!current && index >= paintedCount)
        {
            // The tile of another set, or of none, stays.
            continue;
        }
        const TerrainPattern kept = current.value_or(TerrainPattern{});
        const Candidate* best = nullptr;
        int bestScore = INT_MAX;
        for (const Candidate& candidate : candidates)
        {
            int score = 0;
            bool valid = true;
            const auto check = [&](math::IVec2 point, std::int32_t shown, std::int32_t previous) {
                const auto found = constraints.find(keyOf(point));
                if (found != constraints.end())
                {
                    score += found->second.terrain != shown ? found->second.weight : 0;
                }
                else if (shown != previous)
                {
                    valid = false;
                }
            };
            check(middleOf(cell), candidate.pattern.terrain, kept.terrain);
            for (std::size_t neighbor = 0; neighbor < tileNeighborCount && valid; ++neighbor)
            {
                if (matched[neighbor])
                {
                    check(pointOf(cell, neighbor), candidate.pattern.bits[neighbor], kept.bits[neighbor]);
                }
            }
            if (valid && (score < bestScore || (score == bestScore && current && candidate.pattern == *current)))
            {
                best = &candidate;
                bestScore = score;
            }
        }
        if (best == nullptr)
        {
            continue;
        }
        constraints[keyOf(middleOf(cell))] = {best->pattern.terrain, chosenWeight};
        for (std::size_t neighbor = 0; neighbor < tileNeighborCount; ++neighbor)
        {
            if (matched[neighbor])
            {
                constraints[keyOf(pointOf(cell, neighbor))] = {best->pattern.bits[neighbor], chosenWeight};
            }
        }
        chosen.emplace_back(cell, best);
    }

    // A cell whose tile shows the pattern already keeps it, the variant chosen by hand too.
    std::vector<math::IVec2> changed;
    for (const auto& [cell, candidate] : chosen)
    {
        const std::optional<TerrainPattern>& current = patternBefore(cell);
        if (current && *current == candidate->pattern)
        {
            continue;
        }
        const std::uint16_t value = pickVariant(*candidate, cell);
        if (value != grid.at(cell))
        {
            grid.set(cell, value);
            changed.push_back(cell);
        }
    }
    return changed;
}

std::vector<math::IVec2> paintTerrain(Tilemap& tilemap, const asset::TilesetData& tileset, std::span<const math::IVec2> cells,
                                      std::int32_t terrainSet, std::int32_t terrain, TerrainPaint mode)
{
    TileGrid grid = TileGrid::read(tilemap);
    std::vector<math::IVec2> changed = paintTerrain(grid, tileset, cells, terrainSet, terrain, mode);
    if (!changed.empty())
    {
        grid.write(tilemap);
    }
    return changed;
}

} // namespace devex::scene
