#include "FsUtil.h"
#include <algorithm>
#include <string.h>
#include <ctype.h>
#include <stdio.h>  // _snprintf

// --- xboxkrnl drive link APIs ---
extern "C" {
    typedef struct _STRING { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } STRING, *PSTRING;
    LONG __stdcall IoCreateSymbolicLink(PSTRING SymbolicLinkName, PSTRING DeviceName);
    LONG __stdcall IoDeleteSymbolicLink(PSTRING SymbolicLinkName);
}

static inline void BuildString(STRING& s,const char* z){
    USHORT L=(USHORT)strlen(z); s.Length=L; s.MaximumLength=L+1; s.Buffer=(PCHAR)z;
}
static inline void MakeDosString(char* out,size_t cap,const char* letter){
    _snprintf(out,(int)cap,"\\??\\%s",letter); out[cap-1]=0;
}

#ifndef FILE_READ_ONLY_VOLUME
#define FILE_READ_ONLY_VOLUME 0x00080000u  // Win32 FILE_READ_ONLY_VOLUME
#endif

// ---- progress callback state ----
static CopyProgressFn g_copyProgFn = 0;
static void*          g_copyProgUser = 0;

void SetCopyProgressCallback(CopyProgressFn fn, void* user){
    g_copyProgFn   = fn;
    g_copyProgUser = user;
}

// --- helpers ---
static inline void StripROSysHiddenA(const char* path){
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return;
    DWORD na = a & ~(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN);
    if (na != a) SetFileAttributesA(path, na);
}

static bool IsReadOnlyVolumeA(const char* path){
    if (!path || !path[0]) return false;
    char root[4]; _snprintf(root, sizeof(root), "%c:\\", (char)toupper((unsigned char)path[0])); root[sizeof(root)-1]=0;
    DWORD fsFlags = 0;
    // If GetVolumeInformationA isn't available in your SDK, fall back to simple D:\ check.
    if (GetVolumeInformationA(root, NULL, 0, NULL, NULL, &fsFlags, NULL, 0))
        return (fsFlags & FILE_READ_ONLY_VOLUME) != 0;
    return (root[0] == 'D'); // OG Xbox DVD
}

static BOOL MapLetterToDevice(const char* letter,const char* devicePath){
    char dosBuf[16]={0}; MakeDosString(dosBuf,sizeof(dosBuf),letter);
    STRING sDos; BuildString(sDos,dosBuf); IoDeleteSymbolicLink(&sDos);
    STRING sDev; BuildString(sDev,devicePath);
    if (IoCreateSymbolicLink(&sDos,&sDev)!=0) return FALSE; // STATUS_SUCCESS==0
    char root[8]={0}; _snprintf(root,sizeof(root),"%s\\",letter);
    if (GetFileAttributesA(root)==INVALID_FILE_ATTRIBUTES){
        IoDeleteSymbolicLink(&sDos); return FALSE;
    }
    return TRUE;
}

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

// ---- present drive detection (private state) ----
namespace {
    const char* kRoots[] = { "C:\\", "D:\\", "E:\\", "F:\\", "G:\\", "X:\\", "Y:\\", "Z:\\" };
    const int   kNumRoots = sizeof(kRoots)/sizeof(kRoots[0]);
    int  g_presentIdx[16];
    int  g_presentCount = 0;

    inline int ci_cmp(const char* a,const char* b){ return _stricmp(a,b); }
}

void RescanDrives(){
    g_presentCount = 0;
    for (int i=0;i<kNumRoots && g_presentCount<(int)(sizeof(g_presentIdx)/sizeof(g_presentIdx[0])); ++i){
        DWORD a = GetFileAttributesA(kRoots[i]);
        if (a != INVALID_FILE_ATTRIBUTES) g_presentIdx[g_presentCount++] = i;
    }
}

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
    size_t n = strlen(s);
    if (n==2 && s[1]==':'){ s[2]='\\'; s[3]=0; return; }
    EnsureTrailingSlash(s, 512);
}

static bool ItemLess(const Item& a,const Item& b){
    if(a.isDir!=b.isDir) return a.isDir>b.isDir;
    return ci_cmp(a.name,b.name)<0;
}

bool ListDirectory(const char* path,std::vector<Item>& out){
    out.clear();
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

    size_t start=(strlen(path)>3)?1:0;
    if(out.size()>start+1) std::sort(out.begin()+(int)start,out.end(),ItemLess);
    return true;
}

// ---- misc helpers ----
void FormatSize(ULONGLONG sz, char* out, size_t cap){
    const char* unit="B"; double v=(double)sz;
    if(sz>=(1ULL<<30)){ v/=(double)(1ULL<<30); unit="GB"; }
    else if(sz>=(1ULL<<20)){ v/=(double)(1ULL<<20); unit="MB"; }
    else if(sz>=(1ULL<<10)){ v/=(double)(1ULL<<10); unit="KB"; }
    _snprintf(out,(int)cap,(unit[0]=='B')?"%.0f %s":"%.1f %s",v,unit); out[cap-1]=0;
}

void GetDriveFreeTotal(const char* anyPathInDrive, ULONGLONG& freeBytes, ULONGLONG& totalBytes){
    freeBytes=0; totalBytes=0; 
    char root[8]; _snprintf(root,sizeof(root),"%c:\\",anyPathInDrive[0]); root[sizeof(root)-1]=0;
    ULARGE_INTEGER a,t,f; a.QuadPart=0; t.QuadPart=0; f.QuadPart=0;
    if(GetDiskFreeSpaceExA(root,&a,&t,&f)){ freeBytes=f.QuadPart; totalBytes=t.QuadPart; }
}

