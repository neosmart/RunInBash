#include "stdafx.h"
#include <cassert>
#include <memory>
#include <process.h>
#include <sstream>
#include <string>
#include <vector>
#include "ArgHelper.h"
#include "format.h"

#ifdef _DEBUG
    int debugLevel = 1;
#else
    int debugLevel = 0;
#endif

TCHAR bash[MAX_PATH] = {};

template <typename T> //for both const and non-const
T *TrimStart(T *str)
{
    assert(str != nullptr);
    while (str[0] == _T(' '))
    {
        ++str;
    }

    return str;
}

//There is no CommandLineToArgvA(), so we rely on the caller to pass in argv[]
template <typename T>
const TCHAR *GetArgumentString(const T argv)
{
    const TCHAR *cmdLine = GetCommandLine();

    bool escaped = cmdLine[0] == '\'' || cmdLine[0] == '\"';
    const TCHAR *skipped = cmdLine + _tcsclen(argv[0]) + (escaped ? 1 : 0) * 2;

    return TrimStart(skipped);
}

void PrintHelp()
{
    _tprintf_s(_T("RunInBash by NeoSmart Technologies - Copyright 2017-2018\n"));
    _tprintf_s(_T("Easily run command(s) under bash, capturing the exit code.\n"));
    _tprintf_s(_T("Usage: Alias $ to RunInBash.exe and prefix WSL commands with $ to execute. For example:\n"));
    _tprintf_s(_T("$ uname -a\n"));
}

std::vector<std::wstring> inline StringSplit(const std::wstring &source, const TCHAR *delimiter = _T(" "), bool keepEmpty = false)
{
    std::vector<std::wstring> results;

    size_t prev = 0;
    size_t next = 0;

    while ((next = source.find_first_of(delimiter, prev)) != std::wstring::npos)
    {
        if (keepEmpty || (next - prev != 0))
        {
            results.push_back(source.substr(prev, next - prev));
        }
        prev = next + 1;
    }

    if (prev < source.size())
    {
        results.push_back(source.substr(prev));
    }

    return results;
}

bool starts_with(const TCHAR *haystack, const TCHAR *needle, bool caseSensitive = false)
{
    if (!caseSensitive)
    {
        return _tcsncicmp(haystack, needle, _tcsclen(needle)) == 0;
    }
    return _tcsnccmp(haystack, needle, _tcsclen(needle)) == 0;
}

