#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/LogFile.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <type_traits>

namespace devex::platform {

// What a crash leaves behind: a report on the error stream and in the log — what went wrong, in
// which module, and the calls that led there, named when the symbols (.pdb) sit beside the
// binaries — and a minidump beside the log, which a debugger opens where the process stopped. On
// Linux, addr2line (binutils) names the calls from the debug information, or from the symbols of
// the binaries without it, and no minidump is written: the system keeps a core dump if it is set to.
struct DEVEX_API CrashReporting
{
    // Where the minidumps go; nothing is written there until a crash. Windows only.
    std::filesystem::path directory;
    // Receives the report as it is, without the logger, which the crash may have left busy.
    core::LogFile* log = nullptr;
};

// Catches, for the whole process, what would end it without a word: access violations and the
// other hardware exceptions, abort, std::terminate, pure virtual calls and invalid parameters of
// the C runtime. A later call replaces the settings. On Linux, the signals of faults (SIGSEGV,
// SIGBUS, SIGILL, SIGFPE, SIGTRAP), SIGABRT and std::terminate; the process then dies of its
// signal, as it would have. Elsewhere the process dies as it did.
DEVEX_API void installCrashHandler(CrashReporting reporting);

// Runs a function on this thread, as the editor runs the code of a game it plays: a crash inside it,
// which would end the process, ends the function instead, and so does an exception that nobody
// caught. Their report comes back, as the crash handler would have written it; nothing does when the
// function returned. What the function left half done stays so: the destructors of its frames do
// not run, and a lock it held stays held. Crashes on other threads still end the process. On Linux,
// the crash handler must be installed: the guard catches the signals it receives.
[[nodiscard]] DEVEX_API std::optional<std::string> runGuarded(void (*function)(void*), void* data);

template <typename Function>
[[nodiscard]] std::optional<std::string> runGuarded(Function&& function)
{
    using Callable = std::remove_reference_t<Function>;
    return runGuarded([](void* data) { (*static_cast<Callable*>(data))(); }, static_cast<void*>(&function));
}

// While one lives on a thread, crashes there end the process even inside runGuarded: jumping back
// over the frames of a runtime that unwinds its own, as .NET, would leave it broken.
class DEVEX_API CrashGuardBarrier
{
public:
    CrashGuardBarrier() noexcept;
    ~CrashGuardBarrier();

    CrashGuardBarrier(const CrashGuardBarrier&) = delete;
    CrashGuardBarrier& operator=(const CrashGuardBarrier&) = delete;
};

} // namespace devex::platform
