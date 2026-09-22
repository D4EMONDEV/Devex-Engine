#pragma once

#include <devex/core/LogFile.hpp>

#include <filesystem>

namespace devex::platform {

// What a crash leaves behind: a report on the error stream and in the log — what went wrong, in
// which module, and the calls that led there, named when the symbols (.pdb) sit beside the
// binaries — and a minidump beside the log, which a debugger opens where the process stopped.
struct CrashReporting
{
    // Where the minidumps go; nothing is written there until a crash.
    std::filesystem::path directory;
    // Receives the report as it is, without the logger, which the crash may have left busy.
    core::LogFile* log = nullptr;
};

// Catches, for the whole process, what would end it without a word: access violations and the
// other hardware exceptions, abort, std::terminate, pure virtual calls and invalid parameters of
// the C runtime. A later call replaces the settings. Windows only for now: elsewhere the process
// dies as it did.
void installCrashHandler(CrashReporting reporting);

} // namespace devex::platform
