#ifndef FSUTIL_H
#define FSUTIL_H

// OG Xbox / VS2003 friendly helpers used across the app.

#include <xtl.h>
#include <vector>

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#endif

// Basic listing item
struct Item {
    char        name[256];
    bool        isDir;
    ULONGLONG   size;
    bool        isUpEntry;
	bool        marked;
};

// ----- Drive mapping / discovery -----
void MapStandardDrives_Io();                 // Creates C:/E:/X:/Y:/Z:/F:/G:/D:
void RescanDrives();                         // Updates the list of present drives
void BuildDriveItems(std::vector<Item>& out);// One item per present drive

// ----- Directory listing -----
bool ListDirectory(const char* path, std::vector<Item>& out);

// ----- Path helpers -----
void JoinPath(char* dst, size_t cap, const char* base, const char* name);
void EnsureTrailingSlash(char* s, size_t cap);
void ParentPath(char* path);
void NormalizeDirA(char* s);                 // e.g. "E:" -> "E:\"
bool IsDriveRoot(const char* p);             // "E:\" -> true

// ----- Simple file/dir ops -----
bool DirExistsA(const char* path);
bool EnsureDirA(const char* path);
bool DeleteRecursiveA(const char* path);     // files and folders
ULONGLONG DirSizeRecursiveA(const char* path);

// ----- Misc -----
void FormatSize(ULONGLONG sz, char* out, size_t cap);
void GetDriveFreeTotal(const char* anyPathInDrive,
                       ULONGLONG& freeBytes, ULONGLONG& totalBytes);

bool CanWriteHereA(const char* dir);         // quick writability probe

// FATX-ish name rules
bool IsBadFatxChar(char c);
void SanitizeFatxNameInPlace(char* s);

// --- progress callback (return false to cancel, true to continue) ---
typedef bool (*CopyProgressFn)(ULONGLONG bytesDone,
                               ULONGLONG bytesTotal,
                               const char* currentPath,
                               void* user);

void SetCopyProgressCallback(CopyProgressFn fn, void* user);

// Convenience wrappers
bool CopyRecursiveWithProgressA(const char* srcPath, const char* dstDir,
                                ULONGLONG totalBytes);
// Xbe launching
bool HasXbeExt(const char* name);
bool LaunchXbeA(const char* pathOrDir);

#endif // FSUTIL_H
