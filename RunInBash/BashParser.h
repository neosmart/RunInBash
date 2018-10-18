#pragma once

#include "stdafx.h"

// Parse quoted/escaped arguments from the commandline into an array of actual, unquoted arguments
std::vector<std::wstring> Tokenize(const TCHAR *cmdLine);
std::vector<std::wstring> Parse(const TCHAR *cmdLine);
