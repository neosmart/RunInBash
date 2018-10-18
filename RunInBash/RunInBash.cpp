#include "stdafx.h"

#include <cassert>
#include <memory>
#include <process.h>
#include <sstream>
#include <string>
#include <vector>

#include "ArgHelper.h"
#include "BashParser.h"
#include "PathUtils.h"
#include "Utils.h"
#include "format.h"

#include <fcntl.h>
#include <io.h>

TCHAR bash[MAX_PATH] = {};

// There is no CommandLineToArgvA(), so we rely on the caller to pass in argv[]
template <typename T>
const TCHAR *GetArgumentString(const T argv) {
    const TCHAR *cmdLine = GetCommandLine();

    bool escaped = cmdLine[0] == '\'' || cmdLine[0] == '\"';
    const TCHAR *skipped = cmdLine + _tcsclen(argv[0]) + (escaped ? 1 : 0) * 2;

    return TrimStart(skipped);
}

void PrintHelp() {
    _tprintf_s(_T("RunInBash by NeoSmart Technologies - Copyright 2017-2018\n"));
    _tprintf_s(_T("Easily run command(s) under bash, capturing the exit code. "
                  "Paths are mapped and arguments are automatically quoted correctly.\n"));
    _tprintf_s(_T("Usage: Alias $ to RunInBash.exe and prefix WSL commands with ")
               _T("$ to execute. For example:\n"));
    _tprintf_s(_T("$ uname -a\n"));
}

// This only tokenizes by spaces and double quotes.
static std::vector<std::wstring> Tokenize(const TCHAR *cmdLine) {
    std::vector<std::wstring> args;

    bool inQuotes = false;
    bool escapeNext = false;
    std::wstringstream arg;
    for (auto ptr = cmdLine; ptr != nullptr && *ptr != L'\0'; ++ptr) {
        if (*ptr == L'\\') {
            escapeNext = true;
        }

        if (escapeNext) {
            escapeNext = false;
            arg << *ptr;
            continue;
        }

        if (*ptr == L'"') {
            inQuotes ^= true;
        }

        if (*ptr == L' ' && !inQuotes) {
            args.emplace_back(arg.str());
            std::swap(arg, std::move(std::wstringstream{}));

            // Advance past consecutive whitespace
            while (ptr[1] == L' ') {
                ++ptr;
            }
            continue;
        }

        arg << *ptr;
    }
    auto lastArg = arg.str();
    if (!lastArg.empty()) {
        args.emplace_back(std::move(lastArg));
    }

    if (escapeNext) {
        _ftprintf(stderr, _T("Expecting a character after literal \\\n"));
        ExitProcess(-1);
    }

    return args;
}

