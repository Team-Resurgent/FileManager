#ifndef UNZIPLIBUTIL_H
#define UNZIPLIBUTIL_H

#include "unzipLIB.h"
#include "FileBrowserApp.h"

// ------------------------------------------------------------------
// unzipLIB filesystem callbacks
// ------------------------------------------------------------------
void* ZipFile_Open(const char* filename, int32_t* size);

void ZipFile_Close(void* p);

int32_t ZipFile_Read(void* p, uint8_t* buffer, int32_t length);

int32_t ZipFile_Seek(void* p, int32_t position, int iType);

// ------------------------------------------------------------------
// ExtractCurrentFileWithProgress & helper functions (CrunchBite)
// ------------------------------------------------------------------
int ExtractCurrentFileWithProgress(UNZIP* zip, const char* dst, bool overwrite);

void FreeUnZipBuffer();

#endif // UNZIPLIBUTIL_H
