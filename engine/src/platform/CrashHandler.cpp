#include <devex/platform/CrashHandler.hpp>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// After windows.h, which it needs.
#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <exception>
#include <format>
#include <malloc.h>
#include <string>
#include <string_view>
#elif defined(__linux__)
#include <dlfcn.h>
#include <elf.h>
#include <execinfo.h>
#include <fcntl.h>
#include <link.h>
#include <poll.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/auxv.h>
#include <sys/wait.h>
#include <time.h>
#include <ucontext.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <format>
#include <string>
#include <string_view>
#else
#include <exception>
#include <format>
#endif

namespace devex::platform {

#ifdef _WIN32
namespace {

// Everything the handlers use is set aside beforehand: once the process is crashing, the heap may
// be what broke, and the stack may be what ran out.
struct CrashState
{
    std::array<wchar_t, 1024> directory{};
    core::LogFile* log = nullptr;
    std::atomic<bool> handling{false};
    std::array<char, 32768> report{};
    std::size_t length = 0;
    CONTEXT walked{};
    std::array<wchar_t, 1100> dumpPath{};
    std::array<char, 2048> dumpPathUtf8{};
    bool symbols = false;
    bool installed = false;
};

// Inside runGuarded on this thread, and under CrashGuardBarrier.
thread_local int guardDepth = 0;
thread_local int guardBarriers = 0;
// Raised by the handlers of the C runtime inside a guard, once they described what went wrong, for
// the guard to catch.
constexpr DWORD guardedFatalError = 0xE0445658;

CrashState& crash()
{
    static CrashState state;
    return state;
}

void append(const char* format, ...)
{
    CrashState& state = crash();
    if (state.length + 1 >= state.report.size())
    {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(state.report.data() + state.length,
                                       state.report.size() - state.length, format, arguments);
    va_end(arguments);
    if (written > 0)
    {
        state.length = std::min(state.length + static_cast<std::size_t>(written), state.report.size() - 1);
    }
}

[[nodiscard]] const char* exceptionName(DWORD code) noexcept
{
    switch (code)
    {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_PRIV_INSTRUCTION:
        return "privileged instruction";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer division by zero";
    case EXCEPTION_INT_OVERFLOW:
        return "integer overflow";
    case EXCEPTION_IN_PAGE_ERROR:
        return "page that could not be read";
    case EXCEPTION_DATATYPE_MISALIGNMENT:
        return "misaligned data";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "array bounds exceeded";
    case EXCEPTION_FLT_DIVIDE_BY_ZERO:
    case EXCEPTION_FLT_INVALID_OPERATION:
    case EXCEPTION_FLT_OVERFLOW:
    case EXCEPTION_FLT_UNDERFLOW:
    case EXCEPTION_FLT_INEXACT_RESULT:
    case EXCEPTION_FLT_DENORMAL_OPERAND:
    case EXCEPTION_FLT_STACK_CHECK:
        return "floating point exception";
    case EXCEPTION_BREAKPOINT:
        // What a failed assertion ends with when no debugger is attached.
        return "breakpoint, as a failed assertion stops";
    case 0xE06D7363:
        return "C++ exception that nobody caught";
    case 0xC0000409:
        return "stack buffer overrun or fast fail";
    case 0xC0000374:
        return "heap corruption";
    default:
        return "exception";
    }
}

[[nodiscard]] const char* afterLastSlash(const char* path) noexcept
{
    const char* name = path;
    for (const char* character = path; *character != '\0'; ++character)
    {
        if (*character == '\\' || *character == '/')
        {
            name = character + 1;
        }
    }
    return name;
}

// "devex-engine.dll+0x1a2b3c devex::ui::UiWorld::update (UiWorld.cpp:123)": the module always, the
// function and the line when the symbols are there.
void describeAddress(HANDLE process, DWORD64 address)
{
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(address), &module) &&
        module != nullptr)
    {
        std::array<wchar_t, MAX_PATH> wide{};
        std::array<char, MAX_PATH * 3> name{};
        const DWORD length = GetModuleFileNameW(module, wide.data(), static_cast<DWORD>(wide.size()));
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(length), name.data(),
                            static_cast<int>(name.size() - 1), nullptr, nullptr);
        append("%s+0x%llx", afterLastSlash(name.data()),
               static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    }
    else
    {
        append("0x%016llx", static_cast<unsigned long long>(address));
    }

