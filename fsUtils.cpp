#include "fsUtils.h"

#include "driveManager.h"

#include <Algorithm>

/*
============================================================================
 FsUtil
  - OG Xbox / XDK utility functions used by the file browser.
  - Drive letter mapping (IoCreateSymbolicLink)
  - Directory listing and basic FS ops
  - Copy/move helpers with progress callbacks
  - .xbe launcher (remaps D: and calls XLaunchNewImageA)
  - FATX cache format helpers (X/Y/Z) via XapiFormatFATVolumeEx
  - Integrated DVD helpers (tray state, media sniff, cold remount)
  - No dependency on undocumented.h; minimal kernel shims are declared here.
============================================================================
*/

// TRAY_* and DRIVE_* come from FsUtil.h (kept out of this .cpp on purpose).

#ifndef FILE_READ_ONLY_VOLUME
#define FILE_READ_ONLY_VOLUME 0x00080000u  // for GetVolumeInformationA
#endif

// ----- Small STRING helpers --------------------------------------------------
// Build an XDK STRING directly (avoid Rtl* to keep header surface tiny)
static inline void BuildString(STRING& s, const char* z){
    USHORT L=(USHORT)strlen(z); s.Length=L; s.MaximumLength=L+1; s.Buffer=(PCHAR)z;
}
// "\??\X:" is the DOS devices directory; drive letters live here
static inline void MakeDosString(char* out, size_t cap, const char* letter){
    _snprintf(out, (int)cap, "\\??\\%s", letter); out[cap-1]=0;
}

// ============================================================================
// Copy progress callback plumbing
// ============================================================================

CopyProgressFn CopyProgress::g_copyProgFn   = 0;
void*          CopyProgress::g_copyProgUser = 0;

void SetCopyProgressCallback(CopyProgressFn fn, void* user){
    CopyProgress::g_copyProgFn   = fn;
    CopyProgress::g_copyProgUser = user;
}

// ============================================================================
// Attribute & volume helpers
// ============================================================================

// Remove READONLY/SYSTEM/HIDDEN so we can delete/overwrite stubborn files.
static inline void StripROSysHiddenA(const char* path){
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    DWORD na = a & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
    if (na != a) SetFileAttributesA(path, na);
}

// If GetVolumeInformationA fails, treat D:\ (DVD) as read-only to be safe.
static bool IsReadOnlyVolumeA(const char* path){
    if (!path || !path[0]) return false;
    char root[4]; _snprintf(root, sizeof(root), "%c:\\", (char)toupper((unsigned char)path[0])); root[sizeof(root)-1]=0;
    DWORD fsFlags = 0;
    if (GetVolumeInformationA(root, NULL, 0, NULL, NULL, &fsFlags, NULL, 0))
        return (fsFlags & FILE_READ_ONLY_VOLUME) != 0;
    return (root[0] == 'D'); // OG Xbox DVD (CDFS)
}

// ============================================================================
// DVD helpers (tray state, media detect, remount, size cache)
// ============================================================================

// TRAY_* -> DRIVE_* normalize (our app logic uses DRIVE_* consistently)
static inline DWORD _NormalizeToDriveCode(DWORD code){
    if (code == TRAY_OPEN)                 return DRIVE_OPEN;
    if (code == TRAY_CLOSED_NO_MEDIA)      return DRIVE_CLOSED_NO_MEDIA;
    if (code == TRAY_CLOSED_MEDIA_PRESENT) return DRIVE_CLOSED_MEDIA_PRESENT;
    return code; // already DRIVE_* or unknown
}

// Minimal wrapper; keeps SMC calls private to this TU.
class CIoSupport {
public:
    CIoSupport(){ m_dwTrayState=0; m_dwTrayCount=0; m_dwLastTrayState=0; }
    DWORD GetTrayState(){
        HalReadSMCTrayState(&m_dwTrayState, &m_dwTrayCount); // returns TRAY_*
        m_dwLastTrayState = m_dwTrayState;
        return m_dwTrayState;
    }
    // Unused at the moment but intentionally kept as handy hooks:
    HRESULT EjectTray(){ HalWriteSMBusValue(0x20, 0x0C, FALSE, 0x00); return S_OK; }
    HRESULT CloseTray(){ HalWriteSMBusValue(0x20, 0x0C, FALSE, 0x01); return S_OK; }
private:
    DWORD m_dwTrayState, m_dwTrayCount, m_dwLastTrayState;
};

