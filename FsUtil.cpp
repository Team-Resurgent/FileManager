#include "FsUtil.h"
#include <algorithm>
#include <string.h>
#include <ctype.h>
#include <stdio.h>  // _snprintf

/*
============================================================================
 FsUtil
  - OG Xbox / XDK utility functions used by the file browser.
  - Drive letter mapping (IoCreateSymbolicLink)
  - Directory listing and basic FS ops
  - Copy/move helpers with progress callbacks
  - .xbe launcher (remaps D: and calls XLaunchNewImageA)
  - FATX cache format helpers (X/Y/Z) via XapiFormatFATVolumeEx
============================================================================
*/


// --- xboxkrnl drive link APIs ------------------------------------------------
// We create DOS-style links like "\??\E:" that point to kernel device paths such
// as "\Device\Harddisk0\Partition1". STATUS_SUCCESS == 0 on Xbox.
extern "C" {
    typedef struct _STRING { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } STRING, *PSTRING;
    LONG __stdcall IoCreateSymbolicLink(PSTRING SymbolicLinkName, PSTRING DeviceName);
    LONG __stdcall IoDeleteSymbolicLink(PSTRING SymbolicLinkName);
}

// Small helpers to build XDK STRINGs and "\??\X:" strings.
static inline void BuildString(STRING& s,const char* z){
    USHORT L=(USHORT)strlen(z); s.Length=L; s.MaximumLength=L+1; s.Buffer=(PCHAR)z;
}
static inline void MakeDosString(char* out,size_t cap,const char* letter){
    _snprintf(out,(int)cap,"\\??\\%s",letter); out[cap-1]=0;
}

#ifndef FILE_READ_ONLY_VOLUME
#define FILE_READ_ONLY_VOLUME 0x00080000u  // Win32 FILE_READ_ONLY_VOLUME (for GetVolumeInformationA)
#endif

// ============================================================================
// Copy progress callback plumbing
// ============================================================================
static CopyProgressFn g_copyProgFn = 0;
static void*          g_copyProgUser = 0;

void SetCopyProgressCallback(CopyProgressFn fn, void* user){
    g_copyProgFn   = fn;
    g_copyProgUser = user;
}

// ============================================================================
// Attribute & volume helpers
// ============================================================================

// Remove READONLY/SYSTEM/HIDDEN so we can delete/overwrite.
static inline void StripROSysHiddenA(const char* path){
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    DWORD na = a & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
    if (na != a) SetFileAttributesA(path, na);
}

// Detect read-only volumes. If GetVolumeInformationA fails, treat D:\ (DVD)
// as read-only to be safe on OG Xbox.
static bool IsReadOnlyVolumeA(const char* path){
    if (!path || !path[0]) return false;
    char root[4]; _snprintf(root, sizeof(root), "%c:\\", (char)toupper((unsigned char)path[0])); root[sizeof(root)-1]=0;
    DWORD fsFlags = 0;
    if (GetVolumeInformationA(root, NULL, 0, NULL, NULL, &fsFlags, NULL, 0))
        return (fsFlags & FILE_READ_ONLY_VOLUME) != 0;
    return (root[0] == 'D'); // OG Xbox DVD
}

// ============================================================================
// Drive letter mapping (DOS -> device) via IoCreateSymbolicLink
// ============================================================================

static BOOL MapLetterToDevice(const char* letter,const char* devicePath){
    // Remove any stale mapping first
    char dosBuf[16]={0}; MakeDosString(dosBuf,sizeof(dosBuf),letter);
    STRING sDos; BuildString(sDos,dosBuf); IoDeleteSymbolicLink(&sDos);

    // Create new mapping
    STRING sDev; BuildString(sDev,devicePath);
    if (IoCreateSymbolicLink(&sDos,&sDev)!=0) return FALSE; // STATUS_SUCCESS==0

    // Light probe to confirm the new link resolves
    char root[8]={0}; _snprintf(root,sizeof(root),"%s\\",letter);
    if (GetFileAttributesA(root)==INVALID_FILE_ATTRIBUTES){
        IoDeleteSymbolicLink(&sDos); return FALSE;
    }
    return TRUE;
}

