#pragma once

#include <devex/core/Export.hpp>

#include <devex/core/LogFile.hpp>

#include <filesystem>

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

} // namespace devex::platform