// DVD size cache (expensive to recompute on CDFS; keyed by volume serial)
static DWORD      g_dvdSerialCache = 0xFFFFFFFFu;  // 0xFFFFFFFF => unknown
static ULONGLONG  g_dvdUsedCache   = 0;            // bytes used on the disc
static ULONGLONG  g_dvdTotalCache  = 0;            // disc capacity (for reference)

static void DvdInvalidateSizeCache(){
    g_dvdSerialCache = 0xFFFFFFFFu;
    g_dvdUsedCache   = 0;
    g_dvdTotalCache  = 0;
}

// “Is D:\…” convenience (app also uses a local inline; this is exported)
bool IsDPath(const char* p){
    return p && (p[0]=='D' || p[0]=='d') && p[1]==':' && p[2]=='\\';
}

// Volume serial helper (used for cache key)
bool GetDvdVolumeSerial(DWORD* outSerial){
    if (!outSerial) return false;
    if (GetFileAttributesA("D:\\") == INVALID_FILE_ATTRIBUTES) return false;
    DWORD serial = 0;
    if (!GetVolumeInformationA("D:\\", NULL, 0, &serial, NULL, NULL, NULL, 0))
        return false;
    *outSerial = serial;
    return true;
}

// Simple media sniff: 1=game, 2=video, 3=data, 0=unknown. Writes label.
int DvdDetectMediaSimple(char* outLabel, size_t cap){
    if (!outLabel || cap==0) return 0;
    outLabel[0]=0;

    // Xbox game?
    DWORD a = GetFileAttributesA("DVD-ROM:\\default.xbe");
    if (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY)){
        _snprintf(outLabel, (int)cap, "DVD: Xbox Game"); outLabel[cap-1]=0; return 1;
    }
    // DVD-Video?
    a = GetFileAttributesA("DVD-ROM:\\VIDEO_TS");
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)){
        _snprintf(outLabel, (int)cap, "DVD: Video"); outLabel[cap-1]=0; return 2;
    }
    // Any content at all => Data
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA("DVD-ROM:\\*", &fd);
    if (h != INVALID_HANDLE_VALUE){
        do{
            const char* n = fd.cFileName;
            if (!strcmp(n,".") || !strcmp(n,"..")) continue;
            _snprintf(outLabel, (int)cap, "DVD: Data"); outLabel[cap-1]=0;
            FindClose(h); return 3;
        }while(FindNextFileA(h, &fd));
        FindClose(h);
    }
    _snprintf(outLabel, (int)cap, "DVD: Unknown"); outLabel[cap-1]=0;
    return 0;
}

// Return a DRIVE_* code only when the tray/media state *changes*; otherwise READY.
DWORD DvdGetDriveStateOneShot(){
    static DWORD      s_last = 0xFFFFFFFFu;
    static CIoSupport s_io;

    DWORD raw  = s_io.GetTrayState();         // TRAY_*
    DWORD code = _NormalizeToDriveCode(raw);  // DRIVE_*
    if (code != s_last){ s_last = code; return code; }
    return DRIVE_READY;
}

// ============================================================================
// Drive discovery for the drive list
// ============================================================================

namespace {
    // We only care about the OG Xbox set of letters
    int  g_presentIdx[16];
    int  g_presentCount = 0;

    inline int ci_cmp(const char* a,const char* b){ return _stricmp(a,b); }
}

// 26-bit A..Z mask (handy for quick change detection)
unsigned int QueryDriveMaskAZ(){
    unsigned int mask = 0;
    for (char d='A'; d<='Z'; ++d){
        char root[4] = { d, ':', '\\', 0 };
        DWORD attr = GetFileAttributesA(root);
        if (attr != INVALID_FILE_ATTRIBUTES) mask |= (1u << (d - 'A'));
    }
    return mask;
}

