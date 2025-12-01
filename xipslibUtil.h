#ifndef XIPSLIBUTIL_H
#define XIPSLIBUTIL_H

#include "xipslib.h"
#include "FileBrowserApp.h"

// ------------------------------------------------------------------
// CreateBak function with added progress (src-dev)
// ------------------------------------------------------------------
int CreateBakWithProgress(const char* src, bool ovr, ULONGLONG& inoutBytesDone, ULONGLONG totalBytes);

#endif // XIPSLIBUTIL_H
