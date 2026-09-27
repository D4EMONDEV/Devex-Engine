#include "TileCache.hpp"

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMeshBuilder.h>

#include <zstd.h>

#include <algorithm>
#include <cstring>

namespace devex::navigation::detail {

int ZstdCompressor::maxCompressedSize(int bufferSize)
{
    return static_cast<int>(ZSTD_compressBound(static_cast<std::size_t>(bufferSize)));
}

dtStatus ZstdCompressor::compress(const unsigned char* buffer, int bufferSize, unsigned char* compressed, int maxCompressedSize,
                                  int* compressedSize)
{
    const std::size_t written = ZSTD_compress(compressed, static_cast<std::size_t>(maxCompressedSize), buffer,
                                              static_cast<std::size_t>(bufferSize), 3);
    if (ZSTD_isError(written) != 0)
    {
        return DT_FAILURE;
    }
    *compressedSize = static_cast<int>(written);
    return DT_SUCCESS;
}

dtStatus ZstdCompressor::decompress(const unsigned char* compressed, int compressedSize, unsigned char* buffer, int maxBufferSize,
                                    int* bufferSize)
{
    const std::size_t read = ZSTD_decompress(buffer, static_cast<std::size_t>(maxBufferSize), compressed,
                                             static_cast<std::size_t>(compressedSize));
    if (ZSTD_isError(read) != 0)
    {
        return DT_FAILURE;
    }
    *bufferSize = static_cast<int>(read);
    return DT_SUCCESS;
}

void WalkableProcess::process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags)
{
    for (int polygon = 0; polygon < params->polyCount; ++polygon)
    {
        if (polyAreas[polygon] == DT_TILECACHE_WALKABLE_AREA)
        {
            polyAreas[polygon] = 0;
            polyFlags[polygon] = walkableFlag;
        }
        else
        {
            polyFlags[polygon] = 0;
        }
    }
}

core::Result<std::unique_ptr<TileCache>> TileCache::create(const asset::NavMeshData& navMesh, int maxObstacles)
{
    const asset::NavMeshBuildSettings& settings = navMesh.settings;
    std::unique_ptr<TileCache> cache(new TileCache());

    // A tile holds a few layers, one per floor stacked over it.
    const int tiles = std::max(navMesh.tilesX * navMesh.tilesZ, 1);
    const int layers = std::max(static_cast<int>(navMesh.layers.size()), tiles);
    dtTileCacheParams cacheParams{};
    dtVcopy(cacheParams.orig, &navMesh.origin.x);
    cacheParams.cs = settings.cellSize;
    cacheParams.ch = settings.cellHeight;
    cacheParams.width = settings.tileSize;
    cacheParams.height = settings.tileSize;
    cacheParams.walkableHeight = settings.agentHeight;
    cacheParams.walkableRadius = settings.agentRadius;
    cacheParams.walkableClimb = settings.agentMaxClimb;
    cacheParams.maxSimplificationError = 1.3f;
    cacheParams.maxTiles = static_cast<int>(dtNextPow2(static_cast<unsigned int>(layers * 2)));
    cacheParams.maxObstacles = maxObstacles;
    cache->m_tileCache = dtAllocTileCache();
    if (cache->m_tileCache == nullptr ||
        dtStatusFailed(cache->m_tileCache->init(&cacheParams, &cache->m_allocator, &cache->m_compressor, &cache->m_process)))
    {
        return core::makeError(core::ErrorCode::InvalidState, "the tile cache of the navigation mesh could not be made");
    }

    // Polygon references share 22 bits between the tile and the polygon.
    const int tileBits = std::min(static_cast<int>(dtIlog2(dtNextPow2(static_cast<unsigned int>(layers * 2)))), 14);
    dtNavMeshParams meshParams{};
    dtVcopy(meshParams.orig, &navMesh.origin.x);
    meshParams.tileWidth = static_cast<float>(settings.tileSize) * settings.cellSize;
    meshParams.tileHeight = meshParams.tileWidth;
    meshParams.maxTiles = 1 << tileBits;
    meshParams.maxPolys = 1 << (22 - tileBits);
    cache->m_navMesh = dtAllocNavMesh();
    if (cache->m_navMesh == nullptr || dtStatusFailed(cache->m_navMesh->init(&meshParams)))
    {
        return core::makeError(core::ErrorCode::InvalidState, "the navigation mesh could not be made");
    }
    cache->m_query = dtAllocNavMeshQuery();
    if (cache->m_query == nullptr || dtStatusFailed(cache->m_query->init(cache->m_navMesh, 4096)))
    {
        return core::makeError(core::ErrorCode::InvalidState, "the navigation mesh cannot be queried");
    }

    for (const std::vector<std::byte>& layer : navMesh.layers)
    {
        // The tile cache frees what it is given with dtFree.
        auto* const data = static_cast<unsigned char*>(dtAlloc(static_cast<int>(layer.size()), DT_ALLOC_PERM));
        std::memcpy(data, layer.data(), layer.size());
        if (dtStatusFailed(cache->m_tileCache->addTile(data, static_cast<int>(layer.size()), DT_COMPRESSEDTILE_FREE_DATA, nullptr)))
        {
            dtFree(data);
            return core::makeError(core::ErrorCode::Parse, "a tile of the navigation mesh cannot be read");
        }
    }
    for (int z = 0; z < navMesh.tilesZ; ++z)
    {
        for (int x = 0; x < navMesh.tilesX; ++x)
        {
            static_cast<void>(cache->m_tileCache->buildNavMeshTilesAt(x, z, cache->m_navMesh));
        }
    }
    return cache;
}

TileCache::~TileCache()
{
    dtFreeNavMeshQuery(m_query);
    dtFreeTileCache(m_tileCache);
    dtFreeNavMesh(m_navMesh);
}

} // namespace devex::navigation::detail
