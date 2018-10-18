#pragma once

#include <sstream>
#include <stdarg.h>
#include <string>

// typedef keeps alias in intellisense, #define replaces it
#ifdef _UNICODE
// typedef std::wstring tstring;
// typedef std::wstringstream tstream;
#define tstring std::wstring
#define tstream std::wstringstream
#else
// typedef std::string tstring;
// typedef std::stringstream tstream;
#define tstring std::string
#define tstream std::stringstream
#endif

inline tstring sprintf(const tstring format, ...) {
    va_list valist;
    va_start(valist, format);

    va_list clone1;
    va_copy(clone1, valist);

    // use first clone to find out needed buffer size (including null)
    int length = _vsntprintf(nullptr, 0, format.c_str(), clone1) + 1;
    va_end(clone1);

    // reserve the needed space
    tstring result;
    result.resize(length, _T('\0'));

    // now actually format it
    _vstprintf_s((TCHAR *)result.data(), length, format.c_str(), valist);
    va_end(valist);

    // and drop the trailing null included literally in the data buffer
    result.resize(length - 1);

    return result;
}