// Standard OG Xbox letters: C/E/X/Y/Z/F/G plus D (DVD).
void MapStandardDrives_Io(){
    MapLetterToDevice("D:","\\Device\\Cdrom0");
    MapLetterToDevice("C:","\\Device\\Harddisk0\\Partition2");
    MapLetterToDevice("E:","\\Device\\Harddisk0\\Partition1");
    MapLetterToDevice("X:","\\Device\\Harddisk0\\Partition3");
    MapLetterToDevice("Y:","\\Device\\Harddisk0\\Partition4");
    MapLetterToDevice("Z:","\\Device\\Harddisk0\\Partition5");
    MapLetterToDevice("F:","\\Device\\Harddisk0\\Partition6");
    MapLetterToDevice("G:","\\Device\\Harddisk0\\Partition7");
}

// ============================================================================
// FATX cache format helpers (X/Y/Z) using XDK's XapiFormatFATVolumeEx
//  - We pass a *device path* (not a drive letter) to guarantee a real format.
//  - bytesPerCluster = 0 -> 16 KiB (typical for cache partitions)
// ============================================================================

extern "C" {
typedef struct _ANSI_STRING_ { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } ANSI_STRING_, *PANSI_STRING_;
void  __stdcall RtlInitAnsiString(PANSI_STRING_ DestinationString, const char* SourceString);
BOOL  WINAPI    XapiFormatFATVolumeEx(PANSI_STRING_ VolumePath, ULONG BytesPerCluster);
}

// Translate X/Y/Z drive letters to their device paths.
static const char* _CacheLetterToDevice(char dl)
{
    char c = (dl >= 'a' && dl <= 'z') ? (char)(dl - 32) : dl;
    switch (c) {
        case 'X': return "\\Device\\Harddisk0\\Partition3";
        case 'Y': return "\\Device\\Harddisk0\\Partition4";
        case 'Z': return "\\Device\\Harddisk0\\Partition5";
        default:  return 0;
    }
}

// Low-level format call: writes FATX superblock + FAT and empties root.
static bool _FormatDeviceFatx(const char* devicePath, unsigned long bytesPerCluster)
{
    if (!devicePath || !devicePath[0]) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    if (bytesPerCluster == 0) bytesPerCluster = 16 * 1024; // default for cache

    ANSI_STRING_ vol;
    RtlInitAnsiString(&vol, devicePath);

    // Nonzero on success per XDK
    BOOL ok = XapiFormatFATVolumeEx(&vol, bytesPerCluster);
    if (!ok) {
        // XapiFormatFATVolumeEx does not expose a concrete last error.
        // Caller reads only success/failure.
        return false;
    }
    return true;
}

// Public: format exactly one cache drive (X/Y/Z). Temporarily unmaps the DOS
// link to avoid open-handle surprises, then restores standard mapping.
bool FormatCacheDrive(char driveLetter, unsigned long bytesPerCluster)
{
    const char* dev = _CacheLetterToDevice(driveLetter);
    if (!dev) { SetLastError(ERROR_INVALID_PARAMETER); return false; }

    // Best-effort unmap DOS link first
    char dosBuf[16] = {0};
    _snprintf(dosBuf, sizeof(dosBuf), "\\??\\%c:", (driveLetter >= 'a' && driveLetter <= 'z') ? (driveLetter - 32) : driveLetter);
    STRING sDos; BuildString(sDos, dosBuf);
    IoDeleteSymbolicLink(&sDos);

    bool ok = _FormatDeviceFatx(dev, bytesPerCluster);

    // Always restore standard letters so the app keeps working
    MapStandardDrives_Io();
    return ok;
}

// Format X, Y, Z; optionally blow away and recreate E:\CACHE for good measure.
bool FormatCacheXYZ(unsigned long bytesPerCluster, bool alsoClearECACHE)
{
    bool okX = FormatCacheDrive('X', bytesPerCluster);
    bool okY = FormatCacheDrive('Y', bytesPerCluster);
    bool okZ = FormatCacheDrive('Z', bytesPerCluster);

    if (alsoClearECACHE) {
        DeleteRecursiveA("E:\\CACHE");
        EnsureDirA("E:\\CACHE");
    }
    return okX && okY && okZ;
}