//Translate an absolute path like C:\some\path or a relative path like .\subfolder\
//to a WSL-compatible path (/mnt/c/some/path and ./subfolder, respectively)
TCHAR *TranslatePath(const TCHAR *winPath, bool &fileFound)
{
    //In addition to supporting both relative and absolute paths, we also need to keep
    //in mind that WSL paths are case-sensitive, and that unix does not include the CWD
    //in the search path, meaning that while fILe is a valid path under Windows, it must
    //be transformed to ./file to work under WSL.

    //It's actually not easy to get the case-sensitive filename given a path under native WIN32.
    //The best solution is to try to open the file, obtaining a handle. An API new to Vista+ is
    //`GetFinalPathNameByHandle`, which can then tell us the full and complete path to the file.
    //This will get the correct case, convert relative paths to absolute paths, etc.

    //NB: We can't open a handle to a directory without FILE_FLAG_BACKUP_SEMANTICS, which requires SE_BACKUP_NAME and SE_RESTORE_NAME privileges
    auto hFile = CreateFile(winPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        fileFound = false;
        return nullptr;
    }
    fileFound = true;

    //Find out needed buffer size. There is a bug in the MSDN documentation for `GetFilePathNameByHandle`:
    // > cchFilePath [in]: The size of lpszFilePath, in TCHARs. This value does not include a NULL termination character.
    // > [..] If the function fails because lpszFilePath is too small to hold the string plus the terminating null character,
    // > the return value is the required buffer size, in TCHARs. This value includes the size of the terminating null character."
    //But this behavior as documented is wrong, the returned value in case of not enough memory does not include the null character.
    size_t length = GetFinalPathNameByHandle(hFile, nullptr, 0, FILE_NAME_NORMALIZED);
    if (length == 0)
    {
        CloseHandle(hFile);
        _ftprintf(stderr, _T("GetFilePathNameByHandle error: 0x%x\n"), GetLastError());
        return nullptr;
    }

    auto finalPath = (TCHAR *)alloca((length + 1) * sizeof(TCHAR));
    length = GetFinalPathNameByHandle(hFile, finalPath, length, FILE_NAME_NORMALIZED);
    CloseHandle(hFile);
    if (length == 0)
    {
        _ftprintf(stderr, _T("GetFilePathNameByHandle error: 0x%x\n"), GetLastError());
        return nullptr;
    }

    //Use a different reference so we can retain the original value
    auto buffer = finalPath;

    //Replace all `\` path separators with `/` instead
    //All paths returned by `GetFilePathNameByHandle` are prefixed with \\?\ to bypass MAX_PATH
    if (starts_with(buffer, _T(R"(\\?\)")))
    {
        buffer += _tcsclen(_T(R"(\\?\)"));
    }

    for (size_t i = 0; i < length; ++i)
    {
        if (buffer[i] == _T('\\'))
        {
            buffer[i] = _T('/');
        }
    }

    //Replace the drive letter (e.g. C:) with "/mnt/c"
    //Network paths and \\?\ paths are not supported.
    length = _tcsclen(buffer);
    if (buffer[0] >= _T('A') && buffer[0] <= _T('z')
        && buffer[1] == _T(':')) //this is safe since we assert that length is at least 1 + \0 above
    {
        auto newCount = length + _tcsclen(_T("/mnt/c")) + 1;
        auto result = (TCHAR *)calloc(newCount, sizeof(TCHAR));

        //buffer[2] is guaranteed valid since buffer[1] is ':'
        //drives under /mnt/ are all lower-cased
        _stprintf_s(result, newCount, _T("/mnt/%c%s"), iswupper(buffer[0]) ? towlower(buffer[0]) : buffer[0], buffer + 2);
        return result;
    }

    //Else it's an unsupported path
    if (debugLevel > 0)
    {
        _ftprintf(stderr, _T("Argument \"%s\" evaluated to path \"%s\" unsupported under WSL!\n"), winPath, finalPath);
    }
    else
    {
        _ftprintf(stderr, _T("Error: Argument \"%s\" evaluated to a path unsupported under WSL!\n"), winPath);
    }
    return nullptr;
}

//Escapes for single quoting
const TCHAR *EscapeSingle(const TCHAR *string, bool &mustFree) {
    assert(string != nullptr);

    mustFree = false;
    int newLength = 1; //terminating null
    for (size_t i = 0; string[i] != _T('\0'); ++i) {
        ++newLength;
        if (string[i] == _T('\'')) {
            mustFree = true;
            ++newLength;
        }
    }

    if (!mustFree) {
        return string;
    }

    auto escaped = (TCHAR *)calloc(newLength, sizeof(TCHAR));
    for (size_t i = 0, j = 0; string[i] != _T('\0'); ++i, ++j)
    {
        if (string[i] == _T('\''))
        {
            escaped[j++] = _T('\\');
        }
        escaped[j] = string[i];
    }

    return escaped;
}

TCHAR *Escape(const TCHAR *string)
{
    assert(string != nullptr);
    int newLength = 1; //terminating null
    for (size_t i = 0; string[i] != _T('\0'); ++i)
    {
        ++newLength;
        //these characters will be escaped, so add room for one slash
        if (string[i] == _T('"') || string[i] == _T('\\'))
        {
            ++newLength;
        }
    }

    auto escaped = (TCHAR *) calloc(newLength, sizeof(TCHAR));
    for (size_t i = 0, j = 0; string[i] != _T('\0'); ++i, ++j)
    {
        if (string[i] == _T('"') || string[i] == _T('\\'))
        {
            escaped[j++] = _T('\\');
        }
        escaped[j] = string[i];
    }

    return escaped;
}

enum PipeId {
    PIPE_READ,
    PIPE_WRITE
};

void AppendStrToWstring(std::wstring &wstr, const char *str, int bytes) {
    int requiredChars = MultiByteToWideChar(CP_UTF8, 0, str, bytes, nullptr, 0);
    size_t oldSize = wstr.size();
    wstr.resize(wstr.size() + requiredChars);
    MultiByteToWideChar(CP_UTF8, 0, str, bytes, const_cast<wchar_t*>(wstr.c_str() + oldSize), requiredChars);
}

