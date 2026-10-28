#pragma once

#include "DescriptorSets.hpp"
#include "Device.hpp"
#include "Memory.hpp"
#include "Pipeline.hpp"
#include "Upload.hpp"
#include "Vulkan.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace devex::render::vulkan {

// The tables of the atmosphere and the small maps of the light of its sky, filled on the GPU by the
// frames that draw an atmosphere, after Hillaire (2020): what the air lets through and the light it
// scatters many times, when the air changes; the sky and the haze around the camera, every frame;
// the sky as a cube map prefiltered for each roughness, and its irradiance, when the sky changed.
// Every image stays in the general layout, written by compute and sampled by the frames.
class AtmosphereRenderer
{
public:
    static constexpr std::uint32_t cubeSize = 64;
    static constexpr std::uint32_t cubeMipCount = 5;
    static constexpr std::uint32_t irradianceSize = 16;

    [[nodiscard]] static core::Result<std::unique_ptr<AtmosphereRenderer>> create(const Device& device, const Allocator& allocator,
                                                                                 UploadContext& upload, const DescriptorSets& descriptors,
                                                                                 const std::filesystem::path& shaderDirectory);

    ~AtmosphereRenderer();

    AtmosphereRenderer(const AtmosphereRenderer&) = delete;
    AtmosphereRenderer& operator=(const AtmosphereRenderer&) = delete;

    // Records the passes of a frame, before anything samples the tables: `air` is the address of its
    // GpuAtmosphere, `scene` of its scene data. The tables of the air are filled again when `airKey`
    // changes, the maps of the sky when `skyKey` does, the sky and the haze around the camera when
    // `viewKey` does: nothing at all while the camera and the sun stay where they are.
    void record(VkCommandBuffer commands, VkDescriptorSet global, VkDeviceAddress air, VkDeviceAddress scene, std::uint64_t airKey,
                std::uint64_t skyKey, std::uint64_t viewKey);

    [[nodiscard]] VkImageView transmittanceView() const noexcept;
    [[nodiscard]] VkImageView skyViewView() const noexcept;
    [[nodiscard]] VkImageView aerialPerspectiveView() const noexcept;
    // Cube maps, as the light of an environment is bound.
    [[nodiscard]] VkImageView specularView() const noexcept;
    [[nodiscard]] VkImageView irradianceView() const noexcept;

private:
    AtmosphereRenderer(const Device& device) noexcept;

    // A set of the atmosphere's own: the image a pass writes, and the ones it reads.
    [[nodiscard]] VkDescriptorSet makeSet(VkImageView table, VkImageView layers, VkImageView multiScattering, VkImageView cube);

    const Device& m_device;
    VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    std::optional<Pipeline> m_transmittance;
    std::optional<Pipeline> m_multiScattering;
    std::optional<Pipeline> m_skyView;
    std::optional<Pipeline> m_aerialPerspective;
    std::optional<Pipeline> m_skyCube;
    std::optional<Pipeline> m_prefilter;
    std::optional<Pipeline> m_irradiance;
    std::optional<Image> m_transmittanceTable;
    std::optional<Image> m_multiScatteringTable;
    std::optional<Image> m_skyViewTable;
    std::optional<Image> m_aerialTable;
    std::optional<Image> m_specularCube;
    std::optional<Image> m_irradianceCube;
    VkDescriptorSet m_transmittanceSet = VK_NULL_HANDLE;
    VkDescriptorSet m_multiScatteringSet = VK_NULL_HANDLE;
    VkDescriptorSet m_skyViewSet = VK_NULL_HANDLE;
    VkDescriptorSet m_aerialSet = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, cubeMipCount> m_cubeSets{};
    VkDescriptorSet m_irradianceSet = VK_NULL_HANDLE;
    // What the tables and the maps were last filled for.
    std::optional<std::uint64_t> m_airKey;
    std::optional<std::uint64_t> m_skyKey;
    std::optional<std::uint64_t> m_viewKey;
};

} // namespace devex::render::vulkan
