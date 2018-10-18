#pragma once

#include "stdafx.h" // to please intellisense

extern TCHAR bash[MAX_PATH];

// Escape a string for use within
enum class EscapeStyle {
    SingleQuoted,
    DoubleQuoted,
};

void AppendStrToWstring(std::wstring &wstr, const char *str, int bytes);
bool ContainsAnyOf(const std::wstring &haystack, const TCHAR *charArray);
std::wstring Escape(EscapeStyle style, const std::wstring &string);
bool StartsWith(const std::wstring &haystack, const std::wstring &needle,
                bool caseSensitive = false);
std::vector<std::wstring> StringSplit(const std::wstring &source, const TCHAR *delimiter = _T(" "),
                                      bool keepEmpty = false);

template <typename T> // for both const and non-const
T *TrimStart(T *str) {
    assert(str != nullptr);
    while (str[0] == _T(' ')) {
        ++str;
    }

    return str;
}

inline bool IsAnyOf(const TCHAR *value, const TCHAR *arg) {
    return _tcsicmp(value, arg) == 0;
}

template <typename... Args>
bool IsAnyOf(const TCHAR *value, const TCHAR *arg1, Args... args) {
    if (IsAnyOf(value, arg1)) {
        return true;
    }
    return IsAnyOf(value, args...);
}
