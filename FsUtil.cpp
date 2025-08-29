#include "FsUtil.h"
#include <wchar.h>
#include <algorithm>
#include <string.h>

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
        it.isDir=true; it.size=0; it.isUpEntry=false;
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
        strncpy(up.name,"..",3); up.isDir=true; up.size=0; up.isUpEntry=true; out.push_back(up);
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
        it.size=(((ULONGLONG)fd.nFileSizeHigh)<<32)|fd.nFileSizeLow; it.isUpEntry=false;
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

bool CopyFileSimpleA(const char* s, const char* d){
    return CopyFileA(s, d, FALSE) ? true : false;
}

bool DeleteRecursiveA(const char* path){
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES) return false;
    if (a & FILE_ATTRIBUTE_DIRECTORY){
        char mask[512]; JoinPath(mask, sizeof(mask), path, "*");
        WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA(mask,&fd);
        if (h != INVALID_HANDLE_VALUE){
            do{
                if (!strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..")) continue;
                char sub[512]; JoinPath(sub, sizeof(sub), path, fd.cFileName);
                DeleteRecursiveA(sub);
            }while(FindNextFileA(h,&fd));
            FindClose(h);
        }
        return RemoveDirectoryA(path) ? true : false;
    }else{
        return DeleteFileA(path) ? true : false;
    }
}

bool CopyRecursiveA(const char* srcPath, const char* dstDir){
    DWORD a = GetFileAttributesA(srcPath);
    if (a == INVALID_FILE_ATTRIBUTES) return false;

    const char* base = strrchr(srcPath, '\\');
    base = base ? base+1 : srcPath;

    char dstPath[512]; JoinPath(dstPath, sizeof(dstPath), dstDir, base);

    if (a & FILE_ATTRIBUTE_DIRECTORY){
        if (!EnsureDirA(dstPath)) return false;
        char mask[512];    JoinPath(mask,    sizeof(mask),    srcPath, "*");
        WIN32_FIND_DATAA fd; HANDLE h=FindFirstFileA(mask, &fd);
        if (h != INVALID_HANDLE_VALUE){
            do{
                if (!strcmp(fd.cFileName,".") || !strcmp(fd.cFileName,"..")) continue;
                char subSrc[512];  JoinPath(subSrc,  sizeof(subSrc),  srcPath, fd.cFileName);
                CopyRecursiveA(subSrc, dstPath);
            }while(FindNextFileA(h,&fd));
            FindClose(h);
        }
        return true;
    }else{
        return CopyFileSimpleA(srcPath, dstPath);
    }
}

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