// ============================================================================
// Drive discovery for the drive list
// ============================================================================

namespace {
    const char* kRoots[] = { "C:\\", "D:\\", "E:\\", "F:\\", "G:\\", "X:\\", "Y:\\", "Z:\\" };
    const int   kNumRoots = sizeof(kRoots)/sizeof(kRoots[0]);
    int  g_presentIdx[16];
    int  g_presentCount = 0;

    inline int ci_cmp(const char* a,const char* b){ return _stricmp(a,b); }
}

// Probe which standard roots exist and record indices into kRoots[]
void RescanDrives(){
    g_presentCount = 0;
    for (int i=0;i<kNumRoots && g_presentCount<(int)(sizeof(g_presentIdx)/sizeof(g_presentIdx[0])); ++i){
        DWORD a = GetFileAttributesA(kRoots[i]);
        if (a != INVALID_FILE_ATTRIBUTES) g_presentIdx[g_presentCount++] = i;
    }
}

// Build drive items (e.g., "E:\") into 'out'.
void BuildDriveItems(std::vector<Item>& out){
    out.clear();
    for (int j=0;j<g_presentCount;++j){
        int i = g_presentIdx[j];
        Item it; ZeroMemory(&it, sizeof(it));
        strncpy(it.name, kRoots[i], 255); it.name[255]=0;
        it.isDir=true; it.size=0; it.isUpEntry=false; it.marked=false; 
        out.push_back(it);
    }
}

// ============================================================================
// Path helpers
// ============================================================================

void EnsureTrailingSlash(char* s,size_t cap){
    size_t n=strlen(s);
    if(n && s[n-1]!='\\' && n+1<cap){ s[n]='\\'; s[n+1]=0; }
}

void JoinPath(char* dst,size_t cap,const char* base,const char* name){
    size_t bl=strlen(base);
    if(bl && base[bl-1]=='\\') _snprintf(dst,(int)cap,"%s%s",base,name);
    else                        _snprintf(dst,(int)cap,"%s\\%s",base,name);
    dst[cap-1]=0;
}

void ParentPath(char* path){
    size_t n=strlen(path);
    if (n <= 3) { path[0]=0; return; }
    while (n && path[n-1]=='\\') { path[--n]=0; }
    char* p = strrchr(path,'\\');
    if (!p) { path[0]=0; return; }
    if (p == path+2) *(p+1)=0; else *p=0;
}

