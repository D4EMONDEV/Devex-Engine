#pragma once

#include "../Lighting.hpp"
#include "DescriptorSets.hpp"
#include "Device.hpp"
#include "Atmosphere.hpp"
#include "EnvironmentBaker.hpp"
#include "GpuData.hpp"
#include "Instance.hpp"
#include "Memory.hpp"
#include "Pipeline.hpp"
#include "RenderGraph.hpp"
#include "Swapchain.hpp"
#include "Upload.hpp"
#include "Vulkan.hpp"

#include <devex/asset/MeshData.hpp>
#include <devex/core/SlotMap.hpp>
#include <devex/platform/Platform.hpp>
#include <devex/platform/Window.hpp>
#include <devex/render/Culling.hpp>
#include <devex/render/Renderer.hpp>
#include <devex/render/RenderWorld.hpp>

#include <array>
#include <deque>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace devex::render::vulkan {

// Vulkan implementation behind render::Renderer.
//
// Each frame is recorded as render graph passes: the sun's shadow cascades, the
// high dynamic range scene (forward+ lighting, then the sky), the luminance measure for automatic
// exposure, picking and the selection mask when the tools ask for them, tonemapping to the
// swapchain or to the viewport image of the tools, the tools' overlay, and the images of the tools
// composed over the window. The other views of the frame (RenderWorld::views) are recorded before,
// each in its own submission, with the same passes into an image of its own.
class VulkanRenderer
{
public:
    [[nodiscard]] static core::Result<std::unique_ptr<VulkanRenderer>> create(
        const platform::Platform& platform, platform::Window& window, const RendererConfig& config);

    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    [[nodiscard]] const GpuInfo& gpu() const noexcept;
    [[nodiscard]] PresentMode presentMode() const noexcept;

    [[nodiscard]] core::Result<MeshHandle> createMesh(const asset::MeshData& mesh);
    [[nodiscard]] bool isReady(MeshHandle mesh) const noexcept;
    [[nodiscard]] bool isReady(TextureHandle texture) const noexcept;
    void destroyMesh(MeshHandle mesh);
    [[nodiscard]] std::uint32_t submeshCount(MeshHandle mesh) const noexcept;

    [[nodiscard]] core::Result<TextureHandle> createTexture(const asset::TextureData& texture);
    void destroyTexture(TextureHandle texture);
    [[nodiscard]] core::Result<void> updateTexture(TextureHandle texture, std::uint32_t top, std::uint32_t height,
                                                   std::span<const std::byte> rows);

    [[nodiscard]] core::Result<ReflectionProbeHandle> createReflectionProbe(const asset::ReflectionProbeData& probe);
    void destroyReflectionProbe(ReflectionProbeHandle probe);
    [[nodiscard]] core::Result<IrradianceVolumeHandle> createIrradianceVolume(const asset::IrradianceVolumeData& volume);
    void destroyIrradianceVolume(IrradianceVolumeHandle volume);

    [[nodiscard]] MaterialHandle createMaterial(const MaterialDesc& material);
    void updateMaterial(MaterialHandle handle, const MaterialDesc& material);
    void destroyMaterial(MaterialHandle material);

    [[nodiscard]] core::Result<ShaderHandle> createShader(const asset::ShaderData& shader);
    [[nodiscard]] core::Result<void> updateShader(ShaderHandle handle, const asset::ShaderData& shader);
    void destroyShader(ShaderHandle shader);

    [[nodiscard]] RenderWorld& beginFrame() noexcept;
    [[nodiscard]] core::Result<void> endFrame();
    [[nodiscard]] std::vector<PickResult> takePickResults();
    // Of the main image, or of the view of that id.
    [[nodiscard]] std::uint64_t requestCapture(std::uint32_t maxWidth, std::uint32_t maxHeight,
                                               std::optional<std::uint32_t> view = std::nullopt);
    [[nodiscard]] std::uint64_t requestWindowCapture();
    [[nodiscard]] std::vector<CapturedImage> takeCaptures();

    [[nodiscard]] RendererStats stats() const noexcept;

private:
    // Frames recorded before the oldest one must complete, which bounds the latency added by
    // the CPU running ahead of the GPU.
    static constexpr std::uint64_t framesInFlight = 2;
    // How many jittered frames cover a pixel before the pattern starts over.
    static constexpr std::uint64_t taaSampleCount = 8;
    // How much of the history a still pixel keeps: higher is smoother and slower to follow.
    static constexpr float taaBlend = 0.9f;
    static constexpr VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    static constexpr VkFormat sceneFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    // More textures than any scene needs for now, within what drivers commonly allow.
    static constexpr std::uint32_t maxTextures = 8192;
    // Bindless slots of the textures that stand in for missing ones.
    static constexpr std::uint32_t whiteTextureSlot = 0;
    static constexpr std::uint32_t flatNormalTextureSlot = 1;
    static constexpr std::uint32_t blackTextureSlot = 2;
    // The last slots of the texture array show the images the tools compose, a block for each frame
    // context: the scene image first, then the interface surfaces in the order of the frame.
    static constexpr std::uint32_t toolImageSlots = 32;
    static constexpr std::uint32_t shadowMapSize = 2048;
    static constexpr std::uint32_t luminanceGridWidth = 64;
    static constexpr std::uint32_t luminanceGridHeight = 36;
    static constexpr VkFormat pickFormat = VK_FORMAT_R32_UINT;
    static constexpr VkFormat selectionMaskFormat = VK_FORMAT_R8_UNORM;
    // Where a pixel was on the previous frame, and the normal of its surface.
    static constexpr VkFormat velocityFormat = VK_FORMAT_R16G16_SFLOAT;
    static constexpr VkFormat normalFormat = VK_FORMAT_R16G16_SFLOAT;
    static constexpr VkFormat occlusionFormat = VK_FORMAT_R8_UNORM;
    // The circle of confusion of each pixel, and the motion of each tile.
    static constexpr VkFormat confusionFormat = VK_FORMAT_R16_SFLOAT;
    static constexpr VkFormat tileMotionFormat = VK_FORMAT_R16G16_SFLOAT;
    // The shadows of the local lights share one image, cut into square tiles.
    static constexpr std::uint32_t shadowAtlasSize = 4096;
    static constexpr std::uint32_t shadowAtlasCell = 512;
    static constexpr std::uint32_t shadowAtlasCells = shadowAtlasSize / shadowAtlasCell;
    // How many lights are given tiles twice as wide, from the most important down.
    static constexpr std::size_t largeShadowLights = 2;
    // Larger viewports are clamped, since images this large would exhaust memory.
    static constexpr std::uint32_t maxViewportSize = 8192;
    // The views a frame draws beside the main image; more are left out. The tools capture the probes
    // they bake six faces at a time, a few probes a frame.
    static constexpr std::size_t maxViews = 24;
    // The reflection probes the renderer keeps, and the most a frame reflects, the smallest first.
    static constexpr std::uint32_t maxReflectionProbes = 64;
    static constexpr std::size_t maxFrameReflectionProbes = 32;
    static constexpr std::size_t maxFrameIrradianceVolumes = 16;

