// Runs the compiler and rewrites its /showIncludes notes with the English prefix, "Note: including
// file:", forwarding everything else unchanged.
//
// Ninja reads header dependencies from these notes, recognized by the prefix CMake detects. Without
// the English language pack, MSVC localizes them (French: "Remarque : inclusion du fichier :",
// with no-break spaces) in the code page of the console, which other tools of the build switch to
// UTF-8: the bytes then vary between compilations, and CMake cannot even write a prefix that is not
// valid UTF-8 into its Ninja rules. An ASCII prefix avoids both problems.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view englishPrefix = "Note: including file:";

// The command line without the program name of this launcher.
[[nodiscard]] std::wstring commandToRun()
{
    const wchar_t* line = GetCommandLineW();
    if (*line == L'"')
    {
        ++line;
        while (*line != L'\0' && *line != L'"')
        {
            ++line;
        }
        if (*line == L'"')
        {
            ++line;
        }
    }
    else
    {
        while (*line != L'\0' && *line != L' ' && *line != L'\t')
        {
            ++line;
        }
    }
    while (*line == L' ' || *line == L'\t')
    {
        ++line;
    }
    return line;
}

// Length of a /showIncludes prefix at the start of a line, colon included, or 0: words ending with
// a colon, followed by spaces and an absolute path. Diagnostics start with a path instead.
[[nodiscard]] std::size_t showIncludesPrefixLength(std::string_view line)
{
    for (std::size_t colon = line.find(':'); colon != std::string_view::npos; colon = line.find(':', colon + 1))
    {
        const std::string_view prefix = line.substr(0, colon);
        if (prefix.size() < 2 || prefix.find_first_of("\\/(") != std::string_view::npos)
        {
            return 0;
        }
        std::size_t path = colon + 1;
        if (path >= line.size() || line[path] != ' ')
        {
            continue;
        }
        while (path < line.size() && line[path] == ' ')
        {
            ++path;
        }
        const bool letter = path < line.size() &&
                            ((line[path] >= 'A' && line[path] <= 'Z') || (line[path] >= 'a' && line[path] <= 'z'));
        if (letter && path + 2 < line.size() && line[path + 1] == ':' &&
            (line[path + 2] == '\\' || line[path + 2] == '/'))
        {
            return colon + 1;
        }
    }
    return 0;
}

void writeAll(HANDLE output, std::string_view bytes)
{
    while (!bytes.empty())
    {
        DWORD written = 0;
        if (!WriteFile(output, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) || written == 0)
        {
            return;
        }
        bytes.remove_prefix(written);
    }
}

void forwardLine(HANDLE output, std::string_view line)
{
    if (const std::size_t prefix = showIncludesPrefixLength(line); prefix > 0)
    {
        writeAll(output, englishPrefix);
        line.remove_prefix(prefix);
    }
    writeAll(output, line);
}

} // namespace

int wmain()
{
    std::wstring command = commandToRun();
    if (command.empty())
    {
        std::fputws(L"usage: devex_show_includes_launcher <compiler> [arguments...]\n", stderr);
        return 2;
    }

    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    HANDLE readEnd = nullptr;
    HANDLE writeEnd = nullptr;
    if (!CreatePipe(&readEnd, &writeEnd, &inheritable, 0) || !SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0))
    {
        std::fwprintf(stderr, L"cannot create a pipe (error %lu)\n", GetLastError());
        return 1;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = writeEnd;
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, &command[0], nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &process))
    {
        std::fwprintf(stderr, L"cannot run %ls (error %lu)\n", command.c_str(), GetLastError());
        return 1;
    }
    // The pipe reports its end once the compiler, the only writer left, exits.
    CloseHandle(writeEnd);

    const HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
    std::string pending;
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(readEnd, buffer, sizeof(buffer), &read, nullptr) && read > 0)
    {
        pending.append(buffer, read);
        std::size_t start = 0;
        for (std::size_t newline = pending.find('\n'); newline != std::string::npos;
             newline = pending.find('\n', start))
        {
            forwardLine(output, std::string_view(pending).substr(start, newline + 1 - start));
            start = newline + 1;
        }
        pending.erase(0, start);
    }
    forwardLine(output, pending);
    CloseHandle(readEnd);

    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return static_cast<int>(exitCode);
}