int _tmain(int argc, TCHAR *argv[]) {
    // for multi-arch (x86/x64) support with one (x86) binary
    PVOID oldValue;
    Wow64DisableWow64FsRedirection(&oldValue);

    // Allow unicode in our output, prevent it from being treated as CP_437
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);

    // Search for bash.exe
    ExpandEnvironmentStrings(_T("%windir%\\system32\\bash.exe"), bash, _countof(bash));
    bool found = GetFileAttributes(bash) != INVALID_FILE_ATTRIBUTES;

    if (!found) {
        _ftprintf(stderr, _T("Unable to find bash.exe!\n"));
        return -1;
    }

    if (argc == 1) {
        PrintHelp();
        ExitProcess(-1);
    }

    // handle possible arguments
    int variablesSkipped = 0;
    if (IsAnyOf(argv[1], _T("--help"), _T("-h"), _T("/h"), _T("/help"), _T("/?"))) {
        PrintHelp();
        ExitProcess(0);
    }
    if (IsAnyOf(argv[1], _T("--verbose"), _T("-v"), _T("/verbose"), _T("/v"))) {
        ++variablesSkipped;
        debugLevel = 1;
    }
    if (IsAnyOf(argv[1], _T("--debug"), _T("-d"), _T("/debug"), _T("/d"))) {
        ++variablesSkipped;
        debugLevel = 2;
    }

    // For the most part, we want people to be able to specify WIN32-style arguments
    // to $, as in `file C:\Windows\` should not need the `\` to be escaped or quoted.
    // The exception to this is a literal single quote, which is sometimes used as an
    // escape and sometimes not depending on the combination of shell and CRT. We want
    // `$ 'foo'` to be treated as a quoted variable, but `$ "'foo'"` and `$ \'foo\'`
    // must not be expanded twice into an undecorated `foo`.
    // The easiest thing to do here is use the WIN32-parsed version only to check for
    // and map file names/paths which we then escape and pass to bash, and pass all
    // other tokens as-is to bash with escaping.
    // auto tokenized = StringSplit(GetCommandLineW());
    auto tokenized = Tokenize(GetCommandLineW());

    // Translate paths in the passed string
    // Will only catch paths that exist on the drive and are not part of a string
    std::wstringstream args;
    for (size_t i = 1 + variablesSkipped; i < argc; ++i) {
        const auto &arg = argv[i];
        const auto &token = tokenized[i];

        if (debugLevel >= 2) {
            _ftprintf(stderr, _T("- literal argument: %s\n"), token.c_str());
            _ftprintf(stderr, _T("- WIN32 value: %s\n"), argv[i]);
        };

        bool fileFound = false;
        auto translatedPath = TranslatePath(arg, fileFound);

        // Add a space after the previous argument, if we're not the first argument
        if (i != 1 + variablesSkipped) {
            args << L' ';
        }

        if (!fileFound) {
            // It was a regular argument and not a file
            // Use bash's interpretation of this raw argument, not ours
            if (debugLevel > 1) {
                _ftprintf(stderr, L"\u2022 %s\n", token.c_str());
            }
            args << token;
        } else if (translatedPath.empty()) {
            // A file WAS found but we weren't able to translate the path.
            // We've already printed an error message, so just surrender now.
            ExitProcess(-1);
        } else {
            // Push an escaped version of the translated path as a single-quoted bash argument
            // Bash treats all contents of a single-quoted argument as literal, which is what we
            // want.
            if (debugLevel > 1) {
                _ftprintf(stderr, L"\u2022 %s -> %s\n", argv[i], translatedPath.c_str());
            }
            args << _T("'") << Escape(EscapeStyle::SingleQuoted, translatedPath) << _T("'");
        }
    }

    auto escaped = Escape(EscapeStyle::DoubleQuoted, args.str());

    // By using `exec` here, we prevent bash.exe from needlessly sticking around and we also
    // prevent a needless/incorrect double increment of SHLVL.
    auto lpArg = sprintf(L"bash -c \"exec %s\"", escaped.c_str());

    TCHAR currentDir[MAX_PATH]{};
    GetCurrentDirectory(_countof(currentDir), currentDir);

    if (debugLevel >= 1) {
        _ftprintf(stderr, _T("> %s\n"), lpArg.c_str());
    }

    auto startInfo = STARTUPINFO{0};
    auto pInfo = PROCESS_INFORMATION{0};
    bool success = CreateProcess(bash, const_cast<wchar_t *>(lpArg.c_str()), nullptr, nullptr, true,
                                 0, nullptr, currentDir, &startInfo, &pInfo);

    if (!success) {
        _ftprintf(stderr, _T("Failed to create process. Last error: 0x%x\n"), GetLastError());
        return GetLastError();
    }

    WaitForSingleObject(pInfo.hProcess, INFINITE);
    DWORD bashExitCode = -1;
    while (GetExitCodeProcess(pInfo.hProcess, &bashExitCode) == TRUE) {
        if (bashExitCode == STILL_ACTIVE) {
            // somehow process hasn't terminated according to the kernel
            continue;
        }
        break;
    }

    CloseHandle(pInfo.hProcess);

    return bashExitCode;
}