    enum class MeshPass : std::uint8_t
    {
        // Depth, motion and normals of everything that hides what is behind it.
        Prepass,
        // Everything that hides what is behind it, including the cut-out materials.
        Scene,
        // The blended materials, drawn after the scene from the farthest to the nearest.
        Transparent,
        Shadow,
        // Into one tile of the shadow atlas, for a local light.
        LocalShadow,
        Pick,
        // Only the outlined instances.
        SelectionMask,
    };

    // Where each list of the overlay starts in the frame's overlay vertex buffer.
    struct OverlayRanges
    {
        std::uint32_t sceneLines = 0;
        std::uint32_t overlayLines = 0;
        std::uint32_t overlayTriangles = 0;
    };

    // The copy of a created mesh or texture to the GPU, waiting for a frame to record it.
    struct PendingUpload
    {
        Buffer staging;
        VkDeviceSize bytes = 0;
        MeshHandle mesh;
        TextureHandle texture;
        // For a mesh, from the staging buffer to its vertex, index and skin buffers in turn.
        std::array<VkDeviceSize, 3> bufferBytes{};
        // For a texture, one copy per mip level.
        std::vector<VkBufferImageCopy> levels;
        // Rows of a texture already on the GPU, which frames before still read.
        bool update = false;
    };

    // Timestamps a frame can write: one per pass of the render graph, and one more.
    static constexpr std::uint32_t maxTimestamps = 128;
    // The largest side of the image a pick is drawn into.
    static constexpr std::uint32_t maxPickSize = 512;
    // The largest side of a captured picture.
    static constexpr std::uint32_t maxCaptureSize = 512;