// Build drive items (e.g., "E:\") into 'out'.
void BuildDriveItems(std::vector<Item>& out) {
    out.clear();

    // Ask driveManager for mounted drives
    pointerVector<char*>* drives = driveManager::getMountedDrives();
    if (!drives) return;

    for (uint32_t i = 0; i < drives->count(); ++i) {
        const char* mount = drives->get(i);
        if (!mount || !mount[0]) continue;

        Item it;
        ZeroMemory(&it, sizeof(it));

        // Build root path: "HDD0-C:\"
        _snprintf(it.name, sizeof(it.name), "%s:\\", mount);
        it.name[sizeof(it.name) - 1] = 0;

        it.isDir = true;
        it.size = 0;
        it.isUpEntry = false;
        it.marked = false;

        // Icon selection (example logic)
        if (!_strnicmp(mount, "DVD", 3)) it.icon = '\x9C'; // DVD
        else if (!_strnicmp(mount, "HDD1", 4)) it.icon = '\x9B'; // HDD1 (secondary disk)
        else it.icon = '\x9A'; // HDD0 / normal}

        out.push_back(it);
    }

    delete drives; // pointerVector allocated it
}

// ============================================================================
// Path helpers
// ============================================================================

void EnsureTrailingSlash(char* s,size_t cap){
    size_t n = strlen(s);
    if(n && s[n-1] != '\\' && n + 1 < cap) { s[n] = '\\'; s[n+1] = 0; }
}

// JoinPath does not normalize components; input must be well-formed.
void JoinPath(char* dst, size_t cap, const char* base, const char* name){
    size_t bl = strlen(base);
    if(bl && base[bl-1] == '\\') _snprintf(dst, (int)cap, "%s%s", base, name);
    else _snprintf(dst, (int)cap, "%s\\%s", base, name);
    dst[cap-1] = 0;
}

bool IsDriveRoot(const char* path) {
    if (!path || !path[0]) return false;

    size_t len = strlen(path);

    // Must end with ":\"
    if (len < 3) return false;

    if (path[len - 1] != '\\') return false;

    // Find colon
    const char* colon = strchr(path, ':');
    if (!colon) return false;

    // Colon must be immediately before the slash
    if (colon[1] != '\\') return false;

    // Colon must be the *only* colon
    if (strchr(colon + 1, ':') != nullptr) return false;

    return true;
}

void ParentPath(char* path) {
    if (!path) return;

    // Already at drive list
    if (path[0] == 0) return;

    // If we're at a mounted drive root go back to drive list
    if (IsDriveRoot(path)) {
        path[0] = 0;
        return;
    }

    // Strip trailing slashes
    size_t n = strlen(path);
    while (n && path[n - 1] == '\\') path[--n] = 0;

    // Remove last path component
    char* p = strrchr(path, '\\');
    if (!p) {
        path[0] = 0;
        return;
    }

    *(p + 1) = 0;
}

void NormalizeDirA(char* s){
    // "E:" -> "E:\" ; always ensure trailing slash
    size_t n = strlen(s);
    if (n==2 && s[1]==':'){ s[2]='\\'; s[3]=0; return; }
    EnsureTrailingSlash(s, 512);
}

// Sort: directories first, then case-insensitive by name.
static bool ItemLess(const Item& a,const Item& b){
    if(a.isDir!=b.isDir) return a.isDir>b.isDir;
    return ci_cmp(a.name,b.name)<0;
}

