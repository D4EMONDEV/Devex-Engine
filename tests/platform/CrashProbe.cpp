#include <devex/core/Log.hpp>
#include <devex/core/LogFile.hpp>
#include <devex/platform/CrashHandler.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string_view>

// Crashes on purpose once its handler is installed, so that the platform tests read what a crash
// leaves behind: devex-crash-probe <folder> <access|abort>.
namespace {

#ifdef _MSC_VER
__declspec(noinline)
#endif
void crashHere(std::string_view how)
{
    if (how == "abort")
    {
        std::abort();
    }
    volatile int* nowhere = nullptr;
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
    DEVEX_LOG_INFO("The probe is about to crash");
    crashHere(argv[2]);
    return 0;
}