std::vector<std::wstring> Parse(const TCHAR *cmdLine)
{
    //There's absolutely no good way to properly mimic what bash does. CMD treats single quotes
    //as escapes, but MSVCRT/GetArgvFromCommandLineW does not. Consider cases like "pa'th with strings'"
    //The solution is to just give up and let bash do it.

    TCHAR currentDir[MAX_PATH];
    GetCurrentDirectory(_countof(currentDir), currentDir);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = true;

    HANDLE bash_stdin[2]{};
    HANDLE bash_stdout[2]{};
    HANDLE bash_stderr[2]{};

    for (auto &pipe : { bash_stdin, bash_stdout, bash_stderr })
    {
        if (CreatePipe(&pipe[PIPE_READ], &pipe[PIPE_WRITE], &sa, 0) == 0)
        {
            _ftprintf(stderr, _T("CreatePipe: 0x%x\n"), GetLastError());
            ExitProcess(-1);
        }
    }

    if (SetHandleInformation(bash_stdin[PIPE_WRITE], HANDLE_FLAG_INHERIT, 0) == 0)
    {
        _ftprintf(stderr, _T("SetHandleInformation(bash_stdin[0]): 0x%x"), GetLastError());
        ExitProcess(-1);
    }
    if (SetHandleInformation(bash_stdout[PIPE_READ], HANDLE_FLAG_INHERIT, 0) == 0)
    {
        _ftprintf(stderr, _T("SetHandleInformation(bash_stdout[0]): 0x%x"), GetLastError());
        ExitProcess(-1);
    }
    if (SetHandleInformation(bash_stderr[PIPE_READ], HANDLE_FLAG_INHERIT, 0) == 0)
    {
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

    auto args = Escape(cmdLine);
    auto script = sprintf(_T("for arg in $(echo \"%s\"); do echo $arg; done"), args);
    _tprintf(_T("script: %s\n"), script.c_str());

    auto cmd = sprintf(_T("bash -c '%s'"), script.c_str());
    CreateProcess(bash, (LPWSTR) cmd.c_str(), nullptr, nullptr, true, 0, nullptr, currentDir, &startInfo, &pInfo);

    //Close handles we won't use
    CloseHandle(pInfo.hProcess);
    CloseHandle(pInfo.hThread);

    //Close the write handle to stdin
    CloseHandle(bash_stdin[PIPE_WRITE]);

    //Close our view of the child process's handles
    CloseHandle(bash_stdin[PIPE_READ]);
    CloseHandle(bash_stdout[PIPE_WRITE]);
    CloseHandle(bash_stderr[PIPE_WRITE]);

    std::wstring stdout_str;
    std::wstring stderr_str;

    while (true)
    {
        DWORD bytesRead;
        DWORD totalRead = 0;
        char buffer[512];

        if (ReadFile(bash_stdout[PIPE_READ], buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead != 0)
        {
            totalRead += bytesRead;
            AppendStrToWstring(stdout_str, buffer, bytesRead);
        }

        if (ReadFile(bash_stderr[PIPE_READ], buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead != 0)
        {
            totalRead += bytesRead;
            AppendStrToWstring(stderr_str, buffer, bytesRead);
        }

        if (totalRead == 0)
        {
            break;
        }
    }

    if (!stderr_str.empty())
    {
        _ftprintf(stderr, stderr_str.c_str());
        ExitProcess(-1);
    }

    // Close read ends
    CloseHandle(bash_stdout[PIPE_READ]);
    CloseHandle(bash_stderr[PIPE_READ]);

    auto arguments = StringSplit(stdout_str.c_str(), _T("\n"), true);

    return std::move(arguments);
}

bool contains_any_of(const TCHAR *haystack, const TCHAR *charArray)
{
    size_t searchLength = _tcsclen(charArray);
    for (size_t i = 0; i < _tcsclen(haystack); ++i)
    {
        for (size_t j = 0; j < searchLength; ++j)
        {
            if (haystack[i] == charArray[j])
            {
                return true;
            }
        }
    }

    return false;
}

int _tmain(int argc, TCHAR *argv[])
{
    //for multi-arch (x86/x64) support with one (x86) binary
    PVOID oldValue;
    Wow64DisableWow64FsRedirection(&oldValue);

    //Search for bash.exe
    ExpandEnvironmentStrings(_T("%windir%\\system32\\bash.exe"), bash, _countof(bash));
    bool found = GetFileAttributes(bash) != INVALID_FILE_ATTRIBUTES;

    if (!found)
    {
        _ftprintf(stderr, _T("Unable to find bash.exe!\n"));
        return -1;
    }

    if (argc == 1)
    {
        PrintHelp();
        ExitProcess(-1);
    }

    //handle possible arguments
    if (is_any_of(argv[1], _T("--help"), _T("-h"), _T("/h"), _T("/help"), _T("/?")))
    {
        PrintHelp();
        ExitProcess(0);
    }
    if (is_any_of(argv[1], _T("--verbose"), _T("-v"), _T("/verbose"), _T("/v")))
    {
        debugLevel = 1;
    }
    if (is_any_of(argv[1], _T("--debug"), _T("-d"), _T("/debug"), _T("/d")))
    {
        debugLevel = 2;
    }

    auto args = Parse(GetArgumentString(argv));

    //Translate paths in the passed string
    //Will only catch paths that exist on the drive and are not part of a string
    std::wstringstream translated;
    for (auto arg : args)
    {
        bool fileFound = false;
        auto translatedPath = TranslatePath(arg.c_str(), fileFound);
        if (translatedPath == nullptr && fileFound)
        {
            //no point in continuing
            ExitProcess(-1);
        }

        if (translatedPath == nullptr)
        {
            if (debugLevel > 1)
            {
                _ftprintf(stderr, _T("raw argument: %s\n"), arg.c_str());
            }
            //Regular arguments go in double quotes
            translated << _T("\"") << Escape(arg.c_str()) << _T("\" ");
            continue;
        }

        //Place file in single quotes, because those are taken literally by bash
        if (debugLevel > 1)
        {
            _ftprintf(stderr, _T("mapped argument “%s” to file “%s”\n"), arg.c_str(), translatedPath);
        }
        bool mustFreeSingle;
        auto quotedSingle = EscapeSingle(translatedPath, mustFreeSingle);
        translated << _T("'") << quotedSingle << _T("' ");
        if (mustFreeSingle) {
            free(const_cast<wchar_t *>(quotedSingle));
        }
        free(translatedPath);
    }
    auto argument = translated.str();

    //Escape argument string to be placed within double quotes.
    //In a scope to prevent inadvertent use of freed variables
    TCHAR *lpArg;
    {
        auto escaped = Escape(argument.c_str());
        int temp = sizeof(escaped);
        TCHAR *format = _T("bash -c \"%s\"");
        size_t length = _tcsclen(format) + _tcslen(escaped) + 1;

        lpArg = (TCHAR *)alloca(sizeof(TCHAR) * length);
        ZeroMemory(lpArg, sizeof(TCHAR) * length);
        _stprintf(lpArg, format, escaped);
        free(escaped);
    }

    TCHAR currentDir[MAX_PATH] = { 0 };
    GetCurrentDirectory(_countof(currentDir), currentDir);

    if (debugLevel >= 1)
    {
        _ftprintf(stderr, _T("> %s\n"), lpArg);
    }

    auto startInfo = STARTUPINFO { 0 };
    auto pInfo = PROCESS_INFORMATION { 0 };
    bool success = CreateProcess(bash, lpArg, nullptr, nullptr, true, 0, nullptr, currentDir, &startInfo, &pInfo);

    if (!success)
    {
        _ftprintf(stderr, _T("Failed to create process. Last error: 0x%x\n"), GetLastError());
        return GetLastError();
    }

    WaitForSingleObject(pInfo.hProcess, INFINITE);
    DWORD bashExitCode = -1;
    while (GetExitCodeProcess(pInfo.hProcess, &bashExitCode) == TRUE)
    {
        if (bashExitCode == STILL_ACTIVE)
        {
            //somehow process hasn't terminated according to the kernel
            continue;
        }
        break;
    }

    CloseHandle(pInfo.hProcess);

    return bashExitCode;
}
