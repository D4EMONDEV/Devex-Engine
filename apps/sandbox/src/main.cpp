#include <devex/core/Assert.hpp>
#include <devex/core/BuildInfo.hpp>
#include <devex/core/Error.hpp>
#include <devex/core/Log.hpp>

int main()
{
    using namespace devex::core;

    DEVEX_LOG_INFO("Devex Engine {}", version());
    DEVEX_LOG_DEBUG("Assertions {}", assertsEnabled ? "enabled" : "disabled");

    const Result<void> window = makeError(ErrorCode::Unsupported, "no window backend before milestone {}", 1);
    if (!window)
    {
        DEVEX_LOG_WARNING("Cannot open a window: {}", window.error());
    }

    return 0;
}