// ============================================================================
// Directory listing
//  - Prepends a synthetic ".." entry for non-root folders.
//  - Sorts (dirs first, then by name) while keeping the ".." at index 0.
// ============================================================================
bool ListDirectory(const char* path, std::vector<Item>& out) {
    out.clear();

    // For non-root, push "..\" to allow going up.
    if (strlen(path) > 3) {
        Item up; 
        ZeroMemory(&up, sizeof(up));
        strncpy(up.name, "..\\", 3); 
        up.isDir = true; 
        up.size = 0; 
        up.isUpEntry = true; 
        up.marked = false; 
        up.icon = '\x9D';
        out.push_back(up);
    }

    char base[512]; 
    _snprintf(base,sizeof(base), "%s", path); 
    base[sizeof(base) - 1] = 0; 
    EnsureTrailingSlash(base, sizeof(base));
    char mask[512]; 
    _snprintf(mask, sizeof(mask), "%s*", base); 
    mask[sizeof(mask) - 1] = 0;

    WIN32_FIND_DATAA fd; 
    ZeroMemory(&fd, sizeof(fd));
    HANDLE h = FindFirstFileA(mask, &fd); 
    if (h == INVALID_HANDLE_VALUE) return false;
    do {
        const char* n = fd.cFileName; 
        if (!strcmp(n, ".") || !strcmp(n, "..")) continue;
        Item it; 
        ZeroMemory(&it, sizeof(it));
        strncpy(it.name, n, 255); 
        it.name[255] = 0;
        bool isZip = false;
        if (strlen(n) >= 4 && _memicmp(".zip", n + strlen(n) - 4, 4) == 0) isZip = true;
        bool isXbe = false;
        if (strlen(n) >= 4 && _memicmp(".xbe", n + strlen(n) - 4, 4) == 0) isXbe = true;
        it.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        it.size = (((ULONGLONG)fd.nFileSizeHigh) << 32) | fd.nFileSizeLow; 
        it.isUpEntry = false; 
        it.marked = false;
        it.icon = (it.isDir) ? '\x9F' : (isZip) ? '\x99' : (isXbe) ? '\x95' : '\x9E';
        out.push_back(it);

    } while (FindNextFileA(h, &fd));
    FindClose(h);

    size_t start = (strlen(path) > 3) ? 1 : 0; // keep ".." in place
    if (out.size() > start + 1) std::sort(out.begin() + start, out.end(), ItemLess);
    return true;
}

// ============================================================================
// Misc info helpers
// ============================================================================

void FormatSize(ULONGLONG bytes, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = 0;

    const ULONGLONG KB = 1024ULL;
    const ULONGLONG MB = KB * 1024ULL;
    const ULONGLONG GB = MB * 1024ULL;
    const ULONGLONG TB = GB * 1024ULL;

    double val = 0.0;
    const char* unit = "B";

    if (bytes >= TB) { val = (double)bytes / (double)TB; unit = "TB"; }
    else if (bytes >= GB) { val = (double)bytes / (double)GB; unit = "GB"; }
    else if (bytes >= MB) { val = (double)bytes / (double)MB; unit = "MB"; }
    else if (bytes >= KB) { val = (double)bytes / (double)KB; unit = "KB"; }
    else {
        _snprintf(out, (int)cap, "%llu B", (unsigned long long)bytes);
        out[cap-1] = 0;
        return;
    }

    // Two decimals keeps the footer stable but not too noisy (e.g., 3.09 GB)
    _snprintf(out, (int)cap, "%.2f %s", val, unit);
    out[cap-1] = 0;
}

// For normal drives, we return true "free / total".
// For D:, CDFS reports "free=0". To match the UI label "Free / Total" and avoid
// confusion, we intentionally return "0 / <used_on_disc>" for DVDs.
// We recompute <used_on_disc> only when the volume serial changes.
void GetDriveFreeTotal(const char* anyPathInDrive, ULONGLONG& freeBytes, ULONGLONG& totalBytes) {
    freeBytes = 0;
    totalBytes = 0;
    if (!anyPathInDrive || !anyPathInDrive[0]) return;

    // Extract mount root (up to and including ":\")
    char root[256];
    root[0] = 0;

    const char* colon = strchr(anyPathInDrive, ':');
    if (!colon || colon[1] != '\\') return; // not a valid mounted path

    size_t len = (colon - anyPathInDrive) + 2; // include ":\"
    if (len >= sizeof(root)) return;

    memcpy(root, anyPathInDrive, len);
    root[len] = 0;

    ULARGE_INTEGER avail, total, free;
    avail.QuadPart = total.QuadPart = free.QuadPart = 0;

    if (GetDiskFreeSpaceExA(root, &avail, &total, &free)) {
        freeBytes = free.QuadPart;
        totalBytes = total.QuadPart;
    }
}


// ============================================================================
// Basic FS ops
// ============================================================================

