#pragma once

#include "unzipLIB.h"
#include "main.h"

// unzipLIB filesystem callbacks
void* ZipFile_Open(const char* filename, int32_t* size);
void ZipFile_Close(void* p);
int32_t ZipFile_Read(void* p, uint8_t* buffer, int32_t length);
int32_t ZipFile_Seek(void* p, int32_t position, int iType);

// ExtractCurrentFileWithProgress & helper functions
int ExtractCurrentFileWithProgress(UNZIP* zip, const char* dst, bool overwrite);

void FreeUnZipBuffer();
