#include "stdafx.h"
#include <shlwapi.h>
#include <memory>

template <typename T> //for both const and non-const
T *TrimStart(T *str)
{
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
	
	bool escape = cmdLine[0] == '\'' || cmdLine[0] == '\"';
	const TCHAR *skipped = cmdLine + _tcsclen(argv[0]) + (escape ? 1 : 0) * 2;

	return TrimStart(skipped);
}

void PrintHelp()
{
	_tprintf_s(_T("RunInBash by NeoSmart Technologies - Copyright 2017\n"));
	_tprintf_s(_T("Usage: Alias $ to RunInBash.exe and prefix WSL commands with $ to execute. For example:\n"));
	_tprintf_s(_T("$ uname -a\n"));
}

TCHAR *MakeBashPath(const TCHAR *absolutePath)
{
	if (!absolutePath || _tcsclen(absolutePath) < 3 || absolutePath[1] != _T(':') || absolutePath[2] != _T('\\'))
	{
		//invalid or non-absolute path
		return nullptr;
	}

	//add /mnt/c/ to the beginning and replace all forward slashes with backslashes
	auto bashPath = (TCHAR *)calloc(_tcsclen(absolutePath) + _tcsclen(_T("/mnt/x/") + 1), sizeof(TCHAR));
	auto letter = _totlower(absolutePath[0]);
	_stprintf(bashPath, _T("/mnt/%c/"), letter);
	for (size_t i = 3, j = _tcsclen(bashPath); absolutePath[i] != _T('\0'); ++i, ++j)
	{
		if (absolutePath[i] == _T('\\'))
		{
			bashPath[j] = _T('/');
		}
		else
		{
			bashPath[j] = absolutePath[i];
		}
	}

	return bashPath;
}

TCHAR *Escape(const TCHAR *string)
{
	if (!string)
	{
		return nullptr;
	}

	int newLength = 1; //terminating null
	for (size_t i = 0; string[i] != _T('\0'); ++i)
	{
		++newLength;
		if (string[i] == _T('"') || string[i] == _T('\\'))
		{
			++newLength;
		}
	}

	TCHAR *escaped = (TCHAR *)calloc(newLength, sizeof(TCHAR));
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

int GetExitCode(const TCHAR *statusPath)
{
	auto hFile = CreateFile(statusPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
	if (hFile == INVALID_HANDLE_VALUE || hFile == nullptr)
	{
		//file does not exist
		return -1;
	}

	char buffer[128];
	DWORD bytesRead = 0;
	ReadFile(hFile, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
	if (bytesRead == 0)
	{
		//file was empty
		CloseHandle(hFile);
		return -1;
	}
	buffer[bytesRead] = '\0';

	int exitCode = atoi(buffer);

	CloseHandle(hFile);
	return exitCode;
}

int _tmain(int argc, TCHAR *argv[])
{
	const TCHAR *argumentStr = GetArgumentString(argv);

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
	bool debug = true;
#else
	bool debug = false;
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
		debug = true;
		argument = TrimStart(argument + 2);
	}

	//path to file where status code will be written
	TCHAR tempPath[MAX_PATH];
	ExpandEnvironmentStrings(_T("%temp%"), tempPath, _countof(tempPath));
	TCHAR statusPath[MAX_PATH];
	GetTempFileName(tempPath, _T("wsl"), 0, statusPath);

	auto bashStatusPath = MakeBashPath(statusPath);
	if (bashStatusPath == nullptr)
	{
		_ftprintf(stderr, _T("Unable to create status output file!\n"));
		ExitProcess(-1);
	}

	//Escape it to be placed within double quotes
	auto escaped = Escape(argument);
	int temp = sizeof(escaped);
	//this is a filthy hack that works
	//if you execute bash -c "xxxx; echo $?" the $? is substituted from the start and always returns 0
	//we create the $? string by echoing each letter separately, then execute the result to get what we actually want
	//TCHAR *format = _T("bash -c \"%s; echo -n $? > \\\"%s\\\"\"");
	TCHAR *format = _T("bash -c \"%s; echo -n `echo -n \$; echo -n ?` > \\\"%s\\\"\"");
	size_t length = _tcsclen(format) + _tcslen(escaped) + _tcslen(bashStatusPath) + 1;

	TCHAR *lpArg = (TCHAR *)alloca(sizeof(TCHAR) * length);
	ZeroMemory(lpArg, sizeof(TCHAR) * length);
	_stprintf(lpArg, format, escaped, bashStatusPath);

	TCHAR currentDir[MAX_PATH] = { 0 };
	GetCurrentDirectory(_countof(currentDir), currentDir);

	if (debug)
	{
		_ftprintf(stderr, _T("> %s\n"), lpArg);
	}

	auto startInfo = STARTUPINFO{ 0 };
	auto pInfo = PROCESS_INFORMATION{ 0 };
	bool success = CreateProcess(bash, lpArg, nullptr, nullptr, true, 0, nullptr, currentDir, &startInfo, &pInfo);

	if (!success)
	{
		_ftprintf(stderr, _T("Failed to create process. Last error: 0x%x\n"), GetLastError());
		return GetLastError();
	}

	WaitForSingleObject(pInfo.hProcess, INFINITE);
	DWORD bashExitCode = 0;
	while (GetExitCodeProcess(pInfo.hProcess, &bashExitCode) == TRUE)
	{
		if (bashExitCode == STILL_ACTIVE)
		{
			//somehow process hasn't terminated according to the kernel
			continue;
		}
		break;
	}

	int exitCode = 0;
	if (bashExitCode != 0)
	{
		//there was an error during bash execution
		//pass the exit code through
		exitCode = bashExitCode;
	}
	else
	{
		exitCode = GetExitCode(statusPath);
	}
	CloseHandle(pInfo.hProcess);

	return exitCode;
}
