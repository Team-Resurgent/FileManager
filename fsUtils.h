#pragma once

#include <XTL.h>
#include <Vector>

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#endif

// ---------------- DVD tray codes (SMC) -------------
#ifndef TRAY_OPEN
#define TRAY_OPEN                   16
#define TRAY_CLOSED_NO_MEDIA        64
#define TRAY_CLOSED_MEDIA_PRESENT   96
#define TRAY_NO_CHANGE                   0
#endif

// ----- Pane item (shared with UI) -------------------------------------------
struct Item {
    char        name[256];   // File/dir name or drive-root (e.g. "E:\")
    bool        isDir;       // True if directory (incl. drive roots and "..")
    ULONGLONG   size;        // File size (0 for dirs/roots/"..")
    bool        isUpEntry;   // True only for synthetic ".." row
    bool        marked;      // UI mark flag
    char        icon;
};

// ===== Drive mapping / discovery ============================================
void BuildDriveItems(std::vector<Item>& out);    // Build UI items from roots
unsigned int QueryDriveMaskAZ();                 // Bitmask A..Z (1<<('A'+n))

// ===== Directory listing =====================================================
bool ListDirectory(const char* path, std::vector<Item>& out);

// ===== Path helpers ==========================================================
void JoinPath(char* dst, size_t cap, const char* base, const char* name);
void EnsureTrailingSlash(char* s, size_t cap);
bool IsDriveRoot(const char* p);
void ParentPath(char* path);
void NormalizeDirA(char* s);

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
typedef bool (*CopyProgressFn)(LONGLONG bytesDone,
                               LONGLONG bytesTotal,
                               const char* currentPath,
                               void* user);
struct CopyProgress {
    static CopyProgressFn g_copyProgFn;
    static void* g_copyProgUser;
};
void SetCopyProgressCallback(CopyProgressFn fn, void* user);
bool CopyRecursiveWithProgressA(const char* srcPath, const char* dstDir,
                                ULONGLONG totalBytes);

// ===== extension functions ==================================================
const char* GetExtension(const char* name);
bool HasXbeExt(const char* name);

// ===== .xbe launching =======================================================
bool LaunchXbeA(const char* pathOrDir);

// ================== DVD helpers (keep FileBrowserApp lean) ==================
// These are all you need in the app; implementation details stay in fsutil.cpp

// Quick test for "D:\..." path
bool  IsDPath(const char* p);

// Try to read the current disc’s volume serial from D:\ (returns true on success)
bool  GetDvdVolumeSerial(DWORD* outSerial);


// Detect media type in the tray (also forces a light probe of D:\)
// Returns: 1=game, 2=video, 3=data, 0=unknown. Writes label like "DVD: Xbox Game".
int   DvdDetectMediaSimple(char* outLabel, size_t cap);

// One-shot tray/media state: returns DRIVE_* only when state changes;
// returns DRIVE_READY if no change since last call.
DWORD DvdGetDriveStateOneShot(void);

bool FileExistsA(const char* path);
bool WriteAllA(const char* path, const void* data, DWORD size);

bool FormatCacheXYZ(unsigned long bytesPerCluster);