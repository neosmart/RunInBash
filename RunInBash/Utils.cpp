#include "stdafx.h"
#include "utils.h"

void AppendStrToWstring(std::wstring &wstr, const char *str, int bytes) {
    int requiredChars = MultiByteToWideChar(CP_UTF8, 0, str, bytes, nullptr, 0);
    size_t oldSize = wstr.size();
    wstr.resize(wstr.size() + requiredChars);
    MultiByteToWideChar(CP_UTF8, 0, str, bytes, const_cast<wchar_t *>(wstr.c_str() + oldSize),
                        requiredChars);
}

bool ContainsAnyOf(const std::wstring &haystack, const TCHAR *charArray) {
    return haystack.find_first_of(charArray) != std::wstring::npos;
}

static std::wstring EscapeSingle(const std::wstring &string) {
    // Bash does not have ANY quoting in single-quote mode, not even a literal \' to
    // escape the single quote itself. The only way to do it is to leave single quote
    // mode, print a literal quote via \' and then re-enter single-quote mode, so to
    // escape the phrase `hello 'world'` we would need to change that to
    // `hello '\''world'\''` so that when it is wrapped in single quotes by the caller,
    // it will form a legal string.

    std::wstring escaped;
    escaped.reserve(1.5 * string.size());

    for (auto c : string) {
        if (c != L'\'') {
            escaped.push_back(c);
        } else {
            escaped.append(L"'\\''");
        }
    }

    return escaped;
}

static std::wstring EscapeDouble(const std::wstring &string) {
    int newLength = 0;
    for (size_t i = 0; string[i] != _T('\0'); ++i) {
        ++newLength;
        // these characters will be escaped, so add room for one slash
        if (string[i] == _T('"') || string[i] == _T('\\')) {
            ++newLength;
        }
    }

    std::wstring escaped;
    escaped.resize(newLength);

    for (size_t i = 0, j = 0; string[i] != _T('\0'); ++i, ++j) {
        if (string[i] == _T('"') || string[i] == _T('\\')) {
            escaped[j++] = _T('\\');
        }
        escaped[j] = string[i];
    }

    return escaped;
}

std::wstring Escape(EscapeStyle style, const std::wstring &string) {
    if (style == EscapeStyle::DoubleQuoted) {
        return EscapeDouble(string);
    } else {
        return EscapeSingle(string);
    }
}

bool StartsWith(const std::wstring &haystack, const std::wstring &needle, bool caseSensitive) {
    if (!caseSensitive) {
        return _tcsncicmp(haystack.c_str(), needle.c_str(), needle.size()) == 0;
    }
    return _tcsnccmp(haystack.c_str(), needle.c_str(), needle.size()) == 0;
}

std::vector<std::wstring> StringSplit(const std::wstring &source, const TCHAR *delimiter,
                                      bool keepEmpty) {
    std::vector<std::wstring> results;

    size_t prev = 0;
    size_t next = 0;

    while ((next = source.find_first_of(delimiter, prev)) != std::wstring::npos) {
        if (keepEmpty || (next - prev != 0)) {
            results.push_back(source.substr(prev, next - prev));
        }
        prev = next + 1;
    }

    if (prev < source.size()) {
        results.push_back(source.substr(prev));
    }

    return std::move(results);
}
