#pragma once

#include <devex/core/SlotMap.hpp>
#include <devex/math/Math.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace devex::render {

struct MeshTag;
struct TextureTag;
struct MaterialTag;
// Refers to a mesh uploaded with Renderer::createMesh.
using MeshHandle = core::Handle<MeshTag>;
// Refers to a texture uploaded with Renderer::createTexture.
using TextureHandle = core::Handle<TextureTag>;
// Refers to a material created with Renderer::createMaterial.
using MaterialHandle = core::Handle<MaterialTag>;

struct RenderCamera
{
    // World to view transform: the inverse of the camera's world transform.
    math::Mat4 view{1.0f};
    float verticalFov = math::radians(60.0f);
    // Distance of the near plane. There is no far plane: depth is reversed and infinite.
    float nearPlane = 0.1f;
};

// One submesh of a mesh, drawn with a material.
struct MeshInstance
{
    MeshHandle mesh;
    std::uint32_t submesh = 0;
    // An invalid or destroyed material draws with the default material.
    MaterialHandle material;
    math::Mat4 transform{1.0f};
};

// Snapshot of everything the renderer draws in one frame. Gameplay fills it between
// Renderer::beginFrame and Renderer::endFrame, and the renderer never reads gameplay state
// directly, so rendering can later move to its own thread without changing this contract.
struct RenderWorld
{
    // Linear RGBA color of the background.
    math::Vec4 clearColor{0.02f, 0.02f, 0.03f, 1.0f};
    RenderCamera camera;
    // Direction in which the sunlight travels, in world space.
    math::Vec3 lightDirection{-0.4f, -1.0f, -0.3f};
    // Fraction of the light that reaches surfaces facing away from the sun.
    float ambient = 0.25f;
    std::vector<MeshInstance> meshes;

    // Restores the defaults while keeping allocated storage.
    void reset() noexcept
    {
        std::vector<MeshInstance> storage = std::move(meshes);
        storage.clear();
        *this = RenderWorld{};
        meshes = std::move(storage);
    }
};

} // namespace devex::render
