#pragma once

#include <devex/math/Math.hpp>

namespace devex::render {

// Snapshot of everything the renderer draws in one frame. Gameplay fills it between
// Renderer::beginFrame and Renderer::endFrame, and the renderer never reads gameplay state
// directly, so rendering can later move to its own thread without changing this contract.
struct RenderWorld
{
    // Linear RGBA color of the background.
    math::Vec4 clearColor{0.02f, 0.02f, 0.03f, 1.0f};
};

} // namespace devex::render
