#include "stdafx.h"
#include "Utils.h"
#include "format.h"

static std::vector<std::wstring> InnerParse(const TCHAR *cmdLine, bool tokenizeOnly);

std::vector<std::wstring> Parse(const TCHAR *cmdLine) {
    return InnerParse(cmdLine, false);
}

std::vector<std::wstring> Tokenize(const TCHAR *cmdLine) {
    return InnerParse(cmdLine, true);
}

enum PipeId { PIPE_READ, PIPE_WRITE };

static std::vector<std::wstring> InnerParse(const TCHAR *cmdLine, bool tokenizeOnly) {
    // There's absolutely no good way to properly mimic what bash does. CMD treats
    // single quotes as escapes, but MSVCRT/GetArgvFromCommandLineW does not.
    // Consider cases like "pa'th with strings'" The solution is to just give up
    // and let bash do it.

    TCHAR currentDir[MAX_PATH];
    GetCurrentDirectory(_countof(currentDir), currentDir);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = true;

    HANDLE bash_stdin[2]{};
    HANDLE bash_stdout[2]{};
    HANDLE bash_stderr[2]{};

    for (auto &pipe : {bash_stdin, bash_stdout, bash_stderr}) {
        if (CreatePipe(&pipe[PIPE_READ], &pipe[PIPE_WRITE], &sa, 0) == 0) {
            _ftprintf(stderr, _T("CreatePipe: 0x%x\n"), GetLastError());
            ExitProcess(-1);
        }
    }

    if (SetHandleInformation(bash_stdin[PIPE_WRITE], HANDLE_FLAG_INHERIT, 0) == 0) {
        _ftprintf(stderr, _T("SetHandleInformation(bash_stdin[0]): 0x%x"), GetLastError());
        ExitProcess(-1);
    }
    if (SetHandleInformation(bash_stdout[PIPE_READ], HANDLE_FLAG_INHERIT, 0) == 0) {
        _ftprintf(stderr, _T("SetHandleInformation(bash_stdout[0]): 0x%x"), GetLastError());
        ExitProcess(-1);
    }
    if (SetHandleInformation(bash_stderr[PIPE_READ], HANDLE_FLAG_INHERIT, 0) == 0) {
        _ftprintf(stderr, _T("SetHandleInformation(bash_stderr[0]): 0x%x"), GetLastError());
        ExitProcess(-1);
    }

    STARTUPINFO startInfo{};
    startInfo.cb = sizeof(startInfo);
    startInfo.hStdOutput = bash_stdout[PIPE_WRITE];
    startInfo.hStdError = bash_stderr[PIPE_WRITE];
    startInfo.hStdInput = bash_stdin[PIPE_READ];
    startInfo.dwFlags = STARTF_USESTDHANDLES;
    PROCESS_INFORMATION pInfo{};

    std::wstring script;
    if (tokenizeOnly) {
        // This makes bash emit the literal arguments that were passed in, without un-escaping
        // anything
        script = sprintf(_T("for arg in $(echo \"%s\"); do echo $arg; done"),
                         Escape(EscapeStyle::DoubleQuoted, cmdLine).c_str());
    } else {
        // This makes bash boil down each argument to its evaluated form, i.e. what the user
        // supposedly _wanted_ rather than was forced to type in.
        script = sprintf(_T("for arg in %s; do echo $arg; done"), cmdLine);
    }
    _tprintf(_T("script: %s\n"), script.c_str());

    script = Escape(EscapeStyle::SingleQuoted, script);
    auto cmd = sprintf(_T("bash -c '%s'"), script.c_str());
    CreateProcess(bash, (LPWSTR)cmd.c_str(), nullptr, nullptr, true, 0, nullptr, currentDir,
                  &startInfo, &pInfo);

    // Close handles we won't use
    CloseHandle(pInfo.hProcess);
    CloseHandle(pInfo.hThread);

    // Close the write handle to stdin
    CloseHandle(bash_stdin[PIPE_WRITE]);

    // Close our view of the child process's handles
    CloseHandle(bash_stdin[PIPE_READ]);
    CloseHandle(bash_stdout[PIPE_WRITE]);
    CloseHandle(bash_stderr[PIPE_WRITE]);

    std::wstring stdout_str;
    std::wstring stderr_str;

    while (true) {
        DWORD bytesRead;
        DWORD totalRead = 0;
        char buffer[512];

        if (ReadFile(bash_stdout[PIPE_READ], buffer, sizeof(buffer), &bytesRead, nullptr) &&
            bytesRead != 0) {
            totalRead += bytesRead;
            AppendStrToWstring(stdout_str, buffer, bytesRead);
        }

        if (ReadFile(bash_stderr[PIPE_READ], buffer, sizeof(buffer), &bytesRead, nullptr) &&
            bytesRead != 0) {
            totalRead += bytesRead;
            AppendStrToWstring(stderr_str, buffer, bytesRead);
        }

        if (totalRead == 0) {
            break;
        }
    }

    if (!stderr_str.empty()) {
        _ftprintf(stderr, stderr_str.c_str());
        ExitProcess(-1);
    }

    // Close read ends
    CloseHandle(bash_stdout[PIPE_READ]);
    CloseHandle(bash_stderr[PIPE_READ]);

    auto arguments = StringSplit(stdout_str.c_str(), _T("\n"), true);

    return std::move(arguments);
}