    alignas(SYMBOL_INFO) std::array<char, sizeof(SYMBOL_INFO) + 512> buffer{};
    auto* const symbol = reinterpret_cast<SYMBOL_INFO*>(buffer.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;
    DWORD64 displacement = 0;
    if (SymFromAddr(process, address, &displacement, symbol))
    {
        append(" %s", symbol->Name);
    }
    IMAGEHLP_LINE64 line{};
    line.SizeOfStruct = sizeof(line);
    DWORD lineDisplacement = 0;
    if (SymGetLineFromAddr64(process, address, &lineDisplacement, &line) && line.FileName != nullptr)
    {
        append(" (%s:%lu)", afterLastSlash(line.FileName), line.LineNumber);
    }
}

void walkStack(HANDLE process, const CONTEXT& context)
{
    CrashState& state = crash();
    state.walked = context;
    STACKFRAME64 frame{};
    DWORD machine = 0;
#if defined(_M_X64)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset = context.Rip;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrStack.Offset = context.Rsp;
#elif defined(_M_ARM64)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset = context.Pc;
    frame.AddrFrame.Offset = context.Fp;
    frame.AddrStack.Offset = context.Sp;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    for (int depth = 0; depth < 64; ++depth)
    {
        if (!StackWalk64(machine, process, GetCurrentThread(), &frame, &state.walked, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0)
        {
            break;
        }
        append("  #%-2d ", depth);
        describeAddress(process, frame.AddrPC.Offset);
        append("\n");
    }
}

// crash-2026-09-22-184012.dmp beside the log.
void writeMinidump(HANDLE process, EXCEPTION_POINTERS* exception)
{
    CrashState& state = crash();
    if (state.directory[0] == L'\0')
    {
        return;
    }
    SYSTEMTIME now{};
    GetLocalTime(&now);
    std::swprintf(state.dumpPath.data(), state.dumpPath.size(), L"%ls\\crash-%04u-%02u-%02u-%02u%02u%02u.dmp",
                  state.directory.data(), now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                  now.wSecond);
    const HANDLE file = CreateFileW(state.dumpPath.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        append("No minidump: the folder cannot be written\n");
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION information{};
    information.ThreadId = GetCurrentThreadId();
    information.ExceptionPointers = exception;
    information.ClientPointers = FALSE;
    // The threads, their stacks and the memory they point to: enough to read the variables of every
    // frame, in a few megabytes rather than the whole process.
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                 MiniDumpScanMemory);
    const BOOL written = MiniDumpWriteDump(process, GetCurrentProcessId(), file, type,
                                           exception != nullptr ? &information : nullptr, nullptr, nullptr);
    CloseHandle(file);
    WideCharToMultiByte(CP_UTF8, 0, state.dumpPath.data(), -1, state.dumpPathUtf8.data(),
                        static_cast<int>(state.dumpPathUtf8.size() - 1), nullptr, nullptr);
    if (written)
    {
        append("Minidump: %s\n", state.dumpPathUtf8.data());
    }
    else
    {
        append("No minidump: MiniDumpWriteDump failed (%lu)\n", GetLastError());
    }
}

[[nodiscard]] bool guarding() noexcept
{
    return guardDepth > 0 && guardBarriers == 0;
}

// What went wrong, where, and the calls that led there, into the report: the header of a crash
// that ends the process, or of one a guard caught.
void describe(bool fatal, const char* what, const CONTEXT& context, EXCEPTION_POINTERS* exception)
{
    CrashState& state = crash();
    const HANDLE process = GetCurrentProcess();
    if (!state.symbols)
    {
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS |
                      SYMOPT_NO_PROMPTS);
        state.symbols = SymInitialize(process, nullptr, TRUE) != FALSE;
    }

    state.length = 0;
    if (fatal)
    {
        append("\n*** The process crashed: %s\n", what);
    }
    else
    {
        append("%s\n", what);
    }
    if (exception != nullptr && exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        exception->ExceptionRecord->NumberParameters >= 2)
    {
        const ULONG_PTR kind = exception->ExceptionRecord->ExceptionInformation[0];
        append("While %s address 0x%016llx\n", kind == 0 ? "reading" : kind == 8 ? "executing" : "writing",
               static_cast<unsigned long long>(exception->ExceptionRecord->ExceptionInformation[1]));
    }
    append("Calls, the last one first:\n");
    walkStack(process, context);
}

void report(const char* what, const CONTEXT& context, EXCEPTION_POINTERS* exception)
{
    CrashState& state = crash();
    // A crash inside the report itself ends the process quietly rather than looping.
    if (state.handling.exchange(true))
    {
        return;
    }
    describe(true, what, context, exception);
    writeMinidump(GetCurrentProcess(), exception);

    const std::string_view text(state.report.data(), state.length);
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    if (state.log != nullptr)
    {
        state.log->writeRaw(text);
    }
}

// Reports what the C runtime or the C++ library calls a fatal error, then ends the process with
// the code abort uses; inside a guard, raises the exception that takes the guard back.
[[noreturn]] void reportHere(const char* what)
{
    CONTEXT context{};
    RtlCaptureContext(&context);
    if (guarding() && !crash().handling.exchange(true))
    {
        describe(false, what, context, nullptr);
        crash().handling = false;
        RaiseException(guardedFatalError, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    report(what, context, nullptr);
    TerminateProcess(GetCurrentProcess(), 3);
    std::_Exit(3);
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* exception)
{
    std::array<char, 160> what{};
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    std::snprintf(what.data(), what.size(), "%s (0x%08lX)", exceptionName(code), code);
    report(what.data(), *exception->ContextRecord, exception);
    // Ends the process with the code of the exception, as it would have without the handler.
    return EXCEPTION_EXECUTE_HANDLER;
}

void __cdecl onAbort(int)
{
    reportHere("abort");
}

void onTerminate()
{
    reportHere("std::terminate, after an exception that nobody caught");
}

void __cdecl onPureCall()
{
    reportHere("call of a pure virtual function");
}

void __cdecl onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t)
{
    reportHere("invalid parameter given to the C runtime");
}

// Catches the crashes of a guard with their report; C++ exceptions go on to its catch.
int guardFilter(EXCEPTION_POINTERS* exception) noexcept
{
    const DWORD code = exception->ExceptionRecord->ExceptionCode;
    if (code == 0xE06D7363 || guardBarriers > 0)
    {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // The handlers of the C runtime described theirs before raising it.
    if (code != guardedFatalError && !crash().handling.exchange(true))
    {
        std::array<char, 160> what{};
        std::snprintf(what.data(), what.size(), "%s (0x%08lX)", exceptionName(code), code);
        describe(false, what.data(), *exception->ContextRecord, exception);
        crash().handling = false;
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// The code of the exception that ended the function, 0 when it returned. No C++ object with a
// destructor may live in a function with __try.
DWORD runCaught(void (*function)(void*), void* data)
{
    __try
    {
        function(data);
    }
    __except (guardFilter(GetExceptionInformation()))
    {
        return GetExceptionCode();
    }
    return 0;
}

} // namespace

void installCrashHandler(CrashReporting reporting)
{
    CrashState& state = crash();
    state.log = reporting.log;
    state.installed = true;
    state.directory.fill(L'\0');
    if (!reporting.directory.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(reporting.directory, error);
        const std::wstring directory = reporting.directory.lexically_normal().wstring();
        wcsncpy_s(state.directory.data(), state.directory.size(), directory.c_str(), _TRUNCATE);
    }
    // Room for the handler when the thread dies of its stack: it runs on what is left of it.
    ULONG guarantee = 64 * 1024;
    SetThreadStackGuarantee(&guarantee);

    SetUnhandledExceptionFilter(&onUnhandledException);
    // The report says what abort did: the runtime's own message would come first, and in Debug it
    // is a dialog that holds the process until someone answers it.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::signal(SIGABRT, &onAbort);
    std::set_terminate(&onTerminate);
    _set_purecall_handler(&onPureCall);
    _set_invalid_parameter_handler(&onInvalidParameter);
}

std::optional<std::string> runGuarded(void (*function)(void*), void* data)
{
    ++guardDepth;
    DWORD code = 0;
    try
    {
        code = runCaught(function, data);
    }
    catch (const std::exception& error)
    {
        --guardDepth;
        return std::format("an exception that nobody caught: {}", error.what());
    }
    catch (...)
    {
        --guardDepth;
        return std::string("an exception that nobody caught");
    }
    --guardDepth;
    if (code == 0)
    {
        return std::nullopt;
    }
    // The page that guards the end of the stack is put back for the next overflow, and the handler of
    // abort, which the C runtime takes away before calling it.
    if (code == EXCEPTION_STACK_OVERFLOW)
    {
        _resetstkoflw();
    }
    const CrashState& state = crash();
    if (code == guardedFatalError && state.installed)
    {
        std::signal(SIGABRT, &onAbort);
    }
    return std::string(state.report.data(), state.length);
}

CrashGuardBarrier::CrashGuardBarrier() noexcept
{
    ++guardBarriers;
}

CrashGuardBarrier::~CrashGuardBarrier()
{
    --guardBarriers;
}
#elif defined(__linux__)
namespace {

constexpr std::size_t maxFrames = 64;

// A call on the stack.
struct Frame
{
    std::uintptr_t address = 0;
    // The library or the program it is in, and where in it.
    const char* module = nullptr;
    std::uintptr_t moduleOffset = 0;
    // The address addr2line reads in the module: from its start, or as it is in a program that is not
    // position independent. The line of a return address is the one of the call before it.
    std::uintptr_t lookup = 0;
    // The closest symbol the module exports, for when addr2line names nothing.
    const char* symbol = nullptr;
    std::uintptr_t symbolOffset = 0;
    // "function at file:line", as addr2line answered.
    std::string_view named;
};

// Everything the handlers use is set aside beforehand: once the process is crashing, the heap may
// be what broke, and the stack may be what ran out.
struct CrashState
{
    core::LogFile* log = nullptr;
    std::atomic<bool> handling{false};
    std::array<char, 32768> report{};
    std::size_t length = 0;
    std::array<void*, maxFrames + 16> addresses{};
    std::array<Frame, maxFrames> frames{};
    std::size_t frameCount = 0;
    // The program, which dladdr names as it was started rather than where it is.
    const void* programBase = nullptr;
    std::array<char, 4096> program{};
    std::array<char, 4096> addr2line{};
    std::array<std::array<char, 24>, maxFrames> lookups{};
    std::array<char, 65536> names{};
    std::size_t namesLength = 0;
    // The stack the handlers run on when the thread died of its own.
    alignas(16) std::array<std::byte, 256 * 1024> signalStack{};
    // How the signals are handled, put back after a guard caught one: SA_RESETHAND took it away.
    struct sigaction action{};
};

CrashState& crash()
{
    static CrashState state;
    return state;
}

// Where a crash under runGuarded jumps back to, the guard around it next.
struct Guard
{
    sigjmp_buf jump;
    Guard* previous = nullptr;
};

// Read by the signal handler: in the static block of thread storage, which reading never allocates.
[[gnu::tls_model("initial-exec")]] thread_local Guard* activeGuard = nullptr;
[[gnu::tls_model("initial-exec")]] thread_local int guardBarriers = 0;

[[gnu::format(printf, 1, 2)]] void append(const char* format, ...)
{
    CrashState& state = crash();
    if (state.length + 1 >= state.report.size())
    {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vsnprintf(state.report.data() + state.length,
                                       state.report.size() - state.length, format, arguments);
    va_end(arguments);
    if (written > 0)
    {
        state.length = std::min(state.length + static_cast<std::size_t>(written), state.report.size() - 1);
    }
}

[[nodiscard]] std::string_view afterLastSlash(std::string_view path) noexcept
{
    const std::size_t slash = path.rfind('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

[[nodiscard]] const char* signalName(int number, const siginfo_t* info) noexcept
{
    switch (number)
    {
    case SIGSEGV:
        return "access violation (SIGSEGV)";
    case SIGBUS:
        return "memory that could not be read (SIGBUS)";
    case SIGILL:
        // GCC ends a failed assertion with __builtin_trap, an illegal instruction.
        return "illegal instruction, as a failed assertion stops (SIGILL)";
    case SIGTRAP:
        return "breakpoint, as a failed assertion stops (SIGTRAP)";
    case SIGFPE:
        return info != nullptr && info->si_code == FPE_INTDIV ? "integer division by zero (SIGFPE)"
                                                              : "floating point exception (SIGFPE)";
    case SIGABRT:
        return "abort (SIGABRT)";
    default:
        return "signal";
    }
}

[[nodiscard]] std::uintptr_t programCounter(const ucontext_t& context) noexcept
{
#if defined(__x86_64__)
    return static_cast<std::uintptr_t>(context.uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
    return static_cast<std::uintptr_t>(context.uc_mcontext.pc);
#else
    return 0;
#endif
}

[[nodiscard]] const char* accessKind([[maybe_unused]] const siginfo_t& info,
                                     [[maybe_unused]] const ucontext_t* context) noexcept
{
#if defined(__x86_64__)
    if (info.si_signo == SIGSEGV && context != nullptr)
    {
        if (reinterpret_cast<std::uintptr_t>(info.si_addr) == programCounter(*context))
        {
            return "executing";
        }
        // The error code of the page fault: its second bit is set by writes.
        return (context->uc_mcontext.gregs[REG_ERR] & 2) != 0 ? "writing" : "reading";
    }
#endif
    return "accessing";
}

// Finds the module of each call from `first` on; the first one is where a fault stopped rather than a
// return address when `fault` says so.
void locateFrames(std::size_t first, std::size_t count, bool fault)
{
    CrashState& state = crash();
    state.frameCount = 0;
    for (std::size_t index = first; index < count && state.frameCount < maxFrames; ++index)
    {
        const bool returnAddress = !(fault && state.frameCount == 0);
        Frame& frame = state.frames[state.frameCount++];
        frame = Frame{.address = reinterpret_cast<std::uintptr_t>(state.addresses[index])};
        Dl_info info{};
        if (dladdr(state.addresses[index], &info) == 0 || info.dli_fbase == nullptr || info.dli_fname == nullptr)
        {
            continue;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(info.dli_fbase);
        const std::uintptr_t looked = returnAddress ? frame.address - 1 : frame.address;
        const auto* const header = static_cast<const ElfW(Ehdr)*>(info.dli_fbase);
        frame.module = info.dli_fbase == state.programBase && state.program[0] != '\0' ? state.program.data()
                                                                                      : info.dli_fname;
        frame.moduleOffset = frame.address - base;
        frame.lookup = header->e_type == ET_EXEC ? looked : looked - base;
        frame.symbol = info.dli_sname;
        frame.symbolOffset =
            info.dli_saddr != nullptr ? frame.address - reinterpret_cast<std::uintptr_t>(info.dli_saddr) : 0;
    }
}

[[nodiscard]] long long millisecondsNow() noexcept
{
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000 + now.tv_nsec / 1'000'000;
}

// Names the calls of one module with addr2line, in a process of its own: from the debug information
// of the module, or at least from all its symbols, where dladdr only sees those it exports.
void nameFrames(const char* module)
{
    CrashState& state = crash();
    std::array<char*, maxFrames + 8> arguments{};
    std::size_t argumentCount = 0;
    arguments[argumentCount++] = state.addr2line.data();
    for (const char* const option : {"--demangle", "--functions", "--pretty-print", "--exe"})
    {
        arguments[argumentCount++] = const_cast<char*>(option);
    }
    arguments[argumentCount++] = const_cast<char*>(module);
    std::array<std::size_t, maxFrames> asked{};
    std::size_t askedCount = 0;
    for (std::size_t index = 0; index < state.frameCount; ++index)
    {
        if (state.frames[index].module != module)
        {
            continue;
        }
        std::snprintf(state.lookups[index].data(), state.lookups[index].size(), "0x%llx",
                      static_cast<unsigned long long>(state.frames[index].lookup));
        arguments[argumentCount++] = state.lookups[index].data();
        asked[askedCount++] = index;
    }

    std::array<int, 2> ends{};
    if (pipe2(ends.data(), O_CLOEXEC) != 0)
    {
        return;
    }
    const pid_t child = fork();
    if (child == 0)
    {
        dup2(ends[1], STDOUT_FILENO);
        if (const int nothing = open("/dev/null", O_WRONLY); nothing >= 0)
        {
            dup2(nothing, STDERR_FILENO);
        }
        // In English whatever the language of the user: " at " stands between the function and the line.
        std::array<char*, 2> environment{const_cast<char*>("LC_ALL=C"), nullptr};
        execve(arguments[0], arguments.data(), environment.data());
        _exit(127);
    }
    close(ends[1]);
    if (child < 0)
    {
        close(ends[0]);
        return;
    }

    // The debug information of a library takes addr2line a second or so to read; a stuck one is let go.
    // A local length, which no call in the loop can change behind the bounds checked.
    const std::size_t start = state.namesLength;
    std::size_t length = start;
    const long long deadline = millisecondsNow() + 30'000;
    bool finished = false;
    while (!finished && length < state.names.size() && millisecondsNow() < deadline)
    {
        pollfd readable{.fd = ends[0], .events = POLLIN, .revents = 0};
        const int ready = poll(&readable, 1, 100);
        if (ready < 0 && errno != EINTR)
        {
            break;
        }
        if (ready <= 0)
        {
            continue;
        }
        const ssize_t received = read(ends[0], state.names.data() + length, state.names.size() - length);
        if (received > 0)
        {
            length += static_cast<std::size_t>(received);
        }
        else
        {
            finished = true;
        }
    }
    state.namesLength = length;
    close(ends[0]);
    if (!finished)
    {
        kill(child, SIGKILL);
    }
    int status = 0;
    waitpid(child, &status, 0);

    // One line for each address, in the order they were given.
    std::string_view answer(state.names.data() + start, state.namesLength - start);
    for (std::size_t index = 0; index < askedCount; ++index)
    {
        const std::size_t end = answer.find('\n');
        if (end == std::string_view::npos)
        {
            break;
        }
        state.frames[asked[index]].named = answer.substr(0, end);
        answer.remove_prefix(end + 1);
    }
}

// "libdevex-engine.so+0x1a2b3c devex::ui::UiWorld::update(float) (UiWorld.cpp:123)": the module
// always, the function and the line when addr2line finds them.
void writeFrames()
{
    CrashState& state = crash();
    for (std::size_t index = 0; index < state.frameCount; ++index)
    {
        const Frame& frame = state.frames[index];
        append("  #%-2zu ", index);
        if (frame.module != nullptr)
        {
            const std::string_view module = afterLastSlash(frame.module);
            append("%.*s+0x%llx", static_cast<int>(module.size()), module.data(),
                   static_cast<unsigned long long>(frame.moduleOffset));
        }
        else
        {
            append("0x%016llx", static_cast<unsigned long long>(frame.address));
        }

        // "function at file:line", "function at ??:?" without debug information, "?? ??:0" when
        // nothing is known.
        std::string_view function = frame.named;
        std::string_view location;
        if (const std::size_t at = frame.named.find(" at "); at != std::string_view::npos)
        {
            function = frame.named.substr(0, at);
            location = frame.named.substr(at + 4);
        }
        if (!function.empty() && !function.starts_with("??"))
        {
            append(" %.*s", static_cast<int>(function.size()), function.data());
        }
        else if (frame.symbol != nullptr)
        {
            append(" %s+0x%llx", frame.symbol, static_cast<unsigned long long>(frame.symbolOffset));
        }
        if (const std::size_t discriminator = location.find(" (discriminator"); discriminator != std::string_view::npos)
        {
            location = location.substr(0, discriminator);
        }
        if (!location.empty() && !location.starts_with("??"))
        {
            location = afterLastSlash(location);
            append(" (%.*s)", static_cast<int>(location.size()), location.data());
        }
        append("\n");
    }
}

// What went wrong, where, and the calls that led there, into the report: the header of a crash
// that ends the process, or of one a guard caught.
void describe(bool fatal, const char* what, const siginfo_t* info, const ucontext_t* context)
{
    CrashState& state = crash();
    state.length = 0;
    state.namesLength = 0;
    if (fatal)
    {
        append("\n*** The process crashed: %s\n", what);
    }
    else
    {
        append("%s\n", what);
    }
    if (info != nullptr && (info->si_signo == SIGSEGV || info->si_signo == SIGBUS) && info->si_code > 0)
    {
        append("While %s address 0x%016llx\n", accessKind(*info, context),
               static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info->si_addr)));
    }
    append("Calls, the last one first:\n");

    // The report and the frame of the signal come first: the calls start where the fault stopped, or
    // after the report when nothing faulted.
    const int captured = backtrace(state.addresses.data(), static_cast<int>(state.addresses.size()));
    const std::size_t count = captured > 0 ? static_cast<std::size_t>(captured) : 0;
    std::size_t first = std::min<std::size_t>(1, count);
    bool fault = false;
    if (const std::uintptr_t stopped = context != nullptr ? programCounter(*context) : 0; stopped != 0)
    {
        for (std::size_t index = 0; index < count; ++index)
        {
            if (reinterpret_cast<std::uintptr_t>(state.addresses[index]) == stopped)
            {
                first = index;
                fault = true;
                break;
            }
        }
    }
    locateFrames(first, count, fault);
    if (state.addr2line[0] != '\0')
    {
        for (std::size_t index = 0; index < state.frameCount; ++index)
        {
            const char* const module = state.frames[index].module;
            const bool named = std::any_of(state.frames.begin(), state.frames.begin() + static_cast<std::ptrdiff_t>(index),
                                           [module](const Frame& frame) { return frame.module == module; });
            if (module != nullptr && *module != '\0' && !named)
            {
                nameFrames(module);
            }
        }
    }
    writeFrames();
}

void report(const char* what, const siginfo_t* info, const ucontext_t* context)
{
    CrashState& state = crash();
    // A crash inside the report itself ends the process quietly rather than looping.
    if (state.handling.exchange(true))
    {
        return;
    }
    describe(true, what, info, context);

    const std::string_view text(state.report.data(), state.length);
    for (std::size_t written = 0; written < text.size();)
    {
        const ssize_t wrote = write(STDERR_FILENO, text.data() + written, text.size() - written);
        if (wrote <= 0)
        {
            break;
        }
        written += static_cast<std::size_t>(wrote);
    }
    if (state.log != nullptr)
    {
        state.log->writeRaw(text);
    }
}

// The guard a crash on this thread goes back to, taken from the guards: none under a barrier, or
// while a report is being written.
[[nodiscard]] Guard* takeGuard() noexcept
{
    Guard* const guard = activeGuard;
    if (guard == nullptr || guardBarriers > 0 || crash().handling.exchange(true))
    {
        return nullptr;
    }
    activeGuard = guard->previous;
    return guard;
}

void onSignal(int number, siginfo_t* info, void* context)
{
    const auto* const interrupted = static_cast<const ucontext_t*>(context);
    if (Guard* const guard = takeGuard())
    {
        describe(false, signalName(number, info), info, interrupted);
        sigaction(number, &crash().action, nullptr);
        crash().handling = false;
        // glibc lets a handler of SIGABRT leave abort this way too, when the jump was set by sigsetjmp.
        siglongjmp(guard->jump, 1);
    }
    report(signalName(number, info), info, interrupted);
    // SA_RESETHAND gave the signal back to the system, which ends the process with it once the
    // handler returns, as it would have without the handler.
    raise(number);
}

void onTerminate()
{
    constexpr const char* what = "std::terminate, after an exception that nobody caught";
    if (Guard* const guard = takeGuard())
    {
        describe(false, what, nullptr, nullptr);
        crash().handling = false;
        siglongjmp(guard->jump, 1);
    }
    report(what, nullptr, nullptr);
    // The handler of abort finds the report written, and lets the process end.
    std::abort();
}

// An exception that leaves the function of a guard, as the report of a crash.
[[nodiscard]] std::optional<std::string> callCatching(void (*function)(void*), void* data)
{
    try
    {
        function(data);
    }
    catch (const std::exception& error)
    {
        return std::format("an exception that nobody caught: {}", error.what());
    }
    catch (...)
    {
        return std::string("an exception that nobody caught");
    }
    return std::nullopt;
}

// addr2line, from binutils, looked for while the process is well.
void findAddr2line(CrashState& state)
{
    state.addr2line[0] = '\0';
    const char* const variable = std::getenv("PATH");
    std::string_view folders = variable != nullptr ? variable : "/usr/bin:/bin";
    while (true)
    {
        const std::size_t colon = folders.find(':');
        if (const std::string_view folder = folders.substr(0, colon); !folder.empty())
        {
            const std::string candidate = std::string(folder) + "/addr2line";
            if (candidate.size() < state.addr2line.size() && access(candidate.c_str(), X_OK) == 0)
            {
                std::memcpy(state.addr2line.data(), candidate.c_str(), candidate.size() + 1);
                return;
            }
        }
        if (colon == std::string_view::npos)
        {
            return;
        }
        folders.remove_prefix(colon + 1);
    }
}

} // namespace

void installCrashHandler(CrashReporting reporting)
{
    CrashState& state = crash();
    state.log = reporting.log;
    const ssize_t length = readlink("/proc/self/exe", state.program.data(), state.program.size() - 1);
    state.program[length > 0 ? static_cast<std::size_t>(length) : 0] = '\0';
    Dl_info program{};
    if (dladdr(reinterpret_cast<void*>(getauxval(AT_ENTRY)), &program) != 0)
    {
        state.programBase = program.dli_fbase;
    }
    findAddr2line(state);
    // The first backtrace loads the unwinder of libgcc, which allocates: not during a crash.
    std::array<void*, 4> warm{};
    static_cast<void>(backtrace(warm.data(), static_cast<int>(warm.size())));

    // Room for the handlers when the thread dies of its stack.
    stack_t stack{};
    stack.ss_sp = state.signalStack.data();
    stack.ss_size = state.signalStack.size();
    sigaltstack(&stack, nullptr);

    struct sigaction& action = state.action;
    action = {};
    action.sa_sigaction = &onSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    for (const int number : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP})
    {
        sigaction(number, &action, nullptr);
    }
    std::set_terminate(&onTerminate);
}

std::optional<std::string> runGuarded(void (*function)(void*), void* data)
{
    Guard guard{.previous = activeGuard};
    // A crash comes back here, its guard already taken away, with the signals blocked by the handler
    // unblocked again.
    if (sigsetjmp(guard.jump, 1) != 0)
    {
        const CrashState& state = crash();
        return std::string(state.report.data(), state.length);
    }
    activeGuard = &guard;
    std::optional<std::string> thrown = callCatching(function, data);
    activeGuard = guard.previous;
    return thrown;
}

CrashGuardBarrier::CrashGuardBarrier() noexcept
{
    ++guardBarriers;
}

CrashGuardBarrier::~CrashGuardBarrier()
{
    --guardBarriers;
}
#else
void installCrashHandler(CrashReporting /*reporting*/)
{
}

std::optional<std::string> runGuarded(void (*function)(void*), void* data)
{
    try
    {
        function(data);
    }
    catch (const std::exception& error)
    {
        return std::format("an exception that nobody caught: {}", error.what());
    }
    catch (...)
    {
        return std::string("an exception that nobody caught");
    }
    return std::nullopt;
}

CrashGuardBarrier::CrashGuardBarrier() noexcept = default;
CrashGuardBarrier::~CrashGuardBarrier() = default;
#endif

} // namespace devex::platform