// ---- fs ops ----
bool DirExistsA(const char* path){
    DWORD a = GetFileAttributesA(path);
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool EnsureDirA(const char* path){
    DWORD a = GetFileAttributesA(path);
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryA(path, NULL) ? true : false;
}

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

                // Best-effort delete; if any fail, we still keep trying others
                if (!DeleteRecursiveA(sub)){
                    // optional: record the first error somewhere if you want detailed reporting
                }
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }

        // Try removing the (now hopefully empty) directory
        // Some filesystems lag; 1 quick retry helps.
        if (!RemoveDirectoryA(path)){
            Sleep(1);
            StripROSysHiddenA(path); // in case attributes flipped back
            return RemoveDirectoryA(path) ? true : false;
        }
        return true;
    }else{
        // File: clear attributes then delete
        if (!DeleteFileA(path)){
            // one retry after ensuring attributes
            StripROSysHiddenA(path);
            return DeleteFileA(path) ? true : false;
        }
        return true;
    }
}

// Xbe launching
bool HasXbeExt(const char* name){
    if (!name) return false;
    const char* dot = strrchr(name, '.');
    return (dot && _stricmp(dot, ".xbe") == 0);
}
// ====== Progress copy implementation ======

static bool CopyFileChunkedA(const char* s, const char* d,
                             ULONGLONG& inoutBytesDone, ULONGLONG totalBytes)
{
    HANDLE hs = CreateFileA(s, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, NULL);
    if (hs == INVALID_HANDLE_VALUE) return false;

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

        // tell UI (and give it a chance to cancel)
        if (g_copyProgFn){
            if (!g_copyProgFn(inoutBytesDone, totalBytes, s, g_copyProgUser)){
                ok = false; break;
            }
        }
    }

    LocalFree(buf);
    CloseHandle(hs);
    CloseHandle(hd);
    return ok;
}

static bool CopyRecursiveCoreA(const char* srcPath, const char* dstDir,
                               ULONGLONG& inoutBytesDone, ULONGLONG totalBytes)
{
    DWORD a = GetFileAttributesA(srcPath);
    if (a == INVALID_FILE_ATTRIBUTES) return false;

    const char* base = strrchr(srcPath, '\\'); base = base ? base+1 : srcPath;
    char dstPath[512]; JoinPath(dstPath, sizeof(dstPath), dstDir, base);

    if (a & FILE_ATTRIBUTE_DIRECTORY){
        if (!EnsureDirA(dstPath)) return false;

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

// --- subfolder guard helpers ---
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

bool CopyRecursiveWithProgressA(const char* srcPath, const char* dstDir,
                                ULONGLONG totalBytes)
{
    // Build the top-level destination (dstDir + basename(srcPath))
    const char* base = strrchr(srcPath, '\\'); base = base ? base+1 : srcPath;
    char dstTop[512]; JoinPath(dstTop, sizeof(dstTop), dstDir, base);

    // Guard: prevent copying a folder into itself or its subfolders
    if (IsSubPathCI(srcPath, dstTop)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    // Optional: free-space sanity check (skip if totalBytes==0 or unknown)
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

// ---- size calc ----
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

// ---- FATX-ish name rules ----
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

// --- private: DOS "E:\Games\Foo" -> device path "\Device\Harddisk0\Partition1\Games\Foo"
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

// --- public: launch .xbe (path to .xbe OR folder containing default.xbe)
bool LaunchXbeA(const char* pathOrDir)
{
    if (!pathOrDir || !pathOrDir[0]){ SetLastError(ERROR_INVALID_PARAMETER); return false; }

    // Decide directory to mount + file to launch
    char dir[512]; dir[0]=0;
    char file[256]; file[0]=0;

    if (HasXbeExt(pathOrDir)){
        // path is "...\<name>.xbe"
        const char* slash = strrchr(pathOrDir, '\\');
        _snprintf(file, sizeof(file), "%s", slash ? slash+1 : pathOrDir);
        file[sizeof(file)-1]=0;

        _snprintf(dir, sizeof(dir), "%s", pathOrDir); dir[sizeof(dir)-1]=0;
        ParentPath(dir);
        EnsureTrailingSlash(dir, sizeof(dir));
    } else {
        // path is a folder: launch default.xbe from there
        _snprintf(dir, sizeof(dir), "%s", pathOrDir); dir[sizeof(dir)-1]=0;
        EnsureTrailingSlash(dir, sizeof(dir));
        _snprintf(file, sizeof(file), "default.xbe");
    }

    // Optional pre-check: does <dir>\<file> exist?
    char pre[512]; JoinPath(pre, sizeof(pre), dir, file);
    if (GetFileAttributesA(pre) == INVALID_FILE_ATTRIBUTES){
        SetLastError(ERROR_FILE_NOT_FOUND);
        return false;
    }

    // Repoint D: to that folder’s device path
    char devPath[1024];
    if (!DosToDevicePathA(dir, devPath, sizeof(devPath))){
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    char dosD[16]; MakeDosString(dosD, sizeof(dosD), "D:");
    STRING sDos; BuildString(sDos, dosD);
    IoDeleteSymbolicLink(&sDos); // ignore result

    STRING sDev; BuildString(sDev, devPath);
    LONG st = IoCreateSymbolicLink(&sDos, &sDev);
    if (st != 0){
        // kernel returns STATUS_SUCCESS == 0
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }

    // Launch "D:\<file>"
    char launchPath[512];
    _snprintf(launchPath, sizeof(launchPath), "D:\\%s", file);
    launchPath[sizeof(launchPath)-1]=0;

    DWORD rc = XLaunchNewImageA(launchPath, (PLAUNCH_DATA)NULL);
    if (rc == ERROR_SUCCESS) return true;

    // On failure, leave LastError for the caller; your UI can show rc if desired
    SetLastError(rc);
    return false;
}