bool DirExistsA(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDirA(const char* path) {
    DWORD a = GetFileAttributesA(path);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryA(path, NULL) ? true : false;
}

// Recursively delete with safety rails:
//  - Refuses drive roots
//  - Refuses read-only volumes (CDFS / cache)
//  - Clears READONLY/SYSTEM/HIDDEN before delete
//  - Continues on child failures; tiny retry on RemoveDirectoryA
bool DeleteRecursiveA(const char* path) {
    if (!path || !path[0]) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    if (IsDriveRoot(path)) { SetLastError(ERROR_ACCESS_DENIED);     return false; }
    if (IsReadOnlyVolumeA(path)) { SetLastError(ERROR_WRITE_PROTECT); return false; }

    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) { SetLastError(ERROR_FILE_NOT_FOUND); return false; }

    // Make the target itself writable so final delete can succeed.
    StripROSysHiddenA(path);

    if (a & FILE_ATTRIBUTE_DIRECTORY) {
        // Enumerate children
        char mask[512]; JoinPath(mask, sizeof(mask), path, "*");
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(mask, &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..")) continue;

                char sub[512]; JoinPath(sub, sizeof(sub), path, fd.cFileName);

                // Clear attributes on each child before deleting
                StripROSysHiddenA(sub);

                // Best-effort delete; continue on failure
                if (!DeleteRecursiveA(sub)){
                    // Optionally capture first error here
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }

        // Try removing the (now empty) directory (with a tiny retry)
        if (!RemoveDirectoryA(path)) {
            Sleep(1);
            StripROSysHiddenA(path);
            return RemoveDirectoryA(path) ? true : false;
        }
        return true;
    }
    else {
        // File: clear attributes then delete (retry once)
        if (!DeleteFileA(path)) {
            StripROSysHiddenA(path);
            return DeleteFileA(path) ? true : false;
        }
        return true;
    }
}

// ============================================================================
// File type helpers
// ============================================================================

const char* GetExtension(const char* name){
	if (!name) return NULL;
	char* ext = NULL;
	const char* delim = strrchr(name, '.');
	if (delim && *(delim + 1)) {
		return (delim + 1);
	}
	return NULL;
}
bool HasXbeExt(const char* name){
    if (!name) return false;
	const char* ext = GetExtension(name);
    return (ext && _stricmp(ext, "xbe") == 0);
}

// ============================================================================
// Copy (chunked) + recursive copy with progress/cancel
//  - 64 KiB fixed buffer (predictable RAM use on Xbox)
//  - Normalizes dest attributes before overwrite
//  - Progress callback may cancel; on cancel we delete the partial output
// ============================================================================

static bool CopyFileChunkedA(const char* s, const char* d,
                             ULONGLONG& inoutBytesDone, ULONGLONG totalBytes)
{
    // Open source (read-only, allow readers to share)
    HANDLE hs = CreateFileA(s, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hs == INVALID_HANDLE_VALUE) return false;

    // Preflight dest: directory collision -> error; else clear R/O etc.
    DWORD da = GetFileAttributesA(d);
    if (da != INVALID_FILE_ATTRIBUTES) {
        if (da & FILE_ATTRIBUTE_DIRECTORY) {
            CloseHandle(hs);
            SetLastError(ERROR_ALREADY_EXISTS);
            return false;
        }
        DWORD na = da & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
        if (na != da) SetFileAttributesA(d, na);
    }

    // Create/overwrite dest (no sharing)
    HANDLE hd = CreateFileA(d, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hd == INVALID_HANDLE_VALUE){ CloseHandle(hs); return false; }

    const DWORD BUFSZ = 64 * 1024;
    char* buf = (char*)LocalAlloc(LMEM_FIXED, BUFSZ);
    if (!buf){ CloseHandle(hs); CloseHandle(hd); return false; }

    bool ok = true;
    for (;;){
        DWORD rd = 0;
        if (!ReadFile(hs, buf, BUFSZ, &rd, NULL)) { ok = false; break; }
        if (rd == 0) break;

        DWORD wr = 0;
        if (!WriteFile(hd, buf, rd, &wr, NULL)) { ok = false; break; }

        inoutBytesDone += wr;

        // Progress/cancel callback
        if (CopyProgress::g_copyProgFn){
            if (!CopyProgress::g_copyProgFn(inoutBytesDone, totalBytes, s, CopyProgress::g_copyProgUser)){
                ok = false; break; // canceled
            }
        }
    }

    LocalFree(buf);
    CloseHandle(hs);
    CloseHandle(hd);

    // Normalize dest; on failure, remove partial
    if (!ok) { DeleteFileA(d); }
    else     { SetFileAttributesA(d, FILE_ATTRIBUTE_NORMAL); }
    return ok;
}

// Core recursive copy: directory creation + per-file copy.
static bool CopyRecursiveCoreA(const char* srcPath, const char* dstDir,
                               ULONGLONG& inoutBytesDone, ULONGLONG totalBytes)
{
    DWORD a = GetFileAttributesA(srcPath);
    if (a == INVALID_FILE_ATTRIBUTES) return false;

    const char* base = strrchr(srcPath, '\\'); base = base ? base+1 : srcPath;
    char dstPath[512]; JoinPath(dstPath, sizeof(dstPath), dstDir, base);

    if (a & FILE_ATTRIBUTE_DIRECTORY){
        if (!EnsureDirA(dstPath)) return false;

        // Do NOT preserve source dir attributes
        SetFileAttributesA(dstPath, FILE_ATTRIBUTE_NORMAL);

        char mask[512]; JoinPath(mask, sizeof(mask), srcPath, "*");
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(mask, &fd);
        if (h != INVALID_HANDLE_VALUE){
            do{
                if (!strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..")) continue;
                char subSrc[512]; JoinPath(subSrc, sizeof(subSrc), srcPath, fd.cFileName);
                if (!CopyRecursiveCoreA(subSrc, dstPath, inoutBytesDone, totalBytes))
                    { FindClose(h); return false; }
            }while (FindNextFileA(h,&fd));
            FindClose(h);
        }
        return true;
    } else {
        return CopyFileChunkedA(srcPath, dstPath, inoutBytesDone, totalBytes);
    }
}

// Helpers to detect "copy into own subfolder" (case-insensitive).
static void NormalizeSlashEnd(char* s, size_t cap) {
    size_t n = strlen(s);
    if (n && s[n-1] != '\\' && n+1 < cap) { s[n] = '\\'; s[n+1] = 0; }
}
static bool IsSubPathCI(const char* parent, const char* child) {
    char p[512], c[512];
    _snprintf(p, sizeof(p), "%s", parent); p[sizeof(p)-1]=0; NormalizeSlashEnd(p, sizeof(p));
    _snprintf(c, sizeof(c), "%s", child ); c[sizeof(c)-1]=0; NormalizeSlashEnd(c, sizeof(c));
    return _strnicmp(p, c, strlen(p)) == 0;
}

// Public entry for recursive copy with progress and a safety check.
bool CopyRecursiveWithProgressA(const char* srcPath, const char* dstDir,
                                ULONGLONG totalBytes)
{
    // Compute dstTop = dstDir\basename(srcPath)
    const char* base = strrchr(srcPath, '\\'); base = base ? base+1 : srcPath;
    char dstTop[512]; JoinPath(dstTop, sizeof(dstTop), dstDir, base);

    // Guard: prevent copying into own subfolder
    if (IsSubPathCI(srcPath, dstTop)) { SetLastError(ERROR_INVALID_PARAMETER); return false; }

    // Optional free-space preflight (skip if unknown)
    if (totalBytes > 0) {
        ULONGLONG freeB=0, totalB=0;
        GetDriveFreeTotal(dstDir, freeB, totalB);
        if (freeB > 0 && freeB < totalBytes) { SetLastError(ERROR_DISK_FULL); return false; }
    }

    ULONGLONG done = 0;
    return CopyRecursiveCoreA(srcPath, dstDir, done, totalBytes);
}

// ============================================================================
// Size calculation (recursive)
// ============================================================================

ULONGLONG DirSizeRecursiveA(const char* path){
    ULONGLONG sum = 0;
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return 0;

    if (a & FILE_ATTRIBUTE_DIRECTORY){
        char mask[512]; JoinPath(mask, sizeof(mask), path, "*");
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(mask, &fd);
        if (h != INVALID_HANDLE_VALUE){
            do{
                if (!strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..")) continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY){
                    char sub[512]; JoinPath(sub, sizeof(sub), path, fd.cFileName);
                    sum += DirSizeRecursiveA(sub);
                } else {
                    sum += (((ULONGLONG)fd.nFileSizeHigh)<<32) | fd.nFileSizeLow;
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
    } else {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)){
            sum += (((ULONGLONG)fad.nFileSizeHigh)<<32) | fad.nFileSizeLow;
        }
    }
    return sum;
}

// ============================================================================
// Quick writability probe
// ============================================================================

bool CanWriteHereA(const char* dir){
    char test[512];
    JoinPath(test, sizeof(test), dir, ".__xwtest$__");
    test[sizeof(test)-1]=0;
    HANDLE h = CreateFileA(test, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    DeleteFileA(test);
    return true;
}

// ============================================================================
// FATX-ish naming rules (close to dashboard behavior)
// ============================================================================

bool IsBadFatxChar(char c){
    if ((unsigned char)c < 32) return true;
    const char* bad = "\\/:*?\"<>|+,;=[]";
    return (strchr(bad, c) != NULL);
}

void SanitizeFatxNameInPlace(char* s){
    for (char* p=s; *p; ++p) if (IsBadFatxChar(*p)) *p = '_';
    int n = (int)strlen(s);
    while (n>0 && (s[n-1]==' ' || s[n-1]=='.')) s[--n]=0;
    if (n > 42) { s[42]=0; n=42; }
    if (n==0 || (strcmp(s,".")==0) || (strcmp(s,"..")==0)) strcpy(s, "NewName");
}

// ============================================================================
// .xbe launcher
//  - Accepts either a specific .xbe or a directory (implies "default.xbe").
//  - Repoints D: to the folder's *device path* and calls XLaunchNewImageA.
// ============================================================================

void GetDevicePathFromMountedPath(char* devPath, const char* mountPath) {
    if (!_memicmp(mountPath, "DVD-ROM", 7)) strcpy(devPath, "\\Device\\Cdrom0");
    else if (!_memicmp(mountPath, "HDD0-C", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition2");
    else if (!_memicmp(mountPath, "HDD0-E", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition1");
    else if (!_memicmp(mountPath, "HDD0-F", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition6");
    else if (!_memicmp(mountPath, "HDD0-G", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition7");
    else if (!_memicmp(mountPath, "HDD0-H", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition8");
    else if (!_memicmp(mountPath, "HDD0-I", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition9");
    else if (!_memicmp(mountPath, "HDD0-J", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition10");
    else if (!_memicmp(mountPath, "HDD0-K", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition11");
    else if (!_memicmp(mountPath, "HDD0-L", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition12");
    else if (!_memicmp(mountPath, "HDD0-M", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition13");
    else if (!_memicmp(mountPath, "HDD0-N", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition14");
    else if (!_memicmp(mountPath, "HDD0-X", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition3");
    else if (!_memicmp(mountPath, "HDD0-Y", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition4");
    else if (!_memicmp(mountPath, "HDD0-Z", 6)) strcpy(devPath, "\\Device\\Harddisk0\\Partition5");
    else if (!_memicmp(mountPath, "HDD0-C", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition2");
    else if (!_memicmp(mountPath, "HDD0-E", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition1");
    else if (!_memicmp(mountPath, "HDD0-F", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition6");
    else if (!_memicmp(mountPath, "HDD0-G", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition7");
    else if (!_memicmp(mountPath, "HDD0-H", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition8");
    else if (!_memicmp(mountPath, "HDD0-I", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition9");
    else if (!_memicmp(mountPath, "HDD0-J", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition10");
    else if (!_memicmp(mountPath, "HDD0-K", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition11");
    else if (!_memicmp(mountPath, "HDD0-L", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition12");
    else if (!_memicmp(mountPath, "HDD0-M", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition13");
    else if (!_memicmp(mountPath, "HDD0-N", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition14");
    else if (!_memicmp(mountPath, "HDD0-X", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition3");
    else if (!_memicmp(mountPath, "HDD0-Y", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition4");
    else if (!_memicmp(mountPath, "HDD0-Z", 6)) strcpy(devPath, "\\Device\\Harddisk1\\Partition5");
    else {
        strcpy(devPath, mountPath);
        return;
    }

    char* delim = strchr(mountPath, '\\');
    if (delim != NULL) {
        strcat(devPath, delim);
    }
}

bool LaunchXbeA(const char* pathOrDir)
{
    if (!pathOrDir || !pathOrDir[0]){ SetLastError(ERROR_INVALID_PARAMETER); return false; }

    // Compute directory+file to mount/launch
    char dir[512]; dir[0]=0;
    char file[256]; file[0]=0;

    if (HasXbeExt(pathOrDir)){
        // path = "...\<name>.xbe"
        const char* slash = strrchr(pathOrDir, '\\');
        _snprintf(file, sizeof(file), "%s", slash ? slash+1 : pathOrDir);
        file[sizeof(file)-1]=0;

        _snprintf(dir, sizeof(dir), "%s", pathOrDir); dir[sizeof(dir)-1]=0;
        ParentPath(dir);
        EnsureTrailingSlash(dir, sizeof(dir));
    } else {
        // path = folder: launch default.xbe inside it
        _snprintf(dir, sizeof(dir), "%s", pathOrDir); dir[sizeof(dir)-1]=0;
        EnsureTrailingSlash(dir, sizeof(dir));
        _snprintf(file, sizeof(file), "default.xbe");
    }

    // Pre-check file exists before remapping D:
    char pre[512]; JoinPath(pre, sizeof(pre), dir, file);
    if (GetFileAttributesA(pre) == INVALID_FILE_ATTRIBUTES){
        SetLastError(ERROR_FILE_NOT_FOUND);
        return false;
    }
    
    char devPath[512];
    GetDevicePathFromMountedPath(devPath, dir);

    if (devPath[strlen(devPath) - 1] == '\\') devPath[strlen(devPath) - 1] = '\0'; // Remove trailing slash

    // Repoint D: to device path of 'dir'
    char dosD[16]; MakeDosString(dosD, sizeof(dosD), "D:");
    STRING sDos; BuildString(sDos, dosD);
    IoDeleteSymbolicLink(&sDos); // ignore result

    STRING sDev; BuildString(sDev, devPath);
    LONG st = IoCreateSymbolicLink(&sDos, &sDev);
    if (st != 0){ SetLastError(ERROR_ACCESS_DENIED); return false; } // STATUS_SUCCESS == 0

    // Launch D:\<file>
    char launchPath[512];
    _snprintf(launchPath, sizeof(launchPath), "D:\\%s", file);
    launchPath[sizeof(launchPath)-1]=0;

    DWORD rc = XLaunchNewImageA(launchPath, (PLAUNCH_DATA)NULL);
    if (rc == ERROR_SUCCESS) return true;

    SetLastError(rc);
    return false;
}

bool FileExistsA(const char* path) {
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool WriteAllA(const char* path, const void* data, DWORD size) {
    HANDLE h = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0; BOOL ok = WriteFile(h, data, size, &wrote, NULL); CloseHandle(h);
    return ok && wrote == size;
}

static bool FormatDeviceFatx(const char* devicePath, unsigned long bytesPerCluster)
{
    if (!devicePath || !devicePath[0]) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    if (bytesPerCluster == 0) bytesPerCluster = 16 * 1024; // default for cache

    // Manual init (avoid RtlInitAnsiString to keep header surface small)
    STRING vol;
    vol.Buffer = (PCHAR)devicePath;
    vol.Length = (USHORT)strlen(devicePath);
    vol.MaximumLength = vol.Length + 1;

    // Nonzero on success per XDK
    BOOL ok = XapiFormatFATVolumeEx(&vol, bytesPerCluster);
    if (!ok) return false;
    return true;
}

bool FormatCacheXYZ(unsigned long bytesPerCluster) {
    // Try to format HDD0 and keep error
    bool okX = FormatDeviceFatx("\\Device\\Harddisk0\\Partition3", bytesPerCluster);
    bool okY = FormatDeviceFatx("\\Device\\Harddisk0\\Partition4", bytesPerCluster);
    bool okZ = FormatDeviceFatx("\\Device\\Harddisk0\\Partition5", bytesPerCluster);

    // Try to format HDD1 and throw away error
    FormatDeviceFatx("\\Device\\Harddisk1\\Partition3", bytesPerCluster);
    FormatDeviceFatx("\\Device\\Harddisk1\\Partition4", bytesPerCluster);
    FormatDeviceFatx("\\Device\\Harddisk1\\Partition5", bytesPerCluster);

    return okX && okY && okZ;
}