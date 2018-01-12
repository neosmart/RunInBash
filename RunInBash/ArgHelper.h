#pragma once

inline bool is_any_of(const TCHAR *value, const TCHAR *arg)
{
	return _tcsicmp(value, arg) == 0;
}

template <typename... Args>
bool is_any_of(const TCHAR *value, const TCHAR *arg1, Args... args)
{
	if (is_any_of(value, arg1))
	{
		return true;
	}
	return is_any_of(value, args...);
}