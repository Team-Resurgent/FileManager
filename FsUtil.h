#ifndef FSUTIL_H
#define FSUTIL_H

/*
============================================================================
 FsUtil
  - Drive letter mapping (C/E/F/G/X/Y/Z/D) via IoCreateSymbolicLink
  - Drive discovery and drive-list item building
  - Directory listing + path utilities
  - Basic file/dir ops (delete, mkdir-if-needed, size, free space)
  - Copy-with-progress infrastructure
  - FATX cache partition formatting (X/Y/Z) via XDK XapiFormatFATVolumeEx
  - .xbe launching (remap D: and call XLaunchNewImageA)
============================================================================
*/

#include <xtl.h>
#include <vector>

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#endif

// Represents one entry in a pane listing (file, directory, drive root, or "..").
struct Item {
    char        name[256];   // File/dir name or drive-root string (e.g. "E:\")
    bool        isDir;       // True if directory (including drive roots and "..")
    ULONGLONG   size;        // File size (0 for dirs/roots/"..")
    bool        isUpEntry;   // True only for the synthetic ".." row
    bool        marked;      // UI mark flag (selection for batch ops)
};

// ----- Drive mapping / discovery --------------------------------------------
// Create the standard DOS links (C/E/F/G/X/Y/Z/D) to their kernel device paths.
void MapStandardDrives_Io();

// Probe which of the standard roots are present (used by BuildDriveItems).
void RescanDrives();

// Fill 'out' with one Item per present drive root (e.g. "E:\", "F:\", ...).
void BuildDriveItems(std::vector<Item>& out);

// ----- Directory listing -----------------------------------------------------
// List files/directories in 'path'. For non-root folders, prepends a ".." entry.
// The vector is sorted directories-first, then case-insensitive by name.
bool ListDirectory(const char* path, std::vector<Item>& out);

// ----- Path helpers ----------------------------------------------------------
// JoinPath(dst, cap, base, name): dst = base + '\' + name (handles trailing '\').
void JoinPath(char* dst, size_t cap, const char* base, const char* name);

// EnsureTrailingSlash("E:\Games") -> "E:\Games\"
void EnsureTrailingSlash(char* s, size_t cap);

// ParentPath("E:\Games\Foo") -> "E:\Games\"  ; ParentPath("E:\") -> ""
void ParentPath(char* path);

// NormalizeDirA("E:") => "E:\" ; always ensures trailing slash.
void NormalizeDirA(char* s);

// True for exactly "X:\" form (length 3, colon, backslash).
bool IsDriveRoot(const char* p);

// ----- Simple file/dir ops ---------------------------------------------------
// Existence checks / mkdir-if-needed / recursive delete / recursive size.
bool DirExistsA(const char* path);
bool EnsureDirA(const char* path);
bool DeleteRecursiveA(const char* path);
ULONGLONG DirSizeRecursiveA(const char* path);

// ----- Misc info -------------------------------------------------------------
// Format byte size as "123 MB", "1.4 GB", etc.
void FormatSize(ULONGLONG sz, char* out, size_t cap);

// Free/total bytes for the drive that contains 'anyPathInDrive'.
void GetDriveFreeTotal(const char* anyPathInDrive,
                       ULONGLONG& freeBytes, ULONGLONG& totalBytes);

// Quick writability probe: tries to create+delete a tiny temp file in 'dir'.
bool CanWriteHereA(const char* dir);

// ----- FATX-ish naming rules -------------------------------------------------
// Basic Xbox FATX compatibility: replace bad chars, trim trailing space/dot,
// clamp to 42 chars, and avoid "."/".." by substituting "NewName".
bool IsBadFatxChar(char c);
void SanitizeFatxNameInPlace(char* s);

// ----- Copy progress callback ------------------------------------------------
// Called periodically during file copies. Return false to cancel copy.
typedef bool (*CopyProgressFn)(ULONGLONG bytesDone,
                               ULONGLONG bytesTotal,
                               const char* currentPath,
                               void* user);

// Register (or clear) the global progress callback + user cookie.
void SetCopyProgressCallback(CopyProgressFn fn, void* user);

// High-level recursive copy with optional bytesTotal preflight. Honors the
// progress callback and cancels if it returns false. Prevents copying a folder
// into its own subfolder.
bool CopyRecursiveWithProgressA(const char* srcPath, const char* dstDir,
                                ULONGLONG totalBytes);

// ----- .xbe launching --------------------------------------------------------
// Simple helper to check for ".xbe" extension (case-insensitive).
bool HasXbeExt(const char* name);

// Launch either a specific .xbe or a folder's default.xbe:
// - Remaps D: to the directory's *device path*
// - Calls XLaunchNewImageA("D:\file.xbe")
bool LaunchXbeA(const char* pathOrDir);

// ----- FATX formatting (cache partitions) ------------------------------------
// Formats cache partitions by *device* path (not by DOS letter) using
// XapiFormatFATVolumeEx. Pass bytesPerCluster=0 for default 16 KiB. Returns
// true on success (on failure, GetLastError() may be set for some code paths).
bool FormatCacheDrive(char driveLetter, unsigned long bytesPerCluster /* 0 = 16 KiB */);

// Convenience: format X, Y, Z in sequence; optionally clears and recreates
// E:\CACHE afterwards for compatibility with some apps.
bool FormatCacheXYZ(unsigned long bytesPerCluster /* 0 = 16 KiB */,
                    bool alsoClearECACHE);

#endif // FSUTIL_H
