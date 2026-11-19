#pragma once

// The renderer of OpenGL: OpenGL 3.3 on a computer, OpenGL ES 3.0 on a phone and WebGL2 in a browser, the light renderer
// that will take the place of WebGPU (see "Les API graphiques" in docs/decisions.md). It draws, as the first step of its
// plan, the 2D and the interface: the environment behind the scene, sprites and tilemaps, the tonemapping, the interface
// and the captures; then, in GlScene.cpp, meshes, skinned or not, lit by the sun, the local lights and the environment,
// with their shadows, in the fog, and the exposure measured. The particles, the decals, the terrains, the effects after
// the light and the shaders of projects are kept, not drawn yet. Its shaders are those of shaders/gl_*.slang, translated
// into GLSL at the build.
#include "GlApi.hpp"

#include "../FramePreparation.hpp"
#include "../GpuTypes.hpp"
#include "../RenderBackend.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/core/SlotMap.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/render/Culling.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace devex::render::opengl {

// Where the uniform blocks of shaders/gl_*.slang are bound, by their names.
namespace binding {
inline constexpr gl::GLuint scene = 0;
inline constexpr gl::GLuint lights = 1;
inline constexpr gl::GLuint shadowViews = 2;
inline constexpr gl::GLuint material = 3;
inline constexpr gl::GLuint shadowView = 4;
inline constexpr gl::GLuint spriteFrame = 5;
inline constexpr gl::GLuint tonemap = 6;
inline constexpr gl::GLuint uiDraw = 7;
inline constexpr gl::GLuint bake = 8;
inline constexpr gl::GLuint particleDraw = 9;
inline constexpr gl::GLuint terrain = 10;
inline constexpr gl::GLuint terrainDraw = 11;
inline constexpr gl::GLuint blit = 12;
inline constexpr gl::GLuint decals = 13;
inline constexpr gl::GLuint lights2D = 14;
// The settings of a step of what follows the light: the antialiasing, the lens or the bloom.
inline constexpr gl::GLuint post = 15;
// The uniforms of the material of a shader of the project, and the values objects give the instance uniforms.
inline constexpr gl::GLuint customMaterial = 16;
inline constexpr gl::GLuint customInstances = 17;
} // namespace binding

// The texture units of their samplers: those of the scene first, which every lit shader reads, then those of each draw.
namespace unit {
inline constexpr gl::GLint irradiance = 0;
inline constexpr gl::GLint specular = 1;
inline constexpr gl::GLint brdf = 2;
inline constexpr gl::GLint cascades = 3;
inline constexpr gl::GLint atlas = 4;
// The texture of a draw: the base colour of a material, the sky, a sprite, the interface, the scene to tonemap, or what a
// bake reads.
inline constexpr gl::GLint first = 5;
inline constexpr gl::GLint second = 6;
inline constexpr gl::GLint third = 7;
inline constexpr gl::GLint fourth = 8;
inline constexpr gl::GLint fifth = 9;
inline constexpr gl::GLint bones = 10;
// The arrays of the layers of a terrain.
inline constexpr gl::GLint layerColors = 11;
inline constexpr gl::GLint layerSurfaces = 12;
inline constexpr gl::GLint layerNormals = 13;
// The atlases of the textures of the decals: their colours, then their normal and ORM maps.
inline constexpr gl::GLint decalColors = 14;
inline constexpr gl::GLint decalSurfaces = 15;
// The eight textures of the material of a shader of the project.
inline constexpr gl::GLint customTextures = 16;
} // namespace unit

