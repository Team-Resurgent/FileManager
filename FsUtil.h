#ifndef FSUTIL_H
#define FSUTIL_H
/*
============================================================================
 FsUtil
  - Drive-letter mapping (C/E/F/G/X/Y/Z/D) via IoCreateSymbolicLink
  - Drive discovery + building drive-list items
  - Directory listing + path utilities
  - Basic file/dir ops (delete, mkdir-if-needed, size, free space)
  - Copy-with-progress infrastructure
  - FATX cache partition formatting (X/Y/Z)
  - .xbe launching (remap D: and call XLaunchNewImageA)
  - DVD helpers (tray/media polling & safe remount) — app uses only functions
============================================================================
*/

#include <xtl.h>
#include <vector>

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#endif

// ---------------- DVD tray codes (SMC) + normalized drive codes -------------
#ifndef TRAY_OPEN
#define TRAY_OPEN                   16
#define TRAY_CLOSED_NO_MEDIA        64
#define TRAY_CLOSED_MEDIA_PRESENT   96
#endif

#ifndef DRIVE_OPEN
#define DRIVE_OPEN                      0
#define DRIVE_NOT_READY                 1
#define DRIVE_READY                     2   // "no change" sentinel from one-shot API
#define DRIVE_CLOSED_NO_MEDIA           3
#define DRIVE_CLOSED_MEDIA_PRESENT      4
#endif

// ----- Pane item (shared with UI) -------------------------------------------
struct Item {
    char        name[256];   // File/dir name or drive-root (e.g. "E:\")
    bool        isDir;       // True if directory (incl. drive roots and "..")
    ULONGLONG   size;        // File size (0 for dirs/roots/"..")
    bool        isUpEntry;   // True only for synthetic ".." row
    bool        marked;      // UI mark flag
};

// ===== Drive mapping / discovery ============================================
void MapStandardDrives_Io();                     // Map C/E/F/G/X/Y/Z and D
void RescanDrives();                             // Recompute present roots
void BuildDriveItems(std::vector<Item>& out);    // Build UI items from roots
unsigned int QueryDriveMaskAZ();                 // Bitmask A..Z (1<<('A'+n))

// ===== Directory listing =====================================================
bool ListDirectory(const char* path, std::vector<Item>& out);

// ===== Path helpers ==========================================================
void JoinPath(char* dst, size_t cap, const char* base, const char* name);
void EnsureTrailingSlash(char* s, size_t cap);
void ParentPath(char* path);
void NormalizeDirA(char* s);
bool IsDriveRoot(const char* p);

// ===== Simple file/dir ops ===================================================
bool DirExistsA(const char* path);
bool EnsureDirA(const char* path);
bool DeleteRecursiveA(const char* path);
ULONGLONG DirSizeRecursiveA(const char* path);

// ===== Misc info =============================================================
void FormatSize(ULONGLONG sz, char* out, size_t cap);
void GetDriveFreeTotal(const char* anyPathInDrive,
                       ULONGLONG& freeBytes, ULONGLONG& totalBytes);
bool CanWriteHereA(const char* dir);

// ===== FATX-ish naming rules ================================================
bool IsBadFatxChar(char c);
void SanitizeFatxNameInPlace(char* s);

// ===== Copy progress callback ===============================================
typedef bool (*CopyProgressFn)(ULONGLONG bytesDone,
                               ULONGLONG bytesTotal,
                               const char* currentPath,
                               void* user);
void SetCopyProgressCallback(CopyProgressFn fn, void* user);
bool CopyRecursiveWithProgressA(const char* srcPath, const char* dstDir,
                                ULONGLONG totalBytes);

// ===== extension functions ==================================================
const char* GetExtension(const char* name);
bool HasXbeExt(const char* name);

// ===== .xbe launching =======================================================
bool LaunchXbeA(const char* pathOrDir);

// ===== FATX formatting (cache partitions) ===================================
bool FormatCacheDrive(char driveLetter, unsigned long bytesPerCluster /*0=16KiB*/);
bool FormatCacheXYZ(unsigned long bytesPerCluster /*0=16KiB*/, bool alsoClearECACHE);

// ================== DVD helpers (keep FileBrowserApp lean) ==================
// These are all you need in the app; implementation details stay in fsutil.cpp

// Quick test for "D:\..." path
bool  IsDPath(const char* p);

// Try to read the current disc’s volume serial from D:\ (returns true on success)
bool  GetDvdVolumeSerial(DWORD* outSerial);

// Map / unmap DOS D:
void  DvdMap_Io();                   // D: -> \Device\Cdrom0
void  DvdUnmap_Io();                 // delete \??\D:
void  DvdInvalidateSizeCache(); 

// Detect media type in the tray (also forces a light probe of D:\)
// Returns: 1=game, 2=video, 3=data, 0=unknown. Writes label like "DVD: Xbox Game".
int   DvdDetectMediaSimple(char* outLabel, size_t cap);

// One-shot tray/media state: returns DRIVE_* only when state changes;
// returns DRIVE_READY if no change since last call.
DWORD DvdGetDriveStateOneShot(void);

// Drop old CDFS instance and remap D: cleanly; touches D:\ to force a fresh view.
void  DvdColdRemount();

#endif // FSUTIL_H