bool IsDriveRoot(const char* p){
    return p && strlen(p)==3 && p[1]==':' && p[2]=='\\';
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
bool ListDirectory(const char* path,std::vector<Item>& out){
    out.clear();

    // For non-root, push ".." to allow going up.
    if(strlen(path)>3){
        Item up; ZeroMemory(&up,sizeof(up));
        strncpy(up.name,"..",3); up.isDir=true; up.size=0; up.isUpEntry=true; up.marked=false; out.push_back(up);
    }

    char base[512]; _snprintf(base,sizeof(base),"%s",path); base[sizeof(base)-1]=0; EnsureTrailingSlash(base,sizeof(base));
    char mask[512]; _snprintf(mask,sizeof(mask),"%s*",base); mask[sizeof(mask)-1]=0;

    WIN32_FIND_DATAA fd; ZeroMemory(&fd,sizeof(fd));
    HANDLE h=FindFirstFileA(mask,&fd); if(h==INVALID_HANDLE_VALUE) return false;
    do{
        const char* n=fd.cFileName; if(!strcmp(n,".")||!strcmp(n,"..")) continue;
        Item it; ZeroMemory(&it,sizeof(it));
        strncpy(it.name,n,255); it.name[255]=0;
        it.isDir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
        it.size=(((ULONGLONG)fd.nFileSizeHigh)<<32)|fd.nFileSizeLow; it.isUpEntry=false; it.marked=false;
        out.push_back(it);
    }while(FindNextFileA(h,&fd));
    FindClose(h);

    size_t start=(strlen(path)>3)?1:0; // keep ".." in place
    if(out.size()>start+1) std::sort(out.begin()+(int)start,out.end(),ItemLess);
    return true;
}

// ============================================================================
// Misc info helpers
// ============================================================================

void FormatSize(ULONGLONG bytes, char* out, size_t cap)
{
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

    // One decimal place (e.g., 1.5 GB / 12.0 TB). Tweak if you prefer.
    _snprintf(out, (int)cap, "%.2f %s", val, unit);
    out[cap-1] = 0;
}

void GetDriveFreeTotal(const char* anyPathInDrive, ULONGLONG& freeBytes, ULONGLONG& totalBytes){
    freeBytes=0; totalBytes=0; 
    char root[8]; _snprintf(root,sizeof(root),"%c:\\",anyPathInDrive[0]); root[sizeof(root)-1]=0;
    ULARGE_INTEGER a,t,f; a.QuadPart=0; t.QuadPart=0; f.QuadPart=0;
    if(GetDiskFreeSpaceExA(root,&a,&t,&f)){ freeBytes=f.QuadPart; totalBytes=t.QuadPart; }
}

// ============================================================================
// Basic FS ops
// ============================================================================

bool DirExistsA(const char* path){
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDirA(const char* path){
    DWORD a = GetFileAttributesA(path);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryA(path, NULL) ? true : false;
}

// Recursively delete files/directories with attribute clearing and safety rails:
//  - Rejects drive roots
//  - Rejects read-only volumes
bool DeleteRecursiveA(const char* path){
    if (!path || !path[0]) { SetLastError(ERROR_INVALID_PARAMETER); return false; }
    if (IsDriveRoot(path)) { SetLastError(ERROR_ACCESS_DENIED);     return false; }
    if (IsReadOnlyVolumeA(path)) { SetLastError(ERROR_WRITE_PROTECT); return false; }

    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) { SetLastError(ERROR_FILE_NOT_FOUND); return false; }

    // Make the target itself writable so final delete can succeed.
    StripROSysHiddenA(path);

    if (a & FILE_ATTRIBUTE_DIRECTORY){
        // Enumerate children
        char mask[512]; JoinPath(mask, sizeof(mask), path, "*");
        WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(mask, &fd);
        if (h != INVALID_HANDLE_VALUE){
            do{
                if (!strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..")) continue;

                char sub[512]; JoinPath(sub, sizeof(sub), path, fd.cFileName);

                // Clear attributes on each child before deleting
                StripROSysHiddenA(sub);

                // Best-effort delete; continue on failure
                if (!DeleteRecursiveA(sub)){
                    // optional: collect first error somewhere if desired
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }

        // Try removing the (now empty) directory (with a tiny retry)
        if (!RemoveDirectoryA(path)){
            Sleep(1);
            StripROSysHiddenA(path);
            return RemoveDirectoryA(path) ? true : false;
        }
        return true;
    }else{
        // File: clear attributes then delete (retry once)
        if (!DeleteFileA(path)){
            StripROSysHiddenA(path);
            return DeleteFileA(path) ? true : false;
        }
        return true;
    }
}

// ============================================================================
// File type helpers
// ============================================================================

bool HasXbeExt(const char* name){
    if (!name) return false;
    const char* dot = strrchr(name, '.');
    return (dot && _stricmp(dot, ".xbe") == 0);
}

// ============================================================================
// Copy (chunked) + recursive copy with progress/cancel
//  - CopyFileChunkedA: fixed 64 KiB buffer, attribute normalization
//  - CopyRecursiveCoreA: mkdirs and recurse
//  - CopyRecursiveWithProgressA: guards against copying into a subfolder of self
// ============================================================================

static bool CopyFileChunkedA(const char* s, const char* d,
                             ULONGLONG& inoutBytesDone, ULONGLONG totalBytes)
{
    // Open source (read-only, allow sharing with readers)
    HANDLE hs = CreateFileA(s, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hs == INVALID_HANDLE_VALUE) return false;

    // Preflight destination: normalize attributes if overwriting a file
    DWORD da = GetFileAttributesA(d);
    if (da != INVALID_FILE_ATTRIBUTES) {
        if (da & FILE_ATTRIBUTE_DIRECTORY) {
            // Name collision with existing directory
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
        if (g_copyProgFn){
            if (!g_copyProgFn(inoutBytesDone, totalBytes, s, g_copyProgUser)){
                ok = false; break; // canceled
            }
        }
    }

    LocalFree(buf);
    CloseHandle(hs);
    CloseHandle(hd);

    // Normalize destination attributes; on failure, remove partial.
	if (!ok) {
        DeleteFileA(d);
	} else {
		SetFileAttributesA(d, FILE_ATTRIBUTE_NORMAL);
	}
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
    if (IsSubPathCI(srcPath, dstTop)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Optional free-space preflight
    if (totalBytes > 0) {
        ULONGLONG freeB=0, totalB=0;
        GetDriveFreeTotal(dstDir, freeB, totalB);
        if (freeB > 0 && freeB < totalBytes) {
            SetLastError(ERROR_DISK_FULL);
            return false;
        }
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

// Quick write test: create + delete a small temp file in 'dir'.
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
// FATX-ish naming rules (very close to what dashboards/games expect)
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
//  - Accepts either a specific .xbe or a directory (launches default.xbe).
//  - Repoints D: to the folder's *device path* and calls XLaunchNewImageA.
// ============================================================================

// DOS "E:\Games\Foo" -> device "\Device\Harddisk0\Partition1\Games\Foo"
static bool DosToDevicePathA(const char* dos, char* out, size_t cap){
    if (!dos || strlen(dos) < 2 || dos[1] != ':') return false;
    char drive = (char)toupper((unsigned char)dos[0]);
    const char* tail = dos + 2;
    while (*tail == '\\') ++tail;

    const char* prefix = NULL;
    switch (drive){
        case 'C': prefix="\\Device\\Harddisk0\\Partition2"; break;
        case 'E': prefix="\\Device\\Harddisk0\\Partition1"; break;
        case 'X': prefix="\\Device\\Harddisk0\\Partition3"; break;
        case 'Y': prefix="\\Device\\Harddisk0\\Partition4"; break;
        case 'Z': prefix="\\Device\\Harddisk0\\Partition5"; break;
        case 'F': prefix="\\Device\\Harddisk0\\Partition6"; break;
        case 'G': prefix="\\Device\\Harddisk0\\Partition7"; break;
        case 'D': prefix="\\Device\\Cdrom0";                break;
        default: return false;
    }
    if (!*tail) _snprintf(out,(int)cap,"%s", prefix);
    else        _snprintf(out,(int)cap,"%s\\%s", prefix, tail);
    out[cap-1]=0;
    return true;
}

// Launch a .xbe or a folder's default.xbe by remapping D: and jumping to it.
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

    // Build device path for 'dir'
    char devPath[1024];
    if (!DosToDevicePathA(dir, devPath, sizeof(devPath))){
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Repoint D: to device path of 'dir'
    char dosD[16]; MakeDosString(dosD, sizeof(dosD), "D:");
    STRING sDos; BuildString(sDos, dosD);
    IoDeleteSymbolicLink(&sDos); // ignore result

    STRING sDev; BuildString(sDev, devPath);
    LONG st = IoCreateSymbolicLink(&sDos, &sDev);
    if (st != 0){
        // STATUS_SUCCESS == 0
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }

    // Launch D:\<file>
    char launchPath[512];
    _snprintf(launchPath, sizeof(launchPath), "D:\\%s", file);
    launchPath[sizeof(launchPath)-1]=0;

    DWORD rc = XLaunchNewImageA(launchPath, (PLAUNCH_DATA)NULL);
    if (rc == ERROR_SUCCESS) return true;

    SetLastError(rc);
    return false;
}