class GlRenderer final : public RenderBackend
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<GlRenderer>> create(const platform::Platform& platform, platform::Window& window,
                                                                          const RendererConfig& config);
    ~GlRenderer() override;

    [[nodiscard]] const GpuInfo& gpu() const noexcept override;
    [[nodiscard]] PresentMode presentMode() const noexcept override;
    void setPresentMode(PresentMode mode) override;
    [[nodiscard]] core::Result<std::uint32_t> addWindowTarget(platform::Window& window) override;
    void removeWindowTarget(std::uint32_t target) override;
    [[nodiscard]] core::Result<std::string> describeGraphics() const override;

    [[nodiscard]] core::Result<MeshHandle> createMesh(const asset::MeshData& mesh) override;
    [[nodiscard]] bool isReady(MeshHandle mesh) const noexcept override;
    [[nodiscard]] bool isReady(TextureHandle texture) const noexcept override;
    void destroyMesh(MeshHandle mesh) override;
    [[nodiscard]] std::uint32_t submeshCount(MeshHandle mesh) const noexcept override;

    [[nodiscard]] core::Result<TextureHandle> createTexture(const asset::TextureData& texture) override;
    void destroyTexture(TextureHandle texture) override;
    [[nodiscard]] core::Result<void> updateTexture(TextureHandle texture, std::uint32_t top, std::uint32_t height,
                                                   std::span<const std::byte> rows) override;

    [[nodiscard]] core::Result<ReflectionProbeHandle> createReflectionProbe(const asset::ReflectionProbeData& probe) override;
    void destroyReflectionProbe(ReflectionProbeHandle probe) override;
    [[nodiscard]] core::Result<IrradianceVolumeHandle> createIrradianceVolume(const asset::IrradianceVolumeData& volume) override;
    void destroyIrradianceVolume(IrradianceVolumeHandle volume) override;

    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc& material) override;
    void updateMaterial(MaterialHandle handle, const MaterialDesc& material) override;
    void destroyMaterial(MaterialHandle material) override;

    [[nodiscard]] core::Result<ShaderHandle> createShader(const asset::ShaderData& shader) override;
    [[nodiscard]] core::Result<void> updateShader(ShaderHandle handle, const asset::ShaderData& shader) override;
    void destroyShader(ShaderHandle shader) override;

    [[nodiscard]] RenderWorld& beginFrame() noexcept override;
    void resetSceneHistory() noexcept override;
    [[nodiscard]] core::Result<void> endFrame() override;
    [[nodiscard]] std::vector<PickResult> takePickResults() override;
    [[nodiscard]] std::uint64_t requestCapture(std::uint32_t maxWidth, std::uint32_t maxHeight,
                                               std::optional<std::uint32_t> view) override;
    [[nodiscard]] std::uint64_t requestWindowCapture() override;
    [[nodiscard]] std::vector<CapturedImage> takeCaptures() override;

    [[nodiscard]] RendererStats stats() const noexcept override;

