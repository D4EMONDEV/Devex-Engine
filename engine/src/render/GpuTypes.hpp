#pragma once

// CPU mirrors of the structures the shaders read that are only data, the same for every backend: Vulkan, and WebGPU to
// come. Every member sits at the same offset in the C++ and Slang layouts, which the static assertions keep true. Those
// that hold addresses of the GPU, which only Vulkan has, stay in vulkan/GpuData.hpp.
#include <devex/math/Math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace devex::render {

// The value of a texture slot that a decal does not have.
inline constexpr std::uint32_t noDecalTexture = 0xFFFFFFFFu;

inline constexpr std::uint32_t noParticleTexture = 0xFFFFFFFFU;

struct GpuVertex
{
    math::Vec3 position{0.0f};
    float u = 0.0f;
    math::Vec3 normal{0.0f};
    float v = 0.0f;
    math::Vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

// Texture fields are indices into the bindless texture array.
struct GpuMaterial
{
    math::Vec4 baseColorFactor{1.0f};
    math::Vec3 emissiveFactor{0.0f};
    float alphaCutoff = 0.5f;
    std::uint32_t baseColorTexture = 0;
    std::uint32_t metallicRoughnessTexture = 0;
    std::uint32_t normalTexture = 0;
    std::uint32_t occlusionTexture = 0;
    std::uint32_t emissiveTexture = 0;
    float metallicFactor = 0.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    // 0 opaque, 1 mask, 2 blend.
    std::uint32_t alphaMode = 0;
    std::uint32_t doubleSided = 0;
    // Where the uniforms of its shader start in GpuSceneData::materialParameters.
    std::uint32_t parameterOffset = 0;
};

struct GpuLight
{
    math::Vec3 position{0.0f};
    float range = 0.0f;
    math::Vec3 direction{0.0f, 0.0f, -1.0f};
    float spotScale = 0.0f;
    math::Vec3 intensity{0.0f};
    float spotOffset = 1.0f;
    // The first view of the shadow atlas this light owns, or -1 when it casts none. A point light
    // owns the six that follow, one per face of the cube around it.
    std::int32_t firstShadowView = -1;
    // How far the light reaches, which its views project between the near plane and it.
    float shadowFar = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

// One view of the shadow atlas: where it looks from, and the part of the atlas it was given.
struct GpuShadowView
{
    math::Mat4 viewProjection{1.0f};
    // Offset and size in the atlas, in texture coordinates.
    math::Vec4 tile{0.0f};
};

// A reflection probe of the frame, in the space of its box.
struct GpuReflectionProbe
{
    // From the world into the box, in meters along its axes, from its center.
    math::Mat4 worldToBox{1.0f};
    math::Vec3 halfSize{0.0f};
    float blendDistance = 0.0f;
    // Where it was captured from, in the box.
    math::Vec3 capturePosition{0.0f};
    // Times the values of its map, which turns them into nits.
    float intensity = 1.0f;
    // Its slot among the probe maps of the global set.
    std::uint32_t map = 0;
    std::uint32_t boxProjection = 0;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

struct GpuCluster
{
    std::uint32_t offset = 0;
    std::uint32_t count = 0;
};

// A fog volume of the frame.
struct GpuFogVolume
{
    // From the world into the volume: -1 to 1 along its axes.
    math::Mat4 worldToVolume{1.0f};
    math::Vec3 albedo{1.0f};
    float density = 0.0f;
    math::Vec3 emission{0.0f};
    float heightFalloff = 0.0f;
    // 0 a box, 1 an ellipsoid, 2 everywhere.
    std::uint32_t shape = 0;
    float edgeFade = 0.1f;
    // The height of its centre in the world, from which it thins out going up.
    float centerHeight = 0.0f;
    float padding = 0.0f;
};

// A decal of the frame, as shaders/common.slang reads it.
struct GpuDecal
{
    // From the world into its box: X and Z from -1 to 1 across it, Y from -1 at its bottom to 1 at
    // its top, the side it projects from.
    math::Mat4 worldToDecal{1.0f};
    // Its X and Z in the world, normalized, along which its normal map tilts; and its Y, towards
    // which it faces.
    math::Vec4 axisX{1.0f, 0.0f, 0.0f, 0.0f};
    math::Vec4 axisZ{0.0f, 0.0f, 1.0f, 0.0f};
    math::Vec4 axisY{0.0f, 1.0f, 0.0f, 0.0f};
    math::Vec4 modulate{1.0f};
    std::uint32_t albedoTexture = noDecalTexture;
    std::uint32_t normalTexture = noDecalTexture;
    std::uint32_t ormTexture = noDecalTexture;
    std::uint32_t emissionTexture = noDecalTexture;
    float albedoMix = 1.0f;
    float emissionEnergy = 1.0f;
    float normalFade = 0.0f;
    float upperFade = 0.3f;
    float lowerFade = 0.3f;
    float fadeBegin = 0.0f;
    float fadeLength = 0.0f;
    float padding = 0.0f;
};

// The air of the frame, in kilometers and per kilometer, with the sun above it and the camera in it,
// and the fog, in meters. Read by the sky, by the surfaces it hazes, and by the passes that fill the
// tables of the atmosphere.
struct GpuAtmosphere
{
    float bottomRadius = 6360.0f;
    float topRadius = 6460.0f;
    float rayleighHeight = 8.0f;
    float mieHeight = 1.2f;
    math::Vec3 rayleighScattering{0.0f};
    float mieScattering = 0.0f;
    math::Vec3 ozoneAbsorption{0.0f};
    float mieAbsorption = 0.0f;
    math::Vec3 groundAlbedo{0.0f};
    float mieAnisotropy = 0.8f;
    // Above the air, in lux, times the intensity of the sky; and the cosine of the radius of its disk.
    math::Vec3 sunIlluminance{0.0f};
    float sunCosRadius = 1.0f;
    // Towards the sun, and how far the camera stands from the center of the planet.
    math::Vec3 sunDirection{0.0f, 1.0f, 0.0f};
    float cameraRadius = 6360.0f;
    float sunDiskIntensity = 1.0f;
    // Kilometers of haze per kilometer of the scene, and the farthest the haze is kept.
    float aerialScale = 1.0f;
    float aerialDistance = 32.0f;
    float padding0 = 0.0f;
    math::Vec3 fogColor{0.0f};
    float fogDensity = 0.0f;
    float fogHeight = 0.0f;
    float fogFalloff = 1.0f;
    float fogStart = 0.0f;
    float fogMaxOpacity = 1.0f;
    float fogSunScattering = 0.0f;
    float fogSunAnisotropy = 0.0f;
    float fogSkyBlend = 0.0f;
    float padding1 = 0.0f;
};

// How a vertex of a skinned mesh follows its bones, beside the vertex itself.
struct GpuVertexSkin
{
    std::array<std::uint32_t, 4> joints{};
    math::Vec4 weights{0.0f};
};

// A terrain of the frame, as shaders/terrain.slang reads it.
struct GpuTerrain
{
    math::Vec3 origin{0.0f};
    float size = 1.0f;
    std::uint32_t heights = 0;
    std::uint32_t slopes = 0;
    std::uint32_t paint0 = 0;
    std::uint32_t paint1 = 0;
    std::uint32_t detail = 0;
    std::uint32_t resolution = 0;
    std::uint32_t layerCount = 0;
    std::uint32_t objectId = 0;
    // The materials of its layers.
    std::array<std::uint32_t, 8> layers{};
    float textureSize = 4.0f;
    float blendSharpness = 0.5f;
    std::uint32_t triplanar = 1;
    std::int32_t autoBaseLayer = 0;
    std::int32_t autoSlopeLayer = -1;
    float autoSlope = 0.55f;
    std::int32_t autoHeightLayer = -1;
    float autoHeight = 60.0f;
    float autoBlend = 0.25f;
    float windStrength = 0.25f;
    float windSpeed = 1.2f;
    float grassDistance = 30.0f;
    math::Vec3 grassColor{0.0f};
    float grassHeight = 0.5f;
    math::Vec3 grassTipColor{0.0f};
    float grassWidth = 0.05f;
    float grassDensity = 24.0f;
    // Within it, the finest blocks; each coarser level reaches twice as far.
    float leafRange = 96.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
};

// A block of a terrain drawn, in the space of the terrain, or a tile of its grass.
struct GpuTerrainNode
{
    math::Vec2 corner{0.0f};
    float size = 0.0f;
    // The level of detail of a block; the blades of a tile.
    std::uint32_t level = 0;
};

// Prepared world-space attributes, shared by every pass. Positions retain their homogeneous W.
struct GpuSkinnedVertex
{
    math::Vec4 position{0.0f};
    math::Vec4 previous{0.0f};
    math::Vec3 normal{0.0f};
    float padding = 0.0f;
    math::Vec4 tangent{0.0f};
};

struct GpuSkinBounds
{
    math::Vec3 min{0.0f};
    float padding0 = 0.0f;
    math::Vec3 max{0.0f};
    float padding1 = 0.0f;
};

// A view the instances are culled in: the planes they must stand in front of, and the instances
// it keeps.
struct GpuCullView
{
    std::array<math::Vec4, 6> planes{};
    std::uint32_t planeCount = 0;
    std::uint32_t flags = 0;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
};

struct GpuOverlayVertex
{
    math::Vec3 position{0.0f};
    float padding = 0.0f;
    math::Vec4 color{1.0f};
};

// One vertex of the interface. Its layout matches UiVertex in shaders/ui.slang.
struct GpuUiVertex
{
    math::Vec2 position{0.0f};
    math::Vec2 uv{0.0f};
    math::Vec4 color{1.0f};
};

// A particle as shaders/particle.slang reads it.
struct GpuParticle
{
    math::Vec3 position{0.0f};
    float size = 1.0f;
    math::Vec4 color{1.0f};
    math::Vec3 stretch{0.0f};
    float rotation = 0.0f;
    float frame = 0.0f;
    float padding0 = 0.0f;
    float padding1 = 0.0f;
    float padding2 = 0.0f;
};

// A point of a ribbon as shaders/particle.slang reads it.
struct GpuTrailPoint
{
    math::Vec3 position{0.0f};
    float width = 0.0f;
    math::Vec4 color{1.0f};
    math::Vec3 direction{0.0f, 0.0f, 1.0f};
    float u = 0.0f;
};

// A sprite as shaders/sprite.slang reads it.
// The flags of GpuSprite, above its mode.
inline constexpr std::uint32_t spriteFlipX = 4;
inline constexpr std::uint32_t spriteFlipY = 8;
inline constexpr std::uint32_t spriteAdditive = 16;
inline constexpr std::uint32_t spriteLit = 32;
inline constexpr std::uint32_t spriteUnshaded = 64;

struct GpuSprite
{
    math::Mat4 world{1.0f};
    math::Vec4 uvRect{0.0f, 0.0f, 1.0f, 1.0f};
    math::Vec4 color{1.0f};
    math::Vec4 border{0.0f};
    math::Vec2 size{1.0f};
    math::Vec2 pivot{0.5f};
    math::Vec2 naturalSize{1.0f};
    // An index of the texture array, or noParticleTexture for a plain rectangle.
    std::uint32_t texture = 0;
    // The mode in the two lowest bits, then the flags below.
    std::uint32_t flags = 0;
    std::uint32_t objectId = 0;
    // The 2D lights whose item mask shares a bit with it shine on it, through its normal map, an
    // index of the texture array, or noParticleTexture for none.
    std::uint32_t lightMask = 1;
    std::uint32_t normalTexture = 0xFFFFFFFFU;
    // The material whose shader draws it, for the uniforms of a shader of a project.
    std::uint32_t material = 0;
    // Where the values it gives the instance uniforms of that shader start, or 0xFFFFFFFF for none.
    std::uint32_t instanceParameters = 0xFFFFFFFFU;
    std::uint32_t padding0 = 0;
    std::uint32_t padding1 = 0;
    std::uint32_t padding2 = 0;
};

// A light of the 2D plane as shaders/sprite.slang reads it.
// How many 2D lights the shaders read, and how many of them may cast shadows.
inline constexpr std::size_t maxLights2D = 64;
inline constexpr std::uint32_t maxShadowedLights2D = 16;

struct GpuLight2D
{
    // A point light: its position, its height and its radius. A directional one: the middle of the
    // line its shadow strips start from, its height, and the width of the strips.
    math::Vec4 position{0.0f};
    // Linear colour times energy, and how fast a point light fades.
    math::Vec4 color{0.0f};
    // A directional light: the way it goes, then the line across it.
    math::Vec4 direction{0.0f};
    // How much its shadows blur, how far those of a directional light reach, and the length of its
    // strips.
    math::Vec4 shadow{0.0f};
    // 0 for a point light, 1 for a directional one.
    std::uint32_t kind = 0;
    std::uint32_t itemMask = 1;
    // Where its distances start in the shadow buffer, in distances; -1 without shadows.
    std::int32_t shadowOffset = -1;
    std::uint32_t padding = 0;
};

static_assert(sizeof(GpuTerrain) == 176);
static_assert(sizeof(GpuTerrainNode) == 16);
static_assert(sizeof(GpuVertex) == 48);
static_assert(offsetof(GpuVertex, normal) == 16);
static_assert(offsetof(GpuVertex, tangent) == 32);
static_assert(sizeof(GpuMaterial) == 80);
static_assert(offsetof(GpuMaterial, baseColorTexture) == 32);
static_assert(offsetof(GpuMaterial, emissiveTexture) == 48);
static_assert(offsetof(GpuMaterial, doubleSided) == 72);
static_assert(sizeof(GpuLight) == 64);
static_assert(sizeof(GpuShadowView) == 80);
static_assert(offsetof(GpuLight, direction) == 16);
static_assert(offsetof(GpuLight, intensity) == 32);
static_assert(sizeof(GpuCluster) == 8);
static_assert(sizeof(GpuFogVolume) == 112);
static_assert(offsetof(GpuDecal, modulate) == 112);
static_assert(offsetof(GpuDecal, albedoMix) == 144);
static_assert(sizeof(GpuDecal) == 176);
static_assert(offsetof(GpuAtmosphere, sunIlluminance) == 64);
static_assert(offsetof(GpuAtmosphere, fogColor) == 112);
static_assert(sizeof(GpuAtmosphere) == 160);
static_assert(sizeof(GpuReflectionProbe) == 112);
static_assert(offsetof(GpuReflectionProbe, capturePosition) == 80);
static_assert(offsetof(GpuReflectionProbe, map) == 96);
static_assert(sizeof(GpuVertexSkin) == 32);
static_assert(sizeof(GpuOverlayVertex) == 32);
static_assert(sizeof(GpuParticle) == 64);
static_assert(sizeof(GpuTrailPoint) == 48);
static_assert(sizeof(GpuSprite) == 176);
static_assert(offsetof(GpuSprite, instanceParameters) == 160);
static_assert(sizeof(GpuLight2D) == 80);
static_assert(sizeof(GpuSkinnedVertex) == 64);
static_assert(sizeof(GpuSkinBounds) == 32);
static_assert(sizeof(GpuCullView) == 112);

} // namespace devex::render
