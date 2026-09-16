#pragma once

#include <chrono>
#include <cstdint>

namespace devex::runtime {

// Turns variable frame times into a whole number of fixed simulation steps. Time is accumulated
// in integer nanoseconds so that the number of steps never drifts.
class FixedTimestep
{
public:
    explicit FixedTimestep(
        std::chrono::nanoseconds step,
        std::chrono::nanoseconds maxFrameTime = std::chrono::milliseconds(250)) noexcept;

    [[nodiscard]] static FixedTimestep fromRate(std::uint32_t hertz) noexcept;

    // Accumulates a frame and returns how many fixed steps to run. Frames longer than
    // maxFrameTime are clamped: a slow machine then slows the simulation down instead of
    // falling further behind every frame.
    [[nodiscard]] std::uint32_t advance(std::chrono::nanoseconds frameTime) noexcept;

    [[nodiscard]] std::chrono::nanoseconds step() const noexcept;

    // Time accumulated towards the next step, as a fraction of a step in [0, 1).
    [[nodiscard]] double alpha() const noexcept;

private:
    std::chrono::nanoseconds m_step;
    std::chrono::nanoseconds m_maxFrameTime;
    std::chrono::nanoseconds m_accumulator{0};
};

} // namespace devex::runtime
