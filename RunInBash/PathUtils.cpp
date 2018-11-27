#include "stdafx.h"
#include <Shlwapi.h>
#include "Utils.h"
#include "format.h"

// Translate an absolute path like C:\some\path or a relative path like .\subfolder\
// to a WSL-compatible path (/mnt/c/some/path and ./subfolder, respectively)
std::wstring TranslatePath(const std::wstring &winPath, bool &fileFound) {
    // In addition to supporting both relative and absolute paths, we also need to
    // keep in mind that WSL paths are case-sensitive, and that unix does not
    // include the CWD in the search path: while fILe is a valid path under
    // Windows, it must be transformed to ./file to work under WSL.

    // It's actually not easy to get the case-sensitive filename given a path
    // under native WIN32. The best solution is to try to open the file, obtaining
    // a handle. An API new to Vista+ is `GetFinalPathNameByHandle`, which can then
    // tell us the full and complete path to the file. This will get the correct
    // case, convert relative paths to absolute paths, etc.

    // NB: We can't open a handle to a directory without
    // FILE_FLAG_BACKUP_SEMANTICS, which requires SE_BACKUP_NAME and
    // SE_RESTORE_NAME privileges
    auto hFile = CreateFile(
        winPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        fileFound = false;
        return L"";
    }
    fileFound = true;
    auto destAttr = GetFileAttributes(winPath.c_str());

    // `GetFilePathNameByHandle` is f'd up, if you provide 0 or less than how much is
    // required to store the string, it returns the number of bytes needed to store it
    // INCLUDING the trailing null. But when the call succeeds it returns the number of
    // bytes needed to store the string WITHOUT the trailing null.
    size_t length = GetFinalPathNameByHandle(hFile, nullptr, 0, FILE_NAME_NORMALIZED);
    if (length == 0) {
        CloseHandle(hFile);
        _ftprintf(stderr, _T("GetFilePathNameByHandle error: 0x%x\n"), GetLastError());
        return L"";
    }

    std::wstring finalPath;
    finalPath.resize(length);

    length = GetFinalPathNameByHandle(hFile, const_cast<wchar_t *>(finalPath.data()), length,
                                      FILE_NAME_NORMALIZED);
    CloseHandle(hFile);
    if (length == 0) {
        _ftprintf(stderr, _T("GetFilePathNameByHandle error: 0x%x\n"), GetLastError());
        return L"";
    }

    // Resize to the actual content length without the trailing null.
    finalPath.resize(length);

    bool relativePath = false;
    // GetFinalPathNameByHandle changes a relative path to an absolute one, so we need to
    // undo that if the original path was relative.
    if (PathIsRelative(winPath.c_str())) {
        wchar_t *currentDir = nullptr;
        auto cwdLength = GetCurrentDirectory(0, currentDir) + 1;
        currentDir = (wchar_t *)alloca(cwdLength * sizeof(wchar_t));
        GetCurrentDirectory(cwdLength, currentDir);

        wchar_t path[MAX_PATH + 1];
        // PathRelativePathTo does not support the \\?\ prefix prepended by GetFilePathNameByHandle
        if (PathRelativePathTo(path, currentDir, FILE_ATTRIBUTE_DIRECTORY,
                               (finalPath.c_str() + _tcsclen(_T(R"(\\?\)"))), destAttr)) {
            finalPath = path;
            relativePath = true;
        }
    }

    // The behavior of some commands changes depending on whether an argument ends in a trailing
    // slash or not. Make sure we keep that the way it was.
    if (winPath.back() == _T('/') || winPath.back() == _T('\\')) {
        if (finalPath.back() != _T('\\')) {
            finalPath.push_back(_T('\\'));
        }
    } else {
        if (finalPath.back() == _T('\\')) {
            finalPath.resize(finalPath.length() - 1);
        }
    }

    auto ptr = const_cast<wchar_t *>(finalPath.data());
    // Replace all `\` path separators with `/` instead
    // All paths returned by `GetFilePathNameByHandle` are prefixed with \\?\ to
    // bypass MAX_PATH
    if (!relativePath && StartsWith(finalPath, _T(R"(\\?\)"))) {
        ptr += _tcsclen(_T(R"(\\?\)"));
    }

    for (size_t i = 0; i < finalPath.length(); ++i) {
        if (ptr[i] == _T('\\')) {
            ptr[i] = _T('/');
        }
    }

    // Replace the drive letter (e.g. C:) with "/mnt/c"
    // Network paths and \\?\ paths are not supported.
    if (relativePath) {
        return finalPath;
    } else if (ptr[0] >= _T('A') && ptr[0] <= _T('z') &&
               ptr[1] == _T(':')) // this is safe since we assert that length is at
                                  // least 1 + \0 above
    {
        // buffer[2] is guaranteed valid since buffer[1] is ':'
        // By convention, all drives under /mnt/ are all lower-cased
        auto result =
            sprintf(L"/mnt/%c%s",
                    iswupper(ptr[0]) ? towlower(ptr[0]) : ptr[0], // the lower-case drive letter
                    ptr + 2                                       // the rest of the path
            );

        return result;
    }

    // Else it's an unsupported path
    if (debugLevel > 0) {
        _ftprintf(stderr, _T("Argument \"%s\" evaluated to path \"%s\" unsupported under WSL!\n"),
                  winPath.c_str(), finalPath.c_str());
    } else {
        _ftprintf(stderr,
                  _T("Error: Argument \"%s\" evaluated to a path unsupported ")
                  _T("under WSL!\n"),
                  winPath.c_str());
    }

    return L"";
}
