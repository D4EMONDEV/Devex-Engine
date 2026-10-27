#include <devex/core/Log.hpp>
#include <devex/core/LogFile.hpp>
#include <devex/platform/CrashHandler.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

// Crashes on purpose once its handler is installed, so that the platform tests read what a crash
// leaves behind: devex-crash-probe <folder> <access|abort>. With guarded-access, guarded-abort or
// guarded-exception, it crashes inside runGuarded and lives on; with barrier, it crashes inside
// runGuarded under a CrashGuardBarrier.
namespace {

#ifdef _MSC_VER
__declspec(noinline)
#else
[[gnu::noinline]]
#endif
void crashHere(std::string_view how)
{
    if (how == "abort")
    {
        std::abort();
    }
    if (how == "exception")
    {
        throw std::runtime_error("thrown on purpose");
    }
    // A null the optimizer cannot see, which GCC would otherwise turn into a trap.
    volatile int* volatile nowhere = nullptr;
    *nowhere = 42;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        return 2;
    }
    const std::filesystem::path folder = argv[1];
    devex::core::Result<std::unique_ptr<devex::core::LogFile>> log =
        devex::core::LogFile::open(folder / "probe.log");
    devex::platform::installCrashHandler({.directory = folder, .log = log ? log->get() : nullptr});
    const std::string_view how = argv[2];
    // Twice: the handlers are armed again after a crash a guard caught.
    if (how.starts_with("guarded-"))
    {
        for (int round = 0; round < 2; ++round)
        {
            const std::optional<std::string> caught = devex::platform::runGuarded([how] { crashHere(how.substr(8)); });
            if (!caught)
            {
                DEVEX_LOG_ERROR("The crash was not caught");
                return 1;
            }
            DEVEX_LOG_INFO("Caught: {}", *caught);
        }
        DEVEX_LOG_INFO("The probe lives on");
        return 0;
    }
    if (how == "barrier")
    {
        DEVEX_LOG_INFO("The probe is about to crash");
        static_cast<void>(devex::platform::runGuarded([] {
            const devex::platform::CrashGuardBarrier barrier;
            crashHere("access");
        }));
        return 0;
    }
    DEVEX_LOG_INFO("The probe is about to crash");
    crashHere(how);
    return 0;
}
