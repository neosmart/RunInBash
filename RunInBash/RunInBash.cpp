#include "stdafx.h"
#include <cassert>
#include <memory>

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
	_tprintf_s(_T("RunInBash by NeoSmart Technologies - Copyright 2017\n"));
	_tprintf_s(_T("Easily run command(s) under bash, capturing the exit code.\n"));
	_tprintf_s(_T("Usage: Alias $ to RunInBash.exe and prefix WSL commands with $ to execute. For example:\n"));
	_tprintf_s(_T("$ uname -a\n"));
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

	TCHAR *escaped = (TCHAR *) calloc(newLength, sizeof(TCHAR));
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

int _tmain(int argc, TCHAR *argv[])
{
	//for multi-arch (x86/x64) support with one (x86) binary
	PVOID oldValue;
	Wow64DisableWow64FsRedirection(&oldValue);

	//Search for bash.exe
	TCHAR bash[MAX_PATH] = { 0 };
	ExpandEnvironmentStrings(_T("%windir%\\system32\\bash.exe"), bash, _countof(bash));
	bool found = GetFileAttributes(bash) != INVALID_FILE_ATTRIBUTES;

	if (!found)
	{
		_ftprintf(stderr, _T("Unable to find bash.exe!\n"));
		return -1;
	}

#ifdef _DEBUG
	int debugLevel = 1;
#else
	int debugLevel = 0;
#endif

	//Get whatever the user entered after our EXE as a single string
	auto argument = GetArgumentString(argv);
	if (_tcsclen(argument) == 0)
	{
		PrintHelp();
		ExitProcess(-1);
	}

	//handle possible arguments
	if (_tcsicmp(argv[1], _T("-h")) == 0)
	{
		PrintHelp();
		ExitProcess(0);
	}
	if (_tcsicmp(argv[1], _T("-v")) == 0)
	{
		debugLevel = 1;
		argument = TrimStart(argument + 2);
	}
	if (_tcsicmp(argv[1], _T("-d")) == 0)
	{
		debugLevel = 2;
		argument = TrimStart(argument + 2);
	}

	//Escape it to be placed within double quotes
	//in a scope to prevent inadvertent use of freed variables
	TCHAR *lpArg;
	{
		auto escaped = Escape(argument);
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
