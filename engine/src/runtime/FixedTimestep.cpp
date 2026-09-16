#include <devex/core/Assert.hpp>
#include <devex/runtime/FixedTimestep.hpp>

#include <algorithm>

namespace devex::runtime {

FixedTimestep::FixedTimestep(std::chrono::nanoseconds step,
                             std::chrono::nanoseconds maxFrameTime) noexcept
    : m_step(step)
    , m_maxFrameTime(maxFrameTime)
{
    DEVEX_ASSERT_MSG(step.count() > 0, "a fixed step must be positive");
}

FixedTimestep FixedTimestep::fromRate(std::uint32_t hertz) noexcept
{
    DEVEX_ASSERT_MSG(hertz > 0, "a fixed update rate must be positive");
    return FixedTimestep(std::chrono::nanoseconds(std::chrono::seconds(1)) / std::max(hertz, 1u));
}

std::uint32_t FixedTimestep::advance(std::chrono::nanoseconds frameTime) noexcept
{
    m_accumulator += std::clamp(frameTime, std::chrono::nanoseconds::zero(), m_maxFrameTime);
    const auto steps = m_accumulator / m_step;
    m_accumulator %= m_step;
    return static_cast<std::uint32_t>(steps);
}

std::chrono::nanoseconds FixedTimestep::step() const noexcept
{
    return m_step;
}

double FixedTimestep::alpha() const noexcept
{
    return static_cast<double>(m_accumulator.count()) / static_cast<double>(m_step.count());
}

} // namespace devex::runtime
