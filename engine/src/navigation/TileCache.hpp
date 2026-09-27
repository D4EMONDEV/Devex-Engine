#pragma once

// The navigation mesh of a baked asset, as Detour holds it: the tile cache that keeps the layers of
// the tiles and cuts obstacles out of them, the mesh it builds, and a query on that mesh. Shared by
// the world agents walk and the lines the editor draws.

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>

#include <devex/asset/NavMeshData.hpp>
#include <devex/core/Error.hpp>

#include <memory>

namespace devex::navigation::detail {

// Layers are compressed with zstd.
struct ZstdCompressor final : dtTileCacheCompressor
{
    int maxCompressedSize(int bufferSize) override;
    dtStatus compress(const unsigned char* buffer, int bufferSize, unsigned char* compressed, int maxCompressedSize,
                      int* compressedSize) override;
    dtStatus decompress(const unsigned char* compressed, int compressedSize, unsigned char* buffer, int maxBufferSize,
                        int* bufferSize) override;
};

// Every polygon walked: one area, and the flag the default filter asks for.
struct WalkableProcess final : dtTileCacheMeshProcess
{
    void process(dtNavMeshCreateParams* params, unsigned char* polyAreas, unsigned short* polyFlags) override;
};

inline constexpr unsigned short walkableFlag = 1;

class TileCache
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<TileCache>> create(const asset::NavMeshData& navMesh, int maxObstacles);
    ~TileCache();

    TileCache(const TileCache&) = delete;
    TileCache& operator=(const TileCache&) = delete;

    [[nodiscard]] dtTileCache& tiles() noexcept
    {
        return *m_tileCache;
    }

    [[nodiscard]] dtNavMesh& mesh() noexcept
    {
        return *m_navMesh;
    }

    [[nodiscard]] const dtNavMesh& mesh() const noexcept
    {
        return *m_navMesh;
    }

    [[nodiscard]] const dtNavMeshQuery& query() const noexcept
    {
        return *m_query;
    }

private:
    TileCache() = default;

    dtTileCacheAlloc m_allocator;
    ZstdCompressor m_compressor;
    WalkableProcess m_process;
    dtTileCache* m_tileCache = nullptr;
    dtNavMesh* m_navMesh = nullptr;
    dtNavMeshQuery* m_query = nullptr;
};

} // namespace devex::navigation::detail