    struct FrameContext
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        // Signaled once the frame's commands have completed.
        VkFence completed = VK_NULL_HANDLE;
        // Signaled when the acquired swapchain image is ready to be rendered to.
        VkSemaphore imageAcquired = VK_NULL_HANDLE;
        // One timestamp before each pass of the graph and one after the last, read once the GPU
        // has finished the frame; the names are those of the passes that follow each timestamp.
        VkQueryPool timestamps = VK_NULL_HANDLE;
        std::uint32_t timestampCount = 0;
        std::vector<const char*> timedPasses;
        // The frame of the profiler the timestamps belong to.
        std::uint64_t profiledFrame = 0;
        // Data read by shaders through device addresses. Each frame keeps its own copies, so
        // writing them never touches memory a frame in flight reads.
        std::optional<Buffer> sceneData;
        std::optional<Buffer> materials;
        // The uniforms of the materials drawn by shaders of projects.
        std::optional<Buffer> materialParameters;
        std::uint64_t materialVersion = 0;
        std::optional<Buffer> lights;
        // The bone matrices of every skinned instance of the frame.
        std::optional<Buffer> bones;
        // The values objects give the instance uniforms of their shaders.
        std::optional<Buffer> instanceParameters;
        // The particles of the frame, those of a blended batch farthest first, and the points and
        // segments of its ribbons.
        std::optional<Buffer> particles;
        std::optional<Buffer> trailPoints;
        std::optional<Buffer> trailSegments;
        // The sprites of the frame the camera can see, in the order they are drawn, and the 2D lights
        // that reach them with the distances of their shadows.
        std::optional<Buffer> sprites;
        std::optional<Buffer> lights2D;
        std::optional<Buffer> shadows2D;
        std::optional<Buffer> clusters;
        std::optional<Buffer> clusterLights;
        // The reflection probes the frame reflects, the smallest first, and its irradiance volumes.
        std::optional<Buffer> reflectionProbes;
        std::optional<Buffer> irradianceVolumes;
        // The instances the GPU culls and the views it culls them in; the indirect draw of each batch
        // in each view, copied from the host before the culling counts its instances; and the slots
        // of the instances each view kept, a row of them per view.
        std::optional<Buffer> cullInstances;
        std::optional<Buffer> cullViews;
        std::optional<Buffer> cullCommandUpload;
        std::optional<Buffer> cullCommands;
        std::optional<Buffer> cullVisible;
        // The terrains of the frame, the blocks of them it draws and the tiles of their grass.
        std::optional<Buffer> terrains;
        std::optional<Buffer> terrainNodes;
        std::optional<Buffer> grassTiles;
        // Exposed luminance measured on a grid by the last frame recorded with this context.
        std::optional<Buffer> luminance;
        bool luminanceMeasured = false;
        // The exposure that frame used, to turn measured values back into nits.
        float exposure = 1.0f;
        TransientImagePool images;
        DescriptorSets::FrameImages boundImages;
        // Where every skinned instance stood on the previous frame, for the motion of its pixels.
        std::optional<Buffer> previousBones;
        // The views the local lights were given in the shadow atlas.
        std::optional<Buffer> shadowViews;
        std::optional<Buffer> overlayVertices;
        // The triangles of the interface of the frame.
        std::optional<Buffer> uiVertices;
        std::optional<Buffer> uiIndices;
        // The object identifiers the frame read in the requested rectangle: x, y, width and height
        // in pixels of the scene image, drawn into an image of pickExtent, smaller for a large one.
        std::optional<Buffer> pickReadback;
        std::optional<std::uint64_t> pickRequest;
        std::array<std::uint32_t, 4> pickRect{};
        math::Extent2D pickExtent{1, 1};
        // The picture of the scene the frame drew for a capture, in the format of the target.
        // The air and the fog of the frame, which the shaders read through the scene data.
        std::optional<Buffer> atmosphere;
        std::optional<Buffer> captureReadback;
        // The light a radiance view drew for a capture, as half floats, and the exposure it was drawn at.
        std::optional<Buffer> radianceReadback;
        bool captureRadiance = false;
        std::optional<std::uint64_t> captureRequest;
        math::Extent2D captureExtent{1, 1};
        VkFormat captureFormat = VK_FORMAT_UNDEFINED;
        // The whole window the frame presents, interface and tools included, for a window capture.
        std::optional<Buffer> windowReadback;
        std::optional<std::uint64_t> windowRequest;
        math::Extent2D windowExtent{1, 1};
        VkFormat windowFormat = VK_FORMAT_UNDEFINED;
        // The copies the frame recorded, whose staging buffers wait for the frame to complete.
        std::vector<PendingUpload> uploads;
    };

    // What a view carries from one frame to the next. The renderer holds that of the view it records;
    // the others wait here, swapped in while theirs are recorded.
    struct ViewState
    {
        std::array<std::optional<Image>, 2> history;
        std::array<ImageState, 2> historyStates{ImageState::Undefined, ImageState::Undefined};
        math::Extent2D historyExtent;
        bool historyValid = false;
        math::Mat4 previousViewProjection{1.0f};
        float ev100 = 14.0f;
        float targetEv100 = 14.0f;
        bool exposureInitialized = false;
        std::chrono::steady_clock::time_point lastExposureUpdate;
    };

    // A view of RenderWorld::views: its state, its own frame contexts and descriptor sets, and the
    // image it is drawn into for each frame context, which the main frame of that context shows.
    struct ExtraView
    {
        std::uint32_t id = 0;
        // Its descriptor sets come after those of the main frame and of the views before it.
        std::uint32_t setBase = 0;
        ViewState state;
        std::array<FrameContext, framesInFlight> frames{};
        std::array<std::optional<Image>, framesInFlight> images;
        std::array<ImageState, framesInFlight> imageStates{ImageState::Undefined, ImageState::Undefined};
        math::Extent2D imageExtent;
        bool interface = false;
        // Draws the light for a capture: RenderView::radiance.
        bool radiance = false;
        // The frame it was last drawn in: the tools show it only in that frame.
        std::uint64_t drawnFrame = ~std::uint64_t{0};
    };

    struct SubmeshRange
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
    };

    struct GpuMesh
    {
        // The box holding the mesh, which the culling tests against the views.
        math::Aabb bounds;
        Buffer vertices;
        Buffer indices;
        std::vector<SubmeshRange> submeshes;
        // Only for a skinned mesh: the joints and weights of its vertices.
        std::optional<Buffer> skin;
        // False until a frame has recorded the copy of its data: it is not drawn before.
        bool ready = false;
    };

    // A destroyed mesh waiting for the frames that may still reference it.
    struct RetiredMesh
    {
        GpuMesh mesh;
        std::uint64_t retiredAtFrame = 0;
    };

    struct GpuTexture
    {
        Image image;
        std::uint32_t slot = 0;
        // Read at its nearest pixel, for pixel art.
        bool nearest = false;
        // As for a mesh; the textures of the renderer itself are copied at once.
        bool ready = true;
    };

    struct RetiredTexture
    {
        GpuTexture texture;
        std::uint64_t retiredAtFrame = 0;
    };

    // The reflections of a probe, prefiltered for every roughness, and its place in the global set.
    struct ProbeMap
    {
        Image reflections;
        std::uint32_t slot = 0;
        // The values of the capture are its light in nits times this.
        float scale = 1.0f;
    };

    struct RetiredProbeMap
    {
        ProbeMap map;
        std::uint64_t retiredAtFrame = 0;
    };

    // The light of the probes of an irradiance volume, as the shaders read it.
    struct VolumeProbes
    {
        Buffer probes;
        std::uint32_t countX = 2;
        std::uint32_t countY = 2;
        std::uint32_t countZ = 2;
    };

    struct RetiredVolumeProbes
    {
        VolumeProbes volume;
        std::uint64_t retiredAtFrame = 0;
    };

    struct RetiredEnvironment
    {
        EnvironmentMaps maps;
        std::uint64_t retiredAtFrame = 0;
        // The sky a sky shader drew, which the maps were baked from.
        std::optional<Image> sky;
    };

    // The pipelines of a shader of a project, for the passes its kind draws in: a spatial one has
    // them all but the prepass when it blends, and no shadows when it casts none; a canvas_item one
    // has scene, pick and mask; a particles one scene; a sky one scene and bake.
    struct GpuShader
    {
        asset::ShaderKind kind = asset::ShaderKind::Spatial;
        // Increased by every build, so that what was baked from an older one is baked again.
        std::uint64_t revision = 0;
        bool transparent = false;
        bool castsShadows = false;
        // The slots of four numbers its uniforms take.
        std::uint32_t parameterCount = 0;
        std::optional<Pipeline> scene;
        std::optional<Pipeline> prepass;
        std::optional<Pipeline> shadow;
        std::optional<Pipeline> localShadow;
        std::optional<Pipeline> pick;
        std::optional<Pipeline> mask;
        std::optional<Pipeline> bake;
    };

    struct RetiredShader
    {
        GpuShader shader;
        std::uint64_t retiredAtFrame = 0;
    };

    // The passes sprites are drawn in.
    enum class SpritePass : std::uint8_t
    {
        Scene,
        Pick,
        Mask,
    };

    VulkanRenderer(platform::Window& window, const RendererConfig& config,
                   std::filesystem::path shaderDirectory, Instance instance, Surface surface,
                   Device device, Allocator allocator, UploadContext upload) noexcept;

    [[nodiscard]] core::Result<void> createFrameContexts();
    [[nodiscard]] core::Result<void> createFrameContext(FrameContext& frame);
    void destroyFrameContext(FrameContext& frame) noexcept;
    // Exchanges what the renderer carries for the view it records with what a view waits with.
    void swapViewState(ViewState& state) noexcept;
    // Everything a view needs before it is recorded, for the camera of RenderWorld::camera at the size
    // of m_sceneExtent: lights and their clusters, bones, particles, shadow cascades, history, jitter,
    // scene data and sprites. Answers whether the sun casts shadows.
    [[nodiscard]] core::Result<bool> prepareScene(FrameContext& frame, bool main);
    // Draws the views of the frame, each in its own submission, before the main frame shows them.
    [[nodiscard]] core::Result<void> renderViews(std::uint32_t frameSlot);
    [[nodiscard]] core::Result<void> renderView(ExtraView& view, const RenderView& wanted, std::uint32_t frameSlot);
    // The descriptor set of the frame context for the view being recorded.
    [[nodiscard]] VkDescriptorSet frameDescriptors(std::uint32_t frameSlot) const noexcept;
    // Hands the GPU times of the passes of a finished frame to the profiler.
    void reportPassTimes(FrameContext& frame);
    [[nodiscard]] core::Result<void> createDefaultResources();
    [[nodiscard]] core::Result<void> createScenePipelines();
    // Pipelines drawing into the swapchain or the viewport image, which share its format.
    [[nodiscard]] core::Result<void> createTargetPipelines(VkFormat format);
    [[nodiscard]] core::Result<void> recreateSwapchain(math::Extent2D windowPixelSize);
    // Copies the texture at once and waits for the copy: for the textures of the renderer itself.
    [[nodiscard]] core::Result<GpuTexture> uploadTexture(const asset::TextureData& texture, std::uint32_t slot);
    // Creates the image and fills a staging buffer with its levels, for a later copy.
    [[nodiscard]] core::Result<std::pair<GpuTexture, PendingUpload>> prepareTexture(const asset::TextureData& texture,
                                                                                    std::uint32_t slot);
    // Moves the waiting copies that fit the budget of the frame into it, and makes their meshes
    // and textures ready; then leaves out what the frame would draw with what is still waiting.
    void selectUploads(FrameContext& frame);
    // Leaves out of RenderWorld the meshes that are not on the GPU, and the batches of interfaces and
    // particles that read textures that are not there yet.
    void leaveOutWaiting();
    // Records the copies selected for the frame.
    void recordUploads(VkCommandBuffer commandBuffer, const FrameContext& frame) const;
    [[nodiscard]] GpuMaterial toGpuMaterial(const MaterialDesc& material) const noexcept;
    // The pipelines of a compiled shader.
    [[nodiscard]] core::Result<GpuShader> buildShader(const asset::ShaderData& shader) const;
    // The shader of a kind that draws a material, or null for the standard one.
    [[nodiscard]] const GpuShader* shaderOf(MaterialHandle material, asset::ShaderKind kind) const noexcept;
    [[nodiscard]] const GpuShader* shaderOf(const MaterialDesc* material, asset::ShaderKind kind) const noexcept;
    // The index of a material in the buffer of the frame: the default one for a missing material.
    [[nodiscard]] std::uint32_t materialIndex(MaterialHandle material) const noexcept;
    // Refreshes the frame's copy of the materials when it is older than the current ones.
    [[nodiscard]] core::Result<void> updateFrameMaterials(FrameContext& frame);
    // Bakes image based lighting when the sky texture changed.
    [[nodiscard]] core::Result<void> updateEnvironment();
    // Bakes it from the sky shader of the frame, once its scene data is written, when the shader,
    // its uniforms or the sun changed: the sun at most twice a second.
    [[nodiscard]] core::Result<void> updateSkyShaderEnvironment(const FrameContext& frame);
    // The sky shader of the frame that lights the scene, or null.
    [[nodiscard]] const GpuShader* skyShader() const noexcept;
    void updateExposure(FrameContext& frame);
    [[nodiscard]] core::Result<void> uploadLights(FrameContext& frame, float aspectRatio);
    void writeSceneData(FrameContext& frame, const std::optional<ShadowCascades>& cascades) const noexcept;
    // The air and the fog of the frame into its buffer and the scene data, with what they were made of.
    void writeAtmosphere(FrameContext& frame, GpuSceneData& scene) const noexcept;
    [[nodiscard]] core::Result<OverlayRanges> uploadOverlay(FrameContext& frame);
    [[nodiscard]] core::Result<void> uploadUi(FrameContext& frame, bool tools, std::vector<std::uint32_t>& surfaceIndexBases,
                                            std::uint32_t& toolsIndexBase);
    [[nodiscard]] core::Result<void> uploadPreviousBones(FrameContext& frame) const;
    // Gives the local lights that cast shadows a tile of the atlas, the most important first, and
    // builds the view each tile is drawn through. Lights that do not fit keep their light alone.
    void assignShadowViews();
    [[nodiscard]] core::Result<void> uploadShadowViews(FrameContext& frame) const;
    // The reflection probes the camera may see, the smallest first, into the buffer of the frame;
    // none for a radiance view.
    [[nodiscard]] core::Result<void> uploadReflectionProbes(FrameContext& frame);
    // The irradiance volumes the camera may see, the smallest first, into the buffer of the frame.
    [[nodiscard]] core::Result<void> uploadIrradianceVolumes(FrameContext& frame);
    // Creates the two images the antialiasing carries from frame to frame, when the scene changed
    // size or they do not exist yet.
    [[nodiscard]] core::Result<void> ensureHistory();
    // Remembers where the instances of this frame stood, for the motion of the next one.
    void rememberFrame();
    // Keeps the answer of the pick request the frame recorded, now that it completed.
    void readPickResult(FrameContext& frame);
    // Keeps the picture the frame captured, now that it completed.
    void readCapture(FrameContext& frame);
    void readWindowCapture(FrameContext& frame);
    // What the shadows of the frame are drawn from, hashed: the instances, their bones and the
    // materials. None when a shader of the project draws them, which may move them every frame.
    [[nodiscard]] std::optional<std::uint64_t> shadowCastersKey() const;
    [[nodiscard]] core::Result<void> ensureKeptShadows();
    // Returns the number of draw calls recorded. With a view, records it into its image rather than
    // the main frame with its tools.
    [[nodiscard]] core::Result<std::uint32_t> recordFrame(FrameContext& frame, std::uint32_t frameSlot,
                                                          std::uint32_t imageIndex, bool drawShadows,
                                                          ExtraView* extraView = nullptr);
    // Whether the instance blends with what is behind it, and so belongs to the transparent pass.
    [[nodiscard]] bool isBlended(const MeshInstance& instance) const noexcept;
    // The instances a pass draws, in the order it draws them: those its view can see.
    [[nodiscard]] std::span<const std::uint32_t> instancesOf(MeshPass pass,
                                                             std::uint32_t cascade) const;
    // The box an instance takes in the world, grown for a skinned mesh whose pose has left its
    // bind pose behind.
    [[nodiscard]] math::Aabb worldBounds(const MeshInstance& instance) const noexcept;
    // Where an instance of the previous frame stood, and where its bones were.
    struct PreviousInstance
    {
        std::uint64_t key = 0;
        math::Mat4 transform{1.0f};
        std::uint32_t firstBone = 0;
        bool skinned = false;
    };
    // Where the instance at this index of the world stood on the previous frame; none if it was not there.
    [[nodiscard]] const PreviousInstance* previousInstance(std::uint32_t index) const;
    // The key an instance keeps between frames: its entity and the submesh it draws.
    [[nodiscard]] static std::uint64_t instanceKey(const MeshInstance& instance) noexcept
    {
        return (static_cast<std::uint64_t>(instance.objectId) << 32) | instance.submesh;
    }
    // Whether the GPU culls and draws the instance: a mesh without bones, drawn opaque or cut out by
    // a standard material.
    [[nodiscard]] bool isGpuDriven(const MeshInstance& instance) const noexcept;
    // The instances the GPU culls this frame, in batches of the same submesh and material, and the views
    // it culls them in: the camera, the cascades and the local lights drawn, and the pick requested.
    // Nothing when the GPU culls nothing, the CPU then culling and drawing every instance.
    [[nodiscard]] core::Result<void> prepareCulling(FrameContext& frame, bool drawCascades, bool drawAtlas,
                                                    bool pick);
    // Copies the draws of the batches to the GPU and culls the instances in every view.
    void recordCulling(VkCommandBuffer commandBuffer, const FrameContext& frame) const;
    // The batches of the pass, each drawn at once with the instances its view kept.
    std::uint32_t drawCulled(VkCommandBuffer commandBuffer, const FrameContext& frame, VkDeviceAddress sceneData,
                             MeshPass pass, std::uint32_t cascade, std::uint32_t frameSlot) const;
    // The instances of the pass, or only those given, in their order.
    std::uint32_t drawMeshes(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData,
                             VkDeviceAddress boneMatrices, MeshPass pass, std::uint32_t cascade,
                             std::uint32_t frameSlot, VkDeviceAddress previousBones = 0,
                             std::span<const std::uint32_t> only = {}) const;
    // The blocks of the terrains the camera sees, each with the detail its distance calls for, and the
    // tiles of their grass around the camera, copied into the buffers of the frame.
    [[nodiscard]] core::Result<void> uploadTerrains(FrameContext& frame);
    // The terrains in a pass that draws meshes: their blocks; nothing in passes they are not in.
    std::uint32_t drawTerrains(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData, MeshPass pass, std::uint32_t cascade,
                               std::uint32_t frameSlot) const;
    // Their grass, in the scene pass.
    std::uint32_t drawGrass(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData, std::uint32_t frameSlot) const;
    // The blended instances and the particle batches, farthest first, over the opaque scene.
    std::uint32_t drawTransparent(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData,
                                  VkDeviceAddress boneMatrices, const FrameContext& frame,
                                  std::uint32_t frameSlot) const;
    std::uint32_t drawParticles(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData,
                                const FrameContext& frame, std::uint32_t frameSlot, const ParticleDraw& draw) const;
    // Copies the particles and the ribbons of the frame into its buffers, sorting blended batches.
    [[nodiscard]] core::Result<void> uploadParticles(FrameContext& frame) const;
    // Copies the sprites the camera sees into the buffer of the frame, in the order they are drawn.
    [[nodiscard]] core::Result<void> uploadSprites(FrameContext& frame) const;
    // The 2D lights that reach what the camera sees, and the shadows of those that cast them.
    void gatherLights2D() const;
    // Sprites [first, first + count) of the sorted buffer, in runs drawn by the same shader, each with
    // its pipeline for the pass.
    std::uint32_t drawSprites(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData, const FrameContext& frame,
                              std::uint32_t frameSlot, SpritePass pass, std::uint32_t first, std::uint32_t count) const;
    // One run of them, with one pipeline.
    std::uint32_t drawSpriteRun(VkCommandBuffer commandBuffer, VkDeviceAddress sceneData, const FrameContext& frame,
                                std::uint32_t frameSlot, const Pipeline& pipeline, bool frameSet, std::uint32_t first,
                                std::uint32_t count) const;
    // How far a point is, for the order of the blended draws: its squared distance from a
    // perspective camera, its depth along an orthographic one. Greater is drawn first.
    [[nodiscard]] float sortDistance(math::Vec3 point, math::Vec3 cameraPosition) const noexcept;
    // Copies the bone matrices of the frame's skinned instances into its buffer, and the values of
    // the instance uniforms into theirs.
    [[nodiscard]] core::Result<void> uploadBones(FrameContext& frame) const;
    [[nodiscard]] core::Result<void> ensureHostBuffer(std::optional<Buffer>& buffer, VkDeviceSize bytes) const;
    // The same with the use given, mapped or only in video memory.
    [[nodiscard]] core::Result<void> ensureBuffer(std::optional<Buffer>& buffer, VkDeviceSize bytes, VkBufferUsageFlags usage,
                                                  bool hostVisible) const;
    void releaseRetiredResources() noexcept;
    void destroyPresentSemaphores() noexcept;

    platform::Window& m_window;
    PresentMode m_requestedPresentMode;
    std::filesystem::path m_shaderDirectory;
    Instance m_instance;
    Surface m_surface;
    Device m_device;
    // Everything allocated through VMA is declared below, so it is destroyed before the allocator.
    Allocator m_allocator;
    UploadContext m_upload;
    std::optional<DescriptorSets> m_descriptors;
    std::unique_ptr<EnvironmentBaker> m_baker;
    // The tables and the light of the sky of an atmosphere; whether a frame drew one, and what its
    // air and its sky were made of.
    std::unique_ptr<AtmosphereRenderer> m_atmosphere;
    bool m_atmosphereBound = false;
    mutable std::uint64_t m_airKey = 0;
    mutable std::uint64_t m_skyKey = 0;
    mutable std::uint64_t m_viewKey = 0;
    std::optional<Image> m_brdfTable;
    // Absent while the window has no drawable area.
    std::optional<Swapchain> m_swapchain;
    std::optional<Pipeline> m_meshPipeline;
    std::optional<Pipeline> m_doubleSidedPipeline;
    std::optional<Pipeline> m_transparentPipeline;
    std::optional<Pipeline> m_transparentDoubleSidedPipeline;
    std::optional<Pipeline> m_prepassPipeline;
    std::optional<Pipeline> m_prepassDoubleSidedPipeline;
    std::optional<Pipeline> m_particlePipeline;
    std::optional<Pipeline> m_ribbonPipeline;
    std::optional<Pipeline> m_spritePipeline;
    std::optional<Pipeline> m_spritePickPipeline;
    std::optional<Pipeline> m_spriteMaskPipeline;
    // Something the blended pass draws, sorted by layer, order, then distance.
    struct TransparentItem
    {
        std::int32_t layer = 0;
        std::int32_t order = 0;
        float distance = 0.0f;
        // 0 for a mesh instance, 1 for a batch of particles, 2 for a sprite.
        std::uint8_t kind = 0;
        // The instance, the batch, or the place of the sprite in the sorted buffer.
        std::uint32_t index = 0;
    };
    // Sprites drawn together among the blended surfaces: one sprite, or the tiles of a tilemap.
    struct SpriteBatch
    {
        std::int32_t layer = 0;
        std::int32_t order = 0;
        float distance = 0.0f;
        // A sprite of RenderWorld::sprites, or a tilemap of RenderWorld::tilemaps.
        bool tilemap = false;
        std::uint32_t source = 0;
        // Where it is in the sorted buffer.
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };
    // Scratch for the sprites of the frame: the batches in drawing order, their GPU data in the
    // same order, and which of those are outlined.
    mutable std::vector<SpriteBatch> m_spriteBatches;
    mutable std::vector<GpuSprite> m_gpuSprites;
    // The canvas_item shader of each sprite of the buffer; invalid for the standard one.
    mutable std::vector<ShaderHandle> m_spriteShaders;
    mutable std::vector<std::uint8_t> m_spriteOutlined;
    mutable std::vector<GpuLight2D> m_gpuLights2D;
    mutable std::vector<float> m_shadows2D;
    mutable std::vector<TransparentItem> m_transparentItems;
    // Scratch for the particles of the frame, kept between frames to avoid allocating.
    mutable std::vector<GpuParticle> m_gpuParticles;
    mutable std::vector<GpuTrailPoint> m_gpuTrailPoints;
    mutable std::vector<std::pair<float, std::uint32_t>> m_particleOrder;
    mutable std::vector<std::uint32_t> m_transparentRun;
    // Blended instances of the frame, farthest first; kept between frames to avoid allocating.
    mutable std::vector<std::pair<float, std::uint32_t>> m_transparentOrder;
    mutable std::vector<std::uint32_t> m_passInstances;
    // Where the camera of the frame is, which sorts the blended instances.
    mutable math::Vec3 m_cameraPosition{0.0f};
    // What the camera and each shadow cascade can see, rebuilt every frame.
    mutable Frustum m_cameraFrustum;
    mutable std::array<Frustum, cascadeCount> m_cascadeFrustums{};
    mutable std::array<math::Mat4, cascadeCount> m_cascadeViewProjections{};
    // The shadows of the main image, kept from a frame to the next and drawn again only when what
    // they are drawn from changed: the key of what drew them, none while they must be drawn.
    std::optional<Image> m_keptCascades;
    std::optional<Image> m_keptAtlas;
    ImageState m_keptCascadesState = ImageState::Undefined;
    ImageState m_keptAtlasState = ImageState::Undefined;
    std::optional<std::uint64_t> m_keptCascadesKey;
    std::optional<std::uint64_t> m_keptAtlasKey;
    std::uint32_t m_keptShadowPasses = 0;
    // Instances kept and dropped by the culling of the frame, for the statistics.
    mutable std::uint32_t m_culledInstances = 0;
    // The views the local lights were given this frame, and what each one draws.
    std::vector<GpuShadowView> m_shadowViews;
    std::vector<Frustum> m_shadowViewFrustums;
    std::uint32_t m_lastCulledInstances = 0;
    // Where each instance stood on the previous frame, in the order the scene listed them. A scene
    // usually lists its instances in the same order from a frame to the next, which finds each at its
    // own index; the others are looked for by key, in a map built the first time one is. An instance
    // that was not there yet simply does not move.
    std::vector<PreviousInstance> m_previousInstances;
    mutable std::unordered_map<std::uint64_t, std::uint32_t> m_previousIndices;
    mutable bool m_previousIndexed = false;
    std::vector<math::Mat4> m_previousBoneMatrices;
    // The view and projection of the previous frame, without its jitter.
    math::Mat4 m_previousViewProjection{1.0f};
    mutable math::Mat4 m_unjitteredViewProjection{1.0f};
    // How far the projection of this frame is moved, in clip space.
    math::Vec2 m_jitter{0.0f};
    std::optional<Pipeline> m_shadowPipeline;
    std::optional<Pipeline> m_terrainPrepassPipeline;
    std::optional<Pipeline> m_terrainPipeline;
    std::optional<Pipeline> m_terrainShadowPipeline;
    std::optional<Pipeline> m_terrainLocalShadowPipeline;
    std::optional<Pipeline> m_terrainPickPipeline;
    std::optional<Pipeline> m_grassPipeline;
    // The triangles of a block of a terrain, a quarter after another, and those of a blade of grass.
    std::optional<Buffer> m_terrainGrid;
    std::optional<Buffer> m_grassBlade;
    // What the frame being recorded draws of each terrain: its blocks drawn whole, then those of
    // which one quarter is drawn, by quarter; its tiles of grass near, farther and far, with their
    // blades and how wide these are to cover for the fewer of them.
    struct TerrainDraw
    {
        std::uint32_t terrain = 0;
        std::array<std::uint32_t, 5> firstNode{};
        std::array<std::uint32_t, 5> nodeCount{};
        bool castShadows = true;
        std::array<std::uint32_t, 3> firstTile{};
        std::array<std::uint32_t, 3> tileCount{};
        std::array<std::uint32_t, 3> blades{};
        std::array<float, 3> bladeWidth{};
    };
    std::vector<TerrainDraw> m_terrainDraws;
    std::vector<GpuTerrain> m_gpuTerrains;
    std::vector<GpuTerrainNode> m_terrainNodes;
    std::vector<GpuTerrainNode> m_grassTiles;
    VkDeviceAddress m_frameTerrains = 0;
    VkDeviceAddress m_frameTerrainNodes = 0;
    VkDeviceAddress m_frameGrassTiles = 0;
    std::optional<Pipeline> m_skyPipeline;
    std::optional<Pipeline> m_luminancePipeline;
    std::optional<Pipeline> m_pickPipeline;
    std::optional<Pipeline> m_selectionMaskPipeline;
    std::optional<Pipeline> m_tonemapPipeline;
    std::optional<Pipeline> m_sceneLinePipeline;
    std::optional<Pipeline> m_outlinePipeline;
    std::optional<Pipeline> m_overlayLinePipeline;
    std::optional<Pipeline> m_overlayTrianglePipeline;
    std::optional<Pipeline> m_uiPipeline;
    // The same, for the interface surfaces of the tools, drawn in the format of the tools.
    std::optional<Pipeline> m_uiDisplayPipeline;
    std::optional<Pipeline> m_taaPipeline;
    std::optional<Pipeline> m_aoPipeline;
    std::optional<Pipeline> m_localShadowPipeline;
    // The culling of the instances on the GPU, and the passes that draw what it kept.
    std::optional<Pipeline> m_cullPipeline;
    std::optional<Pipeline> m_culledScenePipeline;
    std::optional<Pipeline> m_culledSceneDoubleSidedPipeline;
    std::optional<Pipeline> m_culledPrepassPipeline;
    std::optional<Pipeline> m_culledPrepassDoubleSidedPipeline;
    std::optional<Pipeline> m_culledShadowPipeline;
    std::optional<Pipeline> m_culledLocalShadowPipeline;
    std::optional<Pipeline> m_culledPickPipeline;
    // Whether the GPU culls the instances it can draw: asked by RendererConfig::gpuCulling, and
    // possible on the device.
    bool m_gpuCulling = false;
    // What the culling of the frame being recorded found: the instances it took from the CPU, its
    // batches, and the row of each of its views, none for a view it does not cull.
    struct CullBatch
    {
        VkBuffer indices = VK_NULL_HANDLE;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        std::uint32_t material = 0;
        bool doubleSided = false;
    };
    struct CullBatchKey
    {
        std::uint32_t mesh = 0;
        std::uint32_t submesh = 0;
        std::uint32_t material = 0;

        bool operator==(const CullBatchKey&) const = default;
    };
    struct CullBatchHash
    {
        [[nodiscard]] std::size_t operator()(const CullBatchKey& key) const noexcept
        {
            return std::hash<std::uint64_t>{}(((std::uint64_t{key.mesh} << 32) | key.material) * 31 + key.submesh);
        }
    };
    static constexpr std::uint32_t noCullView = 0xFFFFFFFFU;
    std::vector<std::uint8_t> m_gpuDriven;
    // The instances left to the CPU, in their order.
    std::vector<std::uint32_t> m_cpuInstances;
    std::vector<CullBatch> m_cullBatches;
    std::vector<GpuInstance> m_gpuInstances;
    std::vector<GpuCullView> m_cullViews;
    std::vector<VkDrawIndexedIndirectCommand> m_cullCommands;
    std::unordered_map<CullBatchKey, std::uint32_t, CullBatchHash> m_batchIndices;
    std::uint32_t m_culledCount = 0;
    std::uint32_t m_cameraCullView = noCullView;
    std::uint32_t m_cascadeCullView = noCullView;
    std::uint32_t m_localCullView = noCullView;
    std::uint32_t m_pickCullView = noCullView;
    std::optional<Pipeline> m_bloomThresholdPipeline;
    std::optional<Pipeline> m_bloomDownsamplePipeline;
    // The depth of field and the motion blur, in the order of their passes.
    std::optional<Pipeline> m_circleOfConfusionPipeline;
    std::optional<Pipeline> m_dofPrefilterPipeline;
    std::optional<Pipeline> m_bokehPipeline;
    std::optional<Pipeline> m_dofFilterPipeline;
    std::optional<Pipeline> m_dofCombinePipeline;
    std::optional<Pipeline> m_motionTilesPipeline;
    std::optional<Pipeline> m_motionNeighboursPipeline;
    std::optional<Pipeline> m_motionBlurPipeline;
    std::optional<Pipeline> m_bloomUpsamplePipeline;
    // Bound as the selection mask of frames that outline nothing.
    std::optional<Image> m_emptySelectionMask;
    std::optional<Image> m_whiteImage;
    // The same, left in the general layout, for the bloom chain a frame did not build.
    std::optional<Image> m_whiteGeneralImage;
    // Stands in for the shadow atlas when no local light casts a shadow.
    std::optional<Image> m_emptyShadowAtlas;
    // What the antialiasing resolved, kept from one frame to the next: one image is read while the
    // other is written, and they swap every frame.
    std::array<std::optional<Image>, 2> m_history;
    std::array<ImageState, 2> m_historyStates{ImageState::Undefined, ImageState::Undefined};
    math::Extent2D m_historyExtent;
    // False until a frame has resolved into the history, and whenever it was just recreated.
    bool m_historyValid = false;
    math::Extent2D m_swapchainWindowPixelSize;
    // The size of the scene image of the frame being recorded.
    math::Extent2D m_sceneExtent;
    bool m_swapchainOutdated = false;
    std::array<FrameContext, framesInFlight> m_frames{};
    // The views drawn beside the main image, kept by their identifiers from frame to frame.
    std::vector<std::unique_ptr<ExtraView>> m_extraViews;
    // Where the descriptor sets of the view being recorded start: 0 for the main frame.
    std::uint32_t m_setBase = 0;
    // The draw calls of the views of the frame, counted with those of the main frame.
    std::uint32_t m_viewDrawCalls = 0;
    // One per swapchain image: a presented image keeps its semaphore busy until it is replaced.
    std::vector<VkSemaphore> m_presentSemaphores;

    // Meshes and textures created and not yet copied to the GPU, oldest first.
    std::deque<PendingUpload> m_pendingUploads;
    std::uint64_t m_pendingUploadBytes = 0;
    std::uint64_t m_uploadBytesPerFrame = 0;
    std::uint64_t m_lastUploadedBytes = 0;

    core::SlotMap<GpuMesh, MeshTag> m_meshes;
    std::vector<RetiredMesh> m_retiredMeshes;
    std::optional<GpuTexture> m_whiteTexture;
    std::optional<GpuTexture> m_flatNormalTexture;
    std::optional<GpuTexture> m_blackTexture;
    core::SlotMap<GpuTexture, TextureTag> m_textures;
    std::vector<RetiredTexture> m_retiredTextures;
    // Bindless slots released by destroyed textures, reused before new ones.
    std::vector<std::uint32_t> m_freeTextureSlots;
    std::uint32_t m_nextTextureSlot = blackTextureSlot + 1;
    // Where the slots of the tools' images start; the textures take those before.
    std::uint32_t m_toolImageSlotBase = 0;
    core::SlotMap<ProbeMap, ReflectionProbeTag> m_probeMaps;
    std::vector<RetiredProbeMap> m_retiredProbeMaps;
    std::vector<std::uint32_t> m_freeProbeSlots;
    std::uint32_t m_nextProbeSlot = 0;
    // The reflection probes of the frame being recorded, and whether it draws a radiance view.
    std::uint32_t m_frameProbeCount = 0;
    bool m_radianceView = false;
    mutable std::vector<std::pair<float, GpuReflectionProbe>> m_gpuProbes;
    core::SlotMap<VolumeProbes, IrradianceVolumeTag> m_volumes;
    std::vector<RetiredVolumeProbes> m_retiredVolumes;
    std::uint32_t m_frameVolumeCount = 0;
    std::vector<std::pair<float, GpuIrradianceVolume>> m_gpuVolumes;
    core::SlotMap<MaterialDesc, MaterialTag> m_materials;
    MaterialHandle m_defaultMaterial;
    // GPU form of every material slot, rebuilt when a material or a texture changes.
    std::vector<GpuMaterial> m_gpuMaterials;
    // The uniforms of the materials drawn by shaders of projects, rebuilt with them.
    std::vector<math::Vec4> m_gpuMaterialParameters;
    // The shaders of projects, and those destroyed or replaced that frames in flight may still use.
    core::SlotMap<GpuShader, ShaderTag> m_shaders;
    std::vector<RetiredShader> m_retiredShaders;
    // The TIME of shaders: seconds since the renderer started, on this frame and the previous one.
    std::chrono::steady_clock::time_point m_startTime = std::chrono::steady_clock::now();
    float m_time = 0.0f;
    float m_previousTime = 0.0f;
    std::uint64_t m_materialVersion = 1;
    bool m_materialsChanged = true;

    // Image based lighting of a uniform white sky, and of the current sky texture.
    std::optional<EnvironmentMaps> m_uniformEnvironment;
    std::optional<EnvironmentMaps> m_textureEnvironment;
    TextureHandle m_environmentSource;
    TextureHandle m_failedEnvironment;
    bool m_environmentBound = false;
    // Image based lighting baked from a sky shader, the sky it drew, and what they were baked from:
    // the shader, its material and uniforms; then the sun, which may change every frame.
    std::optional<EnvironmentMaps> m_shaderEnvironment;
    std::optional<Image> m_shaderSky;
    std::uint64_t m_shaderSkyKey = 0;
    std::uint64_t m_shaderSkyLight = 0;
    std::chrono::steady_clock::time_point m_shaderSkyBaked;
    std::uint64_t m_shaderRevisions = 0;
    std::vector<RetiredEnvironment> m_retiredEnvironments;

    std::uint64_t m_frameIndex = 0;
    // Nanoseconds per timestamp tick, 0 when the queue writes none; and the bits it writes.
    double m_timestampPeriod = 0.0;
    std::uint64_t m_timestampMask = ~std::uint64_t{0};
    RenderWorld m_world;
    LightClusters m_clusters;
    std::vector<GpuLight> m_gpuLights;
    std::optional<ShadowCascades> m_cascades;
    std::vector<float> m_luminanceSamples;
    float m_ev100 = 14.0f;
    float m_targetEv100 = 14.0f;
    bool m_exposureInitialized = false;
    std::chrono::steady_clock::time_point m_lastExposureUpdate;
    std::vector<PickResult> m_pickResults;
    struct CaptureRequest
    {
        std::uint64_t id = 0;
        std::uint32_t maxWidth = 0;
        std::uint32_t maxHeight = 0;
        // A view of RenderWorld::views, or the main image.
        std::optional<std::uint32_t> view;
    };
    std::deque<CaptureRequest> m_captureRequests;
    // A picture of the whole window, taken by the next frame presented.
    std::optional<std::uint64_t> m_windowCaptureRequest;
    std::uint64_t m_nextCapture = 1;
    std::vector<CapturedImage> m_captures;
    std::uint32_t m_lastDrawCalls = 0;
    std::uint32_t m_lastLightCount = 0;
};

} // namespace devex::render::vulkan