private:
    struct GpuTexture
    {
        gl::GLuint texture = 0;
        asset::TextureFormat format = asset::TextureFormat::Rgba8Srgb;
        math::Extent2D size;
        // A texture that cannot be filtered, such as heights in floats, is never sampled.
        bool bindable = true;
        // Read at its nearest pixel, for pixel art.
        bool nearest = false;
        std::uint32_t mips = 1;
    };
    // A mesh in its buffers, and the vertex array that reads them: its vertices, then the skin of a skinned one; the
    // attributes of its instances follow where each batch starts.
    struct GpuMesh
    {
        gl::GLuint vertices = 0;
        gl::GLuint indices = 0;
        gl::GLuint skin = 0;
        gl::GLuint array = 0;
        std::vector<asset::Submesh> submeshes;
        math::Aabb bounds;
    };
    struct GpuMaterial
    {
        MaterialDesc description;
        // Its factors, as the shaders read them; written again when it changes.
        gl::GLuint uniform = 0;
        bool written = false;
        // The slots of the uniforms of a shader of the project, written again when it or the textures change.
        gl::GLuint customSlots = 0;
        std::uint64_t customRevision = ~std::uint64_t{0};
        bool customWritten = false;
    };
    // The programs of a shader of the project, by what they draw.
    enum CustomProgram : std::size_t
    {
        customMesh,
        customSkinned,
        customShadow,
        customSkinnedShadow,
        // Sprites, particles or the sky.
        customDraw,
        // The sky laid out for the light of the environment.
        customBake,
        customProgramCount,
    };
    // A shader of the project: what OpenGL draws with it, its GLSL by entry point, and its programs, linked when first
    // drawn. Its revision grows whenever it changes.
    struct GpuShader
    {
        asset::ShaderKind kind = asset::ShaderKind::Spatial;
        asset::ShaderCull cull = asset::ShaderCull::Back;
        asset::ShaderBlend blend = asset::ShaderBlend::Mix;
        bool transparent = false;
        bool castsShadows = true;
        std::vector<asset::GlslStage> glsl;
        std::array<gl::GLuint, customProgramCount> programs{};
        std::array<bool, customProgramCount> tried{};
        std::uint64_t revision = 0;
    };
    struct Empty
    {
    };
    // A program, linked from the vertex and the fragment shaders of a file of shaders/gl_*.slang.
    struct Program
    {
        gl::GLuint program = 0;
    };
    // A run of sprites that read the same texture, drawn at once.
    struct SpriteRun
    {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
        std::uint32_t texture = 0;
        std::uint32_t normalTexture = 0;
        // A canvas_item shader of the project, and the material of the frame it draws with.
        ShaderHandle shader;
        std::uint32_t material = 0;
    };
    // A batch of the interface as it is drawn.
    struct UiRun
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        gl::GLuint texture = 0;
        bool nearest = false;
        // Pixels from the top left corner: left, top, width, height.
        std::uint32_t scissor[4]{};
    };
    // The meshes of the frame that share a mesh, a submesh, a material and a way to be drawn, at once; a blended one
    // alone, in its order from the farthest.
    struct MeshBatch
    {
        const GpuMesh* mesh = nullptr;
        std::uint32_t submesh = 0;
        MaterialHandle material;
        std::uint32_t flags = 0;
        std::uint32_t firstInstance = 0;
        std::uint32_t instanceCount = 0;
        float distance = 0.0f;
    };
    // The casters of one view of the shadows: a cascade of the sun, or a tile of the atlas of the local lights.
    struct ShadowPass
    {
        std::uint32_t firstBatch = 0;
        std::uint32_t batchCount = 0;
        bool atlas = false;
        std::uint32_t layer = 0;
        // Pixels of the atlas: left, bottom, width, height.
        std::array<float, 4> viewport{};
    };
    // What the blended pass draws in its order: a batch of blended meshes, runs of sprites, or a draw of particles.
    struct TransparentDraw
    {
        bool sprites = false;
        bool particles = false;
        std::uint32_t index = 0;
        std::uint32_t firstRun = 0;
        std::uint32_t runCount = 0;
    };
    // The light of the environment, baked from the sky texture: its irradiance and its specular reflections by roughness.
    struct Environment
    {
        TextureHandle sky;
        bool baked = false;
        gl::GLuint irradiance = 0;
        gl::GLuint specular = 0;
        std::uint32_t specularMips = 1;
    };
    // What a bake of the environment draws with: its uniform block, with the face changed for each.
    struct BakeSettings
    {
        float roughness = 0.0f;
        std::uint32_t sampleCount = 0;
        float sourceLod = 0.0f;
        float sourceSize = 0.0f;
        gl::GLuint source = 0;
        // The width of the equirectangular map, whose detail each level of the cube samples at.
        std::uint32_t sourceWidth = 0;
    };
    // The textures of the layers of a terrain, drawn into arrays, and what they were drawn from.
    struct TerrainLayers
    {
        std::vector<std::uint64_t> key;
        gl::GLuint colors = 0;
        gl::GLuint surfaces = 0;
        gl::GLuint normals = 0;
    };
    // An atlas of the textures of the decals, which the blit draws them into, what it was drawn from, and where each
    // texture lies in it: its corner and its size, from 0 to 1.
    struct DecalAtlas
    {
        std::vector<std::uint64_t> key;
        gl::GLuint texture = 0;
        std::vector<std::pair<TextureHandle, math::Vec4>> rects;
    };
    // What a terrain draws this frame: its blocks, whole or by quarters, and its tiles of grass by bands, where they lie
    // among the instances of the frame; its maps, and the draws of its grass.
    struct TerrainDrawing
    {
        std::size_t terrain = 0;
        bool castShadows = true;
        std::array<std::uint32_t, 5> firstNode{};
        std::array<std::uint32_t, 5> nodeCount{};
        std::array<std::uint32_t, 3> firstTile{};
        std::array<std::uint32_t, 3> tileCount{};
        std::array<std::uint32_t, 3> blades{};
        std::uint32_t firstDraw = 0;
        std::array<gl::GLuint, 5> maps{};
    };
    struct CaptureRequest
    {
        std::uint64_t id = 0;
        math::Extent2D maxSize;
        bool window = false;
    };
    // An image the frame draws into, and the framebuffer that draws into it.
    struct Target
    {
        gl::GLuint texture = 0;
        gl::GLuint framebuffer = 0;
        math::Extent2D size;
    };

    GlRenderer(platform::Window& window, const RendererConfig& config) noexcept;
    [[nodiscard]] core::Result<void> initialize();
    [[nodiscard]] core::Result<Program> linkProgram(std::string_view vertex, std::string_view fragment) const;
    // A program of the GLSL given, named `vertex` and `fragment` in what OpenGL says of it.
    [[nodiscard]] core::Result<Program> linkSources(std::string_view vertex, std::string_view vertexSource, std::string_view fragment,
                                                    std::string_view fragmentSource) const;
    // Binds the uniform blocks of a program to their bindings, and its samplers to their texture units, by their names.
    void bindProgram(const Program& program, std::initializer_list<std::pair<const char*, gl::GLuint>> blocks,
                     std::initializer_list<std::pair<const char*, gl::GLint>> samplers) const;
    // Gives a buffer `size` bytes at least, kept when it already has them.
    void growBuffer(gl::GLuint buffer, gl::GLenum target, std::size_t& capacity, std::size_t size) const;
    [[nodiscard]] core::Result<void> ensureTarget(Target& target, math::Extent2D size, gl::GLenum internalFormat, gl::GLenum format,
                                                  gl::GLenum type) const;
    void destroyTarget(Target& target) const noexcept;
    [[nodiscard]] const GpuTexture* textureAtSlot(std::uint32_t slot) const noexcept;
    void bindTexture(gl::GLuint unit, gl::GLuint texture, gl::GLuint sampler) const;
    void applyPresentMode();

    // GlScene.cpp: the 3D.
    [[nodiscard]] core::Result<void> createSceneResources();
    void destroySceneResources() noexcept;
    [[nodiscard]] gl::GLuint whiteCube() const;
    // Draws a triangle over each face of each level of `cube`, through `program`, with the settings of the bake.
    void bakeFaces(const Program& program, gl::GLuint cube, std::uint32_t size, std::uint32_t mips, const BakeSettings& settings);
    void bakeBrdfTable();
    void bakeEnvironment(TextureHandle sky);
    // The light of the environment from an equirectangular image of `width` pixels.
    void bakeEnvironmentFrom(gl::GLuint equirectangular, std::uint32_t width);
    [[nodiscard]] float sortDistance(math::Vec3 point, math::Vec3 cameraPosition) const noexcept;
    // The lights, the shadows, the instances and the batches of the frame, and the scene the shaders read.
    void prepareMeshes(const ViewMatrices& matrices, const Frustum& frustum, math::Extent2D sceneSize);
    // The local lights that reach a box, at most eight, the nearest first, a byte each; around a point without a box.
    [[nodiscard]] std::array<std::uint32_t, 2> lightsReaching(const math::Aabb& box, math::Vec3 centre) const;
    // The textures into the atlas, kept while they stay the same; the colours in sRGB.
    void fillDecalAtlas(DecalAtlas& atlas, std::vector<TextureHandle> textures, bool colors);
    // The decals of the frame, at most 64, those the camera sees, the nearest first, then in the order of the world.
    void prepareDecals(const Frustum& frustum, math::Vec3 cameraPosition);
    // The decals whose box reaches a box, at most eight, in the order of the frame, a byte each; a point without a box.
    [[nodiscard]] std::array<std::uint32_t, 2> decalsReaching(const math::Aabb& box, math::Vec3 centre) const;
    void prepareParticles(const ViewMatrices& matrices);
    void orderTransparent(const ViewMatrices& matrices);
    // The depth of the opaque surfaces, copied for the particles that fade into them: OpenGL cannot read what it draws into.
    void copySceneDepth(math::Extent2D size);
    void drawParticles(std::uint32_t index);
    void fillTerrainLayers(TerrainLayers& layers, const RenderTerrain& terrain);
    void prepareTerrains(const ViewMatrices& matrices, const Frustum& frustum);
    // Binds what a terrain reads: its uniform block, its maps and the arrays of its layers.
    void bindTerrain(std::size_t drawn) const;
    // The ground of the terrains, or its depth into the view of the shadows `shadowView`.
    void drawTerrains(std::optional<std::size_t> shadowView);
    void drawGrass();
    [[nodiscard]] gl::GLuint materialUniform(GpuMaterial& material) const;
    void bindMaterial(GpuMaterial& material, bool shadow) const;
    // Points the attributes of the instances of `mesh` at those of a batch.
    void bindInstances(const GpuMesh& mesh, std::uint32_t firstInstance) const;
    void bindSceneTextures() const;
    void drawShadows();
    void drawMeshBatch(const MeshBatch& batch);
    void drawMeshes(bool blended);
    void drawTransparent();
    // The luminance of the scene, measured on a coarse grid read back frames later, which the automatic exposure follows.
    void updateExposure();
    void measureLuminance();

    // The size of the uniform block of the scene.
    [[nodiscard]] static std::size_t sceneDataSize() noexcept;
    // GlCustom.cpp: the shaders of projects.
    [[nodiscard]] static GpuShader gpuShaderOf(const asset::ShaderData& shader);
    static void releaseShader(GpuShader& shader) noexcept;
    // The shader of the project a material draws with, when it is of the kind and has its GLSL.
    [[nodiscard]] GpuShader* shaderOf(const MaterialDesc& material, asset::ShaderKind kind);
    [[nodiscard]] GpuShader* shaderOf(MaterialHandle material, asset::ShaderKind kind);
    // The program of a shader that draws `which`, linked and bound when first asked for; 0 when it cannot be.
    [[nodiscard]] gl::GLuint customProgram(GpuShader& shader, CustomProgram which);
    // Binds the uniforms and the textures of the material of a shader of the project.
    void bindCustomMaterial(GpuMaterial& material);
    // The values objects give the instance uniforms of their shaders this frame.
    void uploadInstanceParameters();
    // The sky shader of the environment, and its sky laid out and baked for the light of the environment when what it
    // draws changes: whether it was.
    [[nodiscard]] GpuShader* skyShader();
    [[nodiscard]] bool updateSkyShader();

    // GlPost.cpp: what follows the light.
    [[nodiscard]] core::Result<void> createPostResources();
    void destroyPostResources() noexcept;
    // Whether the scene keeps half floats, which the effects after the light need.
    [[nodiscard]] bool drawsPostEffects() const noexcept;
    // A triangle over `into` through `program`, with its settings in the uniform block of the steps.
    void drawPostStep(const Program& program, const Target& into, const void* settings, std::size_t size);
    // The scene antialiased with the frames before, or the scene itself: what the next steps read.
    [[nodiscard]] gl::GLuint resolve(const ViewMatrices& matrices);
    // The depth of field and the motion blur over `source`, or `source` itself.
    [[nodiscard]] gl::GLuint drawLens(const ViewMatrices& matrices, gl::GLuint source);
    // The halo of the bright parts of `finished`, or 0 without bloom.
    [[nodiscard]] gl::GLuint drawBloom(gl::GLuint finished);
    // The table of colours of the camera, a strip of squares, or nothing.
    [[nodiscard]] const GpuTexture* colorTableOf(TextureHandle handle) const;

    void drawSky();
    void drawSprites(std::uint32_t firstRun, std::uint32_t runCount);
    // The scene tonemapped into the framebuffer bound, of `size`, and the interface over it. A framebuffer that keeps sRGB
    // encodes the colours itself, and blends them as light; another takes them encoded by the shaders.
    void drawImage(math::Extent2D size, bool interface, bool srgb);
    // The pixels of the framebuffer bound, from the top row down.
    [[nodiscard]] CapturedImage readPixels(std::uint64_t request, math::Extent2D size) const;

    platform::Window& m_window;
    RendererConfig m_config;
    void* m_context = nullptr;
    // Whether the functions of OpenGL were found, which the destructor calls.
    bool m_loaded = false;
    bool m_es = false;
    GpuInfo m_gpu;
    PresentMode m_presentMode = PresentMode::Fifo;
    bool m_presentModeApplied = false;
    // Whether the window keeps its colours in sRGB, as Vulkan's swapchain: a browser's does not.
    bool m_windowSrgb = false;
    // Whether the driver reads the blocks of BC7 and of ASTC.
    bool m_bptc = false;
    bool m_astc = false;
    float m_maxAnisotropy = 1.0f;
    gl::GLint m_uniformAlignment = 256;

    core::SlotMap<GpuTexture, TextureTag> m_textures;
    core::SlotMap<GpuMesh, MeshTag> m_meshes;
    core::SlotMap<GpuMaterial, MaterialTag> m_materials;
    core::SlotMap<GpuShader, ShaderTag> m_shaders;
    core::SlotMap<Empty, ReflectionProbeTag> m_probes;
    core::SlotMap<Empty, IrradianceVolumeTag> m_volumes;
    // The textures by the slot the sprites name them with: the index of their handle.
    std::vector<TextureHandle> m_slotHandles;

    Program m_skyProgram;
    Program m_spriteProgram;
    Program m_uiProgram;
    Program m_tonemapProgram;
    Program m_meshProgram;
    Program m_skinnedProgram;
    Program m_shadowProgram;
    Program m_skinnedShadowProgram;
    Program m_cubeProgram;
    Program m_prefilterProgram;
    Program m_irradianceProgram;
    Program m_brdfProgram;
    Program m_luminanceProgram;
    Program m_particleProgram;
    Program m_ribbonProgram;
    Program m_groundProgram;
    Program m_groundShadowProgram;
    Program m_grassProgram;
    Program m_blitProgram;
    Program m_taaProgram;
    // The threshold, the halving and the adding back of the bloom; the eight steps of the lens.
    std::array<Program, 3> m_bloomPrograms;
    std::array<Program, 8> m_lensPrograms;
    // The vertex arrays of what reads no vertices, of the sprites and of the interface.
    gl::GLuint m_emptyArray = 0;
    gl::GLuint m_spriteArray = 0;
    gl::GLuint m_uiArray = 0;
    gl::GLuint m_spriteBuffer = 0;
    std::size_t m_spriteCapacity = 0;
    // The bytes of the local lights of each sprite, which lit ones read.
    gl::GLuint m_spriteLightBuffer = 0;
    // The materials of the frame by their index, which the canvas_item shaders of the project bind.
    std::vector<MaterialHandle> m_spriteMaterials;
    // The values objects give the instance uniforms of their shaders, at most 1024 a frame.
    gl::GLuint m_customInstanceBuffer = 0;
    // The sky a sky shader of the project laid out, and what it was drawn from, which bakes the light of the environment.
    Target m_shaderSky;
    std::uint64_t m_shaderSkyKey = 0;
    std::uint64_t m_shaderSkyLight = 0;
    std::chrono::steady_clock::time_point m_shaderSkyBaked;
    std::size_t m_spriteLightCapacity = 0;
    // The 2D lights of the frame, and the distances their shadows keep, a row of a texture of floats each.
    std::vector<GpuLight2D> m_lights2D;
    std::vector<float> m_shadows2D;
    gl::GLuint m_light2DBuffer = 0;
    gl::GLuint m_shadow2DTexture = 0;
    std::uint32_t m_shadow2DRows = 0;
    gl::GLuint m_uiVertexBuffer = 0;
    std::size_t m_uiVertexCapacity = 0;
    gl::GLuint m_uiIndexBuffer = 0;
    std::size_t m_uiIndexCapacity = 0;
    // The uniform blocks: those of one draw, then those of each batch of the interface at an aligned offset of their own.
    gl::GLuint m_sceneBuffer = 0;
    gl::GLuint m_lightBuffer = 0;
    gl::GLuint m_shadowViewBuffer = 0;
    gl::GLuint m_bakeBuffer = 0;
    // The matrices of the views of the shadows, each at an aligned offset of its own.
    gl::GLuint m_shadowMatrixBuffer = 0;
    std::size_t m_shadowMatrixCapacity = 0;
    gl::GLuint m_instanceBuffer = 0;
    std::size_t m_instanceCapacity = 0;
    // The particles of the frame and the segments of its ribbons, each with both its points, as instances; and the
    // uniform blocks of the draws, each at an aligned offset of its own.
    gl::GLuint m_particleArray = 0;
    gl::GLuint m_ribbonArray = 0;
    gl::GLuint m_particleBuffer = 0;
    std::size_t m_particleCapacity = 0;
    gl::GLuint m_ribbonBuffer = 0;
    std::size_t m_ribbonCapacity = 0;
    gl::GLuint m_particleDrawBuffer = 0;
    std::size_t m_particleDrawCapacity = 0;
    std::size_t m_particleDrawStride = 256;
    // The terrains: the grid of a block by quarters and the blade of grass, as indices; their blocks and tiles as
    // instances; their uniform blocks and those of their draws, each at an aligned offset of its own.
    gl::GLuint m_terrainArray = 0;
    gl::GLuint m_terrainGrid = 0;
    gl::GLuint m_grassBlade = 0;
    gl::GLuint m_terrainNodeBuffer = 0;
    std::size_t m_terrainNodeCapacity = 0;
    gl::GLuint m_grassTileBuffer = 0;
    std::size_t m_grassTileCapacity = 0;
    gl::GLuint m_terrainBuffer = 0;
    std::size_t m_terrainCapacity = 0;
    std::size_t m_terrainStride = 512;
    gl::GLuint m_terrainDrawBuffer = 0;
    std::size_t m_terrainDrawCapacity = 0;
    std::size_t m_terrainDrawStride = 256;
    gl::GLuint m_blitBuffer = 0;
    // The decals of the frame, their boxes in the world in the same order, and the atlases of their textures.
    gl::GLuint m_decalBuffer = 0;
    std::vector<math::Aabb> m_decalBoxes;
    DecalAtlas m_decalColors;
    DecalAtlas m_decalSurfaces;
    std::vector<TerrainLayers> m_terrainLayers;
    std::vector<TerrainDrawing> m_terrainDrawings;
    // Grows whenever a texture comes, goes or changes, which the layers of terrains follow.
    std::uint64_t m_textureRevision = 0;
    gl::GLuint m_depthCopy = 0;
    gl::GLuint m_depthCopyFramebuffer = 0;
    math::Extent2D m_depthCopySize;
    gl::GLuint m_spriteFrameBuffer = 0;
    gl::GLuint m_tonemapBuffer = 0;
    gl::GLuint m_uiDrawBuffer = 0;
    std::size_t m_uiDrawCapacity = 0;
    gl::GLuint m_linearSampler = 0;
    gl::GLuint m_nearestSampler = 0;
    gl::GLuint m_clampSampler = 0;
    // Nearest and clamped, which the bones need: a texture of floats cannot be filtered everywhere.
    gl::GLuint m_pointSampler = 0;
    // Around the horizon, not over the poles.
    gl::GLuint m_skySampler = 0;
    gl::GLuint m_cubeSampler = 0;
    // Bilinear comparisons, as the sampler of shadows of Vulkan.
    gl::GLuint m_shadowSampler = 0;
    // Clamped, between mips: what the atlases of the decals are drawn from.
    gl::GLuint m_clampMipSampler = 0;
    gl::GLuint m_white = 0;
    gl::GLuint m_black = 0;
    gl::GLuint m_flatNormal = 0;
    gl::GLuint m_brdfTable = 0;
    gl::GLuint m_boneTexture = 0;
    std::uint32_t m_boneRows = 0;
    // The cascades of the sun, one layer each, and the atlas of the local lights.
    gl::GLuint m_cascadeMap = 0;
    std::array<gl::GLuint, 4> m_cascadeFramebuffers{};
    gl::GLuint m_atlasMap = 0;
    gl::GLuint m_atlasFramebuffer = 0;
    std::uint32_t m_shadowMapSize = 2048;
    std::uint32_t m_shadowAtlasSize = 4096;
    gl::GLenum m_shadowDepthFormat = 0;
    bool m_depthClamp = false;
    // What follows the light: the settings of each step, the two images the antialiasing resolves into in turn, the halved
    // images of the bloom, the chain of the lens and what it gives, and what the frame before was seen through.
    gl::GLuint m_postBuffer = 0;
    std::array<Target, 2> m_resolved;
    std::array<Target, 5> m_bloomChain;
    std::array<Target, 7> m_lensImages;
    Target m_lensOutput;
    std::uint32_t m_lensTileSize = 8;
    bool m_historyValid = false;
    float m_historyExposure = 1.0f;
    math::Mat4 m_previousViewProjection{1.0f};
    math::Mat4 m_previousSkyViewProjection{1.0f};
    std::uint64_t m_frameIndex = 0;
    // What the tonemapping reads this frame: the scene as the antialiasing and the lens leave it, and the halo of the bloom.
    gl::GLuint m_finishedImage = 0;
    gl::GLuint m_bloomImage = 0;
    // The scene in light, which the tonemapping maps to the display, and its depth.
    Target m_scene;
    gl::GLenum m_sceneFormat = 0;
    gl::GLuint m_sceneDepth = 0;
    // Whether OpenGL draws into images of half floats, which the light of the environment is baked into.
    bool m_halfFloatTargets = false;
    Environment m_environment;
    MaterialHandle m_defaultMaterial;
    // The grid the luminance is measured on, the buffer it is read back into, and when the GPU is done with it.
    Target m_luminanceTarget;
    gl::GLuint m_luminanceBuffer = 0;
    gl::GLsync m_luminanceFence = nullptr;
    float m_measuredExposure = 0.0f;

    RenderWorld m_world;
    std::vector<GpuLight> m_lights;
    std::vector<MeshBatch> m_meshBatches;
    std::vector<MeshBatch> m_shadowBatches;
    std::vector<ShadowPass> m_shadowPasses;
    std::vector<TransparentDraw> m_transparentDraws;
    SpriteFrame m_spriteFrame;
    std::vector<SpriteRun> m_spriteRuns;
    PackedUi m_ui;
    std::vector<UiRun> m_uiRuns;
    std::vector<std::byte> m_uiDraws;

    float m_ev100 = 0.0f;
    float m_targetEv100 = 0.0f;
    float m_exposure = 1.0f;
    bool m_exposureInitialized = false;
    bool m_snapExposure = true;
    std::chrono::steady_clock::time_point m_lastExposureUpdate;
    float m_time = 0.0f;
    // The time of the game in seconds, which the wind of the grass blows by.
    float m_shaderTime = 0.0f;
    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();
    std::uint32_t m_drawCalls = 0;
    math::Extent2D m_windowSize;
    std::uint64_t m_nextCapture = 1;
    std::vector<CaptureRequest> m_captureRequests;
    std::vector<CapturedImage> m_captured;
};

} // namespace devex::render::opengl
