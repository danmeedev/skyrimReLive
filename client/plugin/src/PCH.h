#pragma once

// CommonLibSSE-NG's PCH puts `using namespace std::literals;` inside
// `namespace SKSE`, but the SKSEPluginInfo macro expands to global-scope code
// that uses string-view literals (e.g. "Name"sv). Hoisting the using to global
// scope here lets the generated __<Target>Plugin.cpp file compile.
//
// NOTE: this PCH must NOT include winsock2.h or anything else that pulls in
// <windows.h>. CommonLib's REX/W32/BASE.h #errors out if any Windows API
// surface was included before it. Winsock lives in a separate translation
// unit (Socket.cpp) that opts out of the PCH.
#include "SKSE/Impl/PCH.h"

using namespace std::literals;
