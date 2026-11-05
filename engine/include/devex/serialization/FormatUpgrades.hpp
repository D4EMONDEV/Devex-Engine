#pragma once

#include <devex/core/Error.hpp>
#include <devex/serialization/Text.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <utility>

namespace devex::serialization {

// What brings a text file of an older format to the current one, one version at a time: each step goes from a
// version to the next, and the steps of a format only ever grow, as files of every version stay readable.
class FormatUpgrades
{
public:
    using Step = std::function<void(TextDocument& document)>;

    // The step from a version to the next.
    FormatUpgrades& from(std::int64_t version, Step step)
    {
        m_steps.insert_or_assign(version, std::move(step));
        return *this;
    }

    // Fails when a step is missing, which says the format changed without saying how.
    [[nodiscard]] core::Result<void> apply(TextDocument& document, std::int64_t version, std::int64_t current) const
    {
        for (; version < current; ++version)
        {
            const auto step = m_steps.find(version);
            if (step == m_steps.end())
            {
                return core::makeError(core::ErrorCode::Unsupported, "nothing knows how to read format {} of this file", version);
            }
            step->second(document);
        }
        return {};
    }

private:
    std::map<std::int64_t, Step> m_steps;
};

} // namespace devex::serialization
