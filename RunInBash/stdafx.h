#pragma once

#ifdef _DEBUG
static int debugLevel = 1;
#else
static int debugLevel = 0;
#endif

#include "targetver.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>

#include <stdio.h>
#include <tchar.h>

#include <Windows.h>