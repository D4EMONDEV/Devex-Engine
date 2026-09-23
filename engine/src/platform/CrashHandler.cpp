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
#include <string>
#include <string_view>
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
};

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

void report(const char* what, const CONTEXT& context, EXCEPTION_POINTERS* exception)
{
    CrashState& state = crash();
    // A crash inside the report itself ends the process quietly rather than looping.
    if (state.handling.exchange(true))
    {
        return;
    }
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_NO_PROMPTS);
    SymInitialize(process, nullptr, TRUE);

    state.length = 0;
    append("\n*** The process crashed: %s\n", what);
    if (exception != nullptr && exception->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        exception->ExceptionRecord->NumberParameters >= 2)
    {
        const ULONG_PTR kind = exception->ExceptionRecord->ExceptionInformation[0];
        append("While %s address 0x%016llx\n", kind == 0 ? "reading" : kind == 8 ? "executing" : "writing",
               static_cast<unsigned long long>(exception->ExceptionRecord->ExceptionInformation[1]));
    }
    append("Calls, the last one first:\n");
    walkStack(process, context);
    writeMinidump(process, exception);

    const std::string_view text(state.report.data(), state.length);
    DWORD written = 0;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    if (state.log != nullptr)
    {
        state.log->writeRaw(text);
    }
}

// Reports what the C runtime or the C++ library calls a fatal error, then ends the process with
// the code abort uses.
[[noreturn]] void reportHere(const char* what)
{
    CONTEXT context{};
    RtlCaptureContext(&context);
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

} // namespace

void installCrashHandler(CrashReporting reporting)
{
    CrashState& state = crash();
    state.log = reporting.log;
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
#else
void installCrashHandler(CrashReporting /*reporting*/)
{
}
#endif

} // namespace devex::platform
