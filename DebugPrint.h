#pragma once

#ifdef __cplusplus
extern "C" {
#endif
void MyDebugPrint(const char* fmt, ...);
#ifdef __cplusplus
}
#endif

// Optional: transparently redirect in *your* code.
// Include this header first (e.g., in your pch/stdafx.h) so the macro takes effect.
#ifndef DISABLE_XBUTIL_REDIRECT
#define XBUtil_DebugPrint MyDebugPrint
#endif
