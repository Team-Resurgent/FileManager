//------------------------------------------------------------------------------
// OG Xbox File Browser (Original Xbox XDK) — VS2003 friendly
// Dual-pane GUI with auto layout, context menu, basic file ops, and rename OSD
//------------------------------------------------------------------------------

#include <intrin0.inl.h>
#include <xtl.h>
#include <vector>
#include <algorithm>
#include "XBApp.h"
#include "XBFont.h"
#include "XBInput.h"

extern "C" 
{
	typedef struct _STRING { USHORT Length; USHORT MaximumLength; PCHAR Buffer; } STRING, *PSTRING;
	LONG __stdcall IoCreateSymbolicLink(PSTRING SymbolicLinkName, PSTRING DeviceName);
	LONG __stdcall IoDeleteSymbolicLink(PSTRING SymbolicLinkName);
	EXCEPTION_DISPOSITION __cdecl _except_handler4(_EXCEPTION_RECORD* ExceptionRecord, void* EstablisherFrame, _CONTEXT* ContextRecord, PVOID DispatcherContext)
	{
		return ExceptionContinueExecution;
	}
	__declspec(naked) long _ftol2_sse(void) {
		__asm {
			sub     esp, 8
			fistp   qword ptr[esp]
			mov     eax, [esp]
			add     esp, 8
			ret
		}
	}
}

//#pragma comment(lib, "xboxkrnl.lib")

#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFF
#endif

// --- Simple on-screen keyboard layout (fixed grid, VS2003-safe) ---
static const char s_kb_r0[] = "ABCDEFGHIJKL";     // 12
static const char s_kb_r1[] = "MNOPQRSTUVWX";     // 12
static const char s_kb_r2[] = "YZ0123456789";     // 12
static const char s_kb_r3[] = "-_.()[]{}+&";      // 12 (punctuation row)
static const int  s_kb_cols[5] = {12,12,12,12,5}; // last row: Back, Space, Aa, OK, Cancel

static inline FLOAT Snap(FLOAT v){ return (FLOAT)((int)(v + 0.5f)); } // integer pixel snap

static const char* kRoots[] = { "C:\\", "D:\\", "E:\\", "F:\\", "G:\\", "X:\\", "Y:\\", "Z:\\" };
static const int   kNumRoots = sizeof(kRoots)/sizeof(kRoots[0]);
static int  g_presentIdx[16];
static int  g_presentCount = 0;

// -------------------- text / size / path helpers --------------------
static inline void DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
	WCHAR wbuf[512]; MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512); font.DrawText(x,y,color,wbuf,0,0.0f);
}
static inline void FormatSize(ULONGLONG sz, char* out, size_t cap){
	const char* unit="B"; double v=(double)sz;
	if(sz>=(1ULL<<30)){ v/=(double)(1ULL<<30); unit="GB"; }
	else if(sz>=(1ULL<<20)){ v/=(double)(1ULL<<20); unit="MB"; }
	else if(sz>=(1ULL<<10)){ v/=(double)(1ULL<<10); unit="KB"; }
	_snprintf(out,(int)cap,(unit[0]=='B')?"%.0f %s":"%.1f %s",v,unit); out[cap-1]=0;
}
static inline void GetDriveFreeTotal(const char* anyPathInDrive, ULONGLONG& freeBytes, ULONGLONG& totalBytes){
	freeBytes=0; totalBytes=0; char root[8]; _snprintf(root,sizeof(root),"%c:\\",anyPathInDrive[0]); root[sizeof(root)-1]=0;
	ULARGE_INTEGER a,t,f; a.QuadPart=0; t.QuadPart=0; f.QuadPart=0;
	if(GetDiskFreeSpaceExA(root,&a,&t,&f)){ freeBytes=f.QuadPart; totalBytes=t.QuadPart; }
}
static inline void BuildString(STRING& s,const char* z){ USHORT L=(USHORT)strlen(z); s.Length=L; s.MaximumLength=L+1; s.Buffer=(PCHAR)z; }
static inline void MakeDosString(char* out,size_t cap,const char* letter){ _snprintf(out,(int)cap,"\\??\\%s",letter); out[cap-1]=0; }
static inline void JoinPath(char* dst,size_t cap,const char* base,const char* name){
	size_t bl=strlen(base);
	if(bl&&base[bl-1]=='\\') _snprintf(dst,(int)cap,"%s%s",base,name);
	else                      _snprintf(dst,(int)cap,"%s\\%s",base,name);
	dst[cap-1]=0;
}
static inline void EnsureTrailingSlash(char* s,size_t cap){ size_t n=strlen(s); if(n&&s[n-1]!='\\'&&n+1<cap){ s[n]='\\'; s[n+1]=0; } }
static inline void ParentPath(char* path){
	size_t n=strlen(path);
	if (n <= 3) { path[0]=0; return; }
	while (n && path[n-1]=='\\') { path[--n]=0; }
	char* p = strrchr(path,'\\');
	if (!p) { path[0]=0; return; }
	if (p == path+2) *(p+1)=0; else *p=0;
}
static inline int ci_cmp(const char* a,const char* b){ return _stricmp(a,b); }

// -------------------- letter mapping --------------------
static BOOL MapLetterToDevice(const char* letter,const char* devicePath){
	char dosBuf[16]={0}; MakeDosString(dosBuf,sizeof(dosBuf),letter);
	STRING sDos; BuildString(sDos,dosBuf); IoDeleteSymbolicLink(&sDos);
	STRING sDev; BuildString(sDev,devicePath);
	if(IoCreateSymbolicLink(&sDos,&sDev)!=0) return FALSE; // STATUS_SUCCESS==0
	char root[8]={0}; _snprintf(root,sizeof(root),"%s\\",letter);
	if(GetFileAttributesA(root)==INVALID_FILE_ATTRIBUTES){ IoDeleteSymbolicLink(&sDos); return FALSE; }
	return TRUE;
}
static void MapStandardDrives_Io(){
	MapLetterToDevice("D:","\\Device\\Cdrom0");
	MapLetterToDevice("C:","\\Device\\Harddisk0\\Partition2");
	MapLetterToDevice("E:","\\Device\\Harddisk0\\Partition1");
	MapLetterToDevice("X:","\\Device\\Harddisk0\\Partition3");
	MapLetterToDevice("Y:","\\Device\\Harddisk0\\Partition4");
	MapLetterToDevice("Z:","\\Device\\Harddisk0\\Partition5");
	MapLetterToDevice("F:","\\Device\\Harddisk0\\Partition6");
	MapLetterToDevice("G:","\\Device\\Harddisk0\\Partition7");
}

// -------------------- listing --------------------
struct Item { char name[256]; bool isDir; ULONGLONG size; bool isUpEntry; };

static void BuildDriveItems(std::vector<Item>& out){
	out.clear(); for(int j=0;j<g_presentCount;++j){ int i=g_presentIdx[j]; Item it; ZeroMemory(&it,sizeof(it));
	strncpy(it.name,kRoots[i],255); it.name[255]=0; it.isDir=true; it.size=0; it.isUpEntry=false; out.push_back(it); }
}
static void RescanDrives(){ g_presentCount=0; for(int i=0;i<kNumRoots && g_presentCount<(int)(sizeof(g_presentIdx)/sizeof(g_presentIdx[0])); ++i){ DWORD a=GetFileAttributesA(kRoots[i]); if(a!=INVALID_FILE_ATTRIBUTES) g_presentIdx[g_presentCount++]=i; } }
static bool ItemLess(const Item& a,const Item& b){ if(a.isDir!=b.isDir) return a.isDir>b.isDir; return ci_cmp(a.name,b.name)<0; }
static bool ListDirectory(const char* path,std::vector<Item>& out){
	out.clear();
	if(strlen(path)>3){ Item up; ZeroMemory(&up,sizeof(up)); strncpy(up.name,"..",3); up.isDir=true; up.size=0; up.isUpEntry=true; out.push_back(up); }
	char base[512]; _snprintf(base,sizeof(base),"%s",path); base[sizeof(base)-1]=0; EnsureTrailingSlash(base,sizeof(base));
	char mask[512]; _snprintf(mask,sizeof(mask),"%s*",base); mask[sizeof(mask)-1]=0;
	WIN32_FIND_DATAA fd; ZeroMemory(&fd,sizeof(fd)); HANDLE h=FindFirstFileA(mask,&fd); if(h==INVALID_HANDLE_VALUE) return false;
	do{ const char* n=fd.cFileName; if(!strcmp(n,".")||!strcmp(n,"..")) continue; Item it; ZeroMemory(&it,sizeof(it));
	strncpy(it.name,n,255); it.name[255]=0; it.isDir=(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
	it.size=(((ULONGLONG)fd.nFileSizeHigh)<<32)|fd.nFileSizeLow; it.isUpEntry=false; out.push_back(it);
	}while(FindNextFileA(h,&fd)); FindClose(h);
	size_t start=(strlen(path)>3)?1:0; if(out.size()>start+1) std::sort(out.begin()+(int)start,out.end(),ItemLess);
	return true;
}

static bool IsDriveRoot(const char* p){
	return p && strlen(p)==3 && p[1]==':' && p[2]=='\\';
}

// -------------------- App (dual-pane + menu) --------------------
class FileBrowserApp : public CXBApplication {
public:
	// pane state
	struct Pane {
		std::vector<Item> items;
		char  curPath[512];
		int   mode;     // 0=drives, 1=dir
		int   sel;
		int   scroll;
		Pane(){ curPath[0]=0; mode=0; sel=0; scroll=0; }
	};

	CXBFont m_font;
	int     m_visible;              // rows
	unsigned char m_prevA, m_prevB, m_prevX, m_prevY;
	DWORD        m_prevButtons;          // last frame's digital buttons
	unsigned char m_prevWhite, m_prevBlack; // last frame's white/black
	Pane    m_pane[2];
	int     m_active;               // 0=left, 1=right
	bool  m_navUDHeld;      // up/down currently held?
	int   m_navUDDir;       // -1 up, +1 down
	DWORD m_navUDNext;      // next time (ms) another step is allowed

	// TL vertex for filled rects
	struct TLVERT { float x,y,z,rhw; D3DCOLOR color; };
	enum { FVF_TLVERT = D3DFVF_XYZRHW | D3DFVF_DIFFUSE };

	// Layout (left pane base + right pane offset) — computed at runtime
	static FLOAT kListX_L, kListY, kListW, kLineH;
	static FLOAT kHdrX_L,  kHdrY,  kHdrW,  kHdrH;
	static FLOAT kGutterW, kPaddingX, kScrollBarW;
	static FLOAT kPaneGap;   // space between panes

	// --- Context menu/state ---
	enum Action {
		ACT_OPEN, ACT_COPY, ACT_MOVE, ACT_DELETE, ACT_RENAME,
		ACT_MKDIR, ACT_CALCSIZE, ACT_GOROOT, ACT_SWITCHMEDIA
	};
	struct MenuItem { const char* label; Action act; bool enabled; };

	bool        m_menuOpen;
	int         m_menuSel;
	MenuItem    m_menu[9];
	int         m_menuCount;

	enum { MODE_BROWSE, MODE_MENU, MODE_RENAME } m_mode;

	// footer status
	char  m_status[256];
	DWORD m_statusUntilMs;

	// --- Rename UI state ---
	bool  m_renActive;
	char  m_renParent[512];
	char  m_renOld[256];
	char  m_renBuf[256];
	int   m_renCursor;   // insertion cursor in m_renBuf
	int   m_renSelRow;   // keyboard selection
	int   m_renSelCol;
	bool  m_kbLower;     // case mode

	FileBrowserApp(){
		m_visible=13; m_prevA=0; m_prevB=0; m_prevX=0; m_prevY=0; m_active=0;
		m_prevButtons = 0;
		m_prevWhite = m_prevBlack = 0;
		m_navUDHeld = false;
		m_navUDDir  = 0;
		m_navUDNext = 0;
		ZeroMemory(&m_d3dpp,sizeof(m_d3dpp));
		m_d3dpp.BackBufferWidth=1280; m_d3dpp.BackBufferHeight=720;
		m_d3dpp.BackBufferFormat=D3DFMT_X8R8G8B8; m_d3dpp.SwapEffect=D3DSWAPEFFECT_DISCARD;
		m_d3dpp.FullScreen_RefreshRateInHz=60; m_d3dpp.EnableAutoDepthStencil=TRUE; m_d3dpp.AutoDepthStencilFormat=D3DFMT_D24S8;
		m_d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;

		m_menuOpen=false; m_menuSel=0; m_menuCount=0; m_mode=MODE_BROWSE;
		m_status[0]=0; m_statusUntilMs=0;

		m_renActive=false; m_renParent[0]=0; m_renOld[0]=0; m_renBuf[0]=0;
		m_renCursor=0; m_renSelRow=0; m_renSelCol=0; m_kbLower=false;
	}

	// --- draw prims
	void DrawRect(float x,float y,float w,float h,D3DCOLOR c){
		TLVERT v[4]; v[0].x=x; v[0].y=y; v[1].x=x+w; v[1].y=y; v[2].x=x; v[2].y=y+h; v[3].x=x+w; v[3].y=y+h;
		for(int i=0;i<4;i++){ v[i].z=0.0f; v[i].rhw=1.0f; v[i].color=c; }
		m_pd3dDevice->SetTexture(0,NULL);
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
		m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
		m_pd3dDevice->SetVertexShader(FVF_TLVERT);
		m_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(TLVERT));
	}
	void DrawHLine(float x,float y,float w,D3DCOLOR c){ DrawRect(x,y,w,1.0f,c); }
	void DrawVLine(float x,float y,float h,D3DCOLOR c){ DrawRect(x,y,1.0f,h,c); }

	// --- layout helpers/columns
	static FLOAT NameColX(FLOAT baseX){ return baseX + kGutterW + kPaddingX; }
	static FLOAT HdrX   (FLOAT baseX){ return baseX - 15.0f; }

	// --- text measurement + right-align
	FLOAT MeasureTextW(const char* s){
		WCHAR wbuf[256]; 
		MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 256);
		FLOAT w=0.0f, h=0.0f;
		m_font.GetTextExtent(wbuf, &w, &h);
		return w;
	}
	// Returns both width & height for centering
	void MeasureTextWH(const char* s, FLOAT& outW, FLOAT& outH){
		WCHAR wbuf[256];
		MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 256);
		m_font.GetTextExtent(wbuf, &outW, &outH);
	}

	void DrawRightAligned(const char* s, FLOAT rightX, FLOAT y, DWORD color){
		FLOAT w = MeasureTextW(s);
		DrawAnsi(m_font, rightX - w, y, color, s);
	}

	// --- size column measurement (dynamic per pane)
	FLOAT ComputeSizeColW(const Pane& p){
		FLOAT maxW = MeasureTextW("Size");
		int limit = (int)p.items.size(); if(limit>200) limit=200;
		char buf[64];
		for(int i=0;i<limit;++i){
			const Item& it = p.items[i];
			if(!it.isDir && !it.isUpEntry){
				FormatSize(it.size, buf, sizeof(buf));
				FLOAT w = MeasureTextW(buf); if(w>maxW) maxW=w;
			}
		}
		maxW += 16.0f; // padding
		const FLOAT minW = max(90.0f, kLineH * 4.0f);
		const FLOAT maxWClamp = kListW * 0.40f;
		if(maxW < minW) maxW = minW;
		if(maxW > maxWClamp) maxW = maxWClamp;
		return maxW;
	}

	// --- status
	void SetStatus(const char* fmt, ...){
		va_list ap; va_start(ap, fmt);
		_vsnprintf(m_status, sizeof(m_status), fmt, ap);
		va_end(ap);
		m_status[sizeof(m_status)-1]=0;
		m_statusUntilMs = GetTickCount() + 3000; // 3s
	}
	void SetStatusLastErr(const char* prefix){
		DWORD e = GetLastError();
		char msg[64]; _snprintf(msg, sizeof(msg), "%s (err=%lu)", prefix, (unsigned long)e);
		msg[sizeof(msg)-1]=0;
		SetStatus("%s", msg);
	}

	// Return true if we can create & delete a temp file in 'dir'
	bool CanWriteHereA(const char* dir){
		char test[512];
		JoinPath(test, sizeof(test), dir, ".__xwtest$__");
		test[sizeof(test)-1]=0;
		HANDLE h = CreateFileA(test, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
		if (h == INVALID_HANDLE_VALUE) return false;
		CloseHandle(h);
		DeleteFileA(test);
		return true;
	}

	// normalize dir for things like "E:" -> "E:\"
	void NormalizeDirA(char* s){
		size_t n = strlen(s);
		if (n==2 && s[1]==':'){ s[2]='\\'; s[3]=0; return; }
		EnsureTrailingSlash(s, 512);
	}

	// FATX-ish name rules: disallow control chars and obvious bad ones
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

	// pick char at (row,col) for rows 0-3 of keyboard (respects case)
	char KbCharAt(int row, int col){
		char ch = 0;
		if (row==0) ch = s_kb_r0[col];
		else if (row==1) ch = s_kb_r1[col];
		else if (row==2) ch = s_kb_r2[col];
		else if (row==3) ch = s_kb_r3[col];
		if (m_kbLower && (ch>='A' && ch<='Z')) ch = (char)(ch + ('a' - 'A'));
		return ch;
	}

	void RefreshPane(Pane& p){
		if (p.mode==1){
			int prevSel   = p.sel;
			int prevScroll= p.scroll;

			ListDirectory(p.curPath, p.items);

			if (prevSel >= (int)p.items.size()) prevSel = (int)p.items.size()-1;
			if (prevSel < 0) prevSel = 0;
			p.sel = prevSel;

			int maxScroll = max(0, (int)p.items.size() - m_visible);
			if (prevScroll > maxScroll) prevScroll = maxScroll;
			if (prevScroll < 0) prevScroll = 0;
			p.scroll = prevScroll;
		} else {
			// drives list
			BuildDriveItems(p.items);
			if (p.sel >= (int)p.items.size()) p.sel = (int)p.items.size()-1;
			if (p.sel < 0) p.sel = 0;
			p.scroll = 0;
		}
	}

	bool ResolveDestDir(char* outDst, size_t cap){
		Pane& dst = m_pane[1 - m_active];
		outDst[0] = 0;

		if (dst.mode == 1) {
			_snprintf(outDst, (int)cap, "%s", dst.curPath);
			outDst[cap-1]=0;
			NormalizeDirA(outDst);
			return true;
		}
		if (!dst.items.empty()){
			const Item& di = dst.items[dst.sel];
			if (di.isDir && !di.isUpEntry){
				_snprintf(outDst, (int)cap, "%s", di.name);  // e.g. "E:\"
				outDst[cap-1]=0;
				NormalizeDirA(outDst);
				return true;
			}
		}
		return false;
	}

	// --- file ops helpers
	bool EnsureDirA(const char* path){
		DWORD a = GetFileAttributesA(path);
		if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) return true;
		return CreateDirectoryA(path, NULL) ? true : false;
	}
	bool CopyFileSimpleA(const char* s, const char* d){ return CopyFileA(s, d, FALSE) ? true : false; }
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

	void AddMenuItem(const char* label, Action act, bool enabled){
		m_menu[m_menuCount].label   = label;
		m_menu[m_menuCount].act     = act;
		m_menu[m_menuCount].enabled = enabled;
		++m_menuCount;
	}

	bool DirExistsA(const char* path){
		DWORD a = GetFileAttributesA(path);
		return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
	}

	void SelectItemInPane(Pane& p, const char* name){
		if (!name || p.items.empty()) return;
		for (int i=0;i<(int)p.items.size();++i){
			if (_stricmp(p.items[i].name, name) == 0){
				p.sel = i;
				if (p.sel < p.scroll) p.scroll = p.sel;
				if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1);
				return;
			}
		}
	}

	// --- context menu
	void BuildContextMenu(){
		Pane& p   = m_pane[m_active];
		bool inDir  = (p.mode == 1);
		bool hasSel = !p.items.empty();

		m_menuCount = 0;

		if (inDir) AddMenuItem("Open",            ACT_OPEN,       hasSel);
		AddMenuItem("Copy",            ACT_COPY,       hasSel);
		AddMenuItem("Move",            ACT_MOVE,       hasSel);
		AddMenuItem("Delete",          ACT_DELETE,     hasSel);
		AddMenuItem("Rename",          ACT_RENAME,     hasSel);
		AddMenuItem("Make new folder", ACT_MKDIR,      inDir);
		AddMenuItem("Calculate size",  ACT_CALCSIZE,   hasSel);
		AddMenuItem("Go to root",      ACT_GOROOT,     inDir);
		AddMenuItem("Switch pane",     ACT_SWITCHMEDIA,true);

		if (m_menuSel >= m_menuCount) m_menuSel = m_menuCount-1;
		if (m_menuSel < 0)            m_menuSel = 0;
	}
	void OpenMenu(){ BuildContextMenu(); m_menuOpen=true; m_mode=MODE_MENU; }

	// >>> CHANGED: only switch to BROWSE if we were actually in MENU
	void CloseMenu(){
		m_menuOpen = false;
		if (m_mode == MODE_MENU)
			m_mode = MODE_BROWSE;
	}

	// --- Rename UI lifecycle
	void BeginRename(const char* parentDir, const char* oldName){
		// ensure any context menu is hidden, but do NOT stomp the new mode
		m_menuOpen = false;

		m_renActive = true;
		_snprintf(m_renParent, sizeof(m_renParent), "%s", parentDir);
		m_renParent[sizeof(m_renParent)-1]=0;
		_snprintf(m_renOld, sizeof(m_renOld), "%s", oldName);
		m_renOld[sizeof(m_renOld)-1]=0;
		_snprintf(m_renBuf, sizeof(m_renBuf), "%s", oldName);
		m_renBuf[sizeof(m_renBuf)-1]=0;
		m_renCursor = (int)strlen(m_renBuf);
		m_renSelRow = 0; m_renSelCol = 0;
		m_mode = MODE_RENAME;
	}
	void CancelRename(){
		m_renActive = false;
		m_mode = MODE_BROWSE;
	}
	void AcceptRename(){
		SanitizeFatxNameInPlace(m_renBuf);
		if (_stricmp(m_renBuf, m_renOld)==0){ SetStatus("No change"); CancelRename(); return; }

		char oldPath[512]; JoinPath(oldPath, sizeof(oldPath), m_renParent, m_renOld);
		char newPath[512]; JoinPath(newPath, sizeof(newPath), m_renParent, m_renBuf);

		if (MoveFileA(oldPath, newPath)){
			SetStatus("Renamed to %s", m_renBuf);
			RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
			Pane& ap = m_pane[m_active];
			if (ap.mode==1 && _stricmp(ap.curPath, m_renParent)==0){
				SelectItemInPane(ap, m_renBuf);
			}
		}else{
			SetStatusLastErr("Rename failed");
			RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
		}
		CancelRename();
	}

	void OnPad_Rename(const XBGAMEPAD& pad)
	{
		const DWORD btn = pad.wButtons;

		// Edge-detected digital buttons + allow quick analog nudge
		bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)    && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))    || (pad.sThumbLY >  16000);
		bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN))  || (pad.sThumbLY < -16000);
		bool left  = ((btn & XINPUT_GAMEPAD_DPAD_LEFT)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_LEFT))  || (pad.sThumbLX < -16000);
		bool right = ((btn & XINPUT_GAMEPAD_DPAD_RIGHT) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) || (pad.sThumbLX >  16000);

		if (up)   { if (m_renSelRow > 0) m_renSelRow--; Sleep(120); }
		if (down) { if (m_renSelRow < 4) m_renSelRow++; Sleep(120); }

		int cols = s_kb_cols[m_renSelRow];
		if (m_renSelCol >= cols) m_renSelCol = cols - 1;

		if (left)  { if (m_renSelCol > 0)        m_renSelCol--; Sleep(120); }
		if (right) { if (m_renSelCol < cols - 1) m_renSelCol++; Sleep(120); }

		// Edge-detect face buttons
		unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
		unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
		unsigned char y = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
		unsigned char w = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
		unsigned char k = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];

		bool aTrig = (a > 30 && m_prevA <= 30);
		bool bTrig = (b > 30 && m_prevB <= 30);
		bool yTrig = (y > 30 && m_prevY <= 30);
		bool startTrig = ((btn & XINPUT_GAMEPAD_START) && !(m_prevButtons & XINPUT_GAMEPAD_START));

		// Toggle case with Y
		if (yTrig){ m_kbLower = !m_kbLower; Sleep(120); }

		if (aTrig){
			if (m_renSelRow <= 3){
				// Insert selected char at cursor
				int len = (int)strlen(m_renBuf);
				const int cap = (int)sizeof(m_renBuf) - 1;
				if (len < cap){
					char ch = KbCharAt(m_renSelRow, m_renSelCol);
					for (int i=len; i>=m_renCursor; --i) m_renBuf[i+1] = m_renBuf[i];
					m_renBuf[m_renCursor++] = ch;
				}
			}else{
				// Bottom-row actions: 0=Backspace 1=Space 2=Aa 3=OK 4=Cancel
				if (m_renSelCol == 0){ // Backspace
					if (m_renCursor > 0){
						int len = (int)strlen(m_renBuf);
						for (int i=m_renCursor-1; i<=len; ++i) m_renBuf[i] = m_renBuf[i+1];
						m_renCursor--;
					}
				}else if (m_renSelCol == 1){ // Space
					int len = (int)strlen(m_renBuf);
					const int cap = (int)sizeof(m_renBuf) - 1;
					if (len < cap){
						for (int i=len; i>=m_renCursor; --i) m_renBuf[i+1] = m_renBuf[i];
						m_renBuf[m_renCursor++] = ' ';
					}
				}else if (m_renSelCol == 2){ // Aa toggle
					m_kbLower = !m_kbLower;
				}else if (m_renSelCol == 3){ // OK
					AcceptRename();
					m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
					return;
				}else if (m_renSelCol == 4){ // Cancel
					CancelRename();
					m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
					return;
				}
			}
			Sleep(140);
		}

		if (startTrig){
			AcceptRename();
			m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
			return;
		}
		if (bTrig){
			CancelRename();
			m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
			return;
		}

		// Move caret with White/Black (edge-detected)
		bool wTrig = (w > 30 && m_prevWhite <= 30);
		bool kTrig = (k > 30 && m_prevBlack <= 30);

		if (wTrig){ if (m_renCursor > 0) m_renCursor--; }
		if (kTrig){ int len=(int)strlen(m_renBuf); if (m_renCursor < len) m_renCursor++; }

		// Clamp caret
		{
			int len=(int)strlen(m_renBuf);
			if (m_renCursor < 0)   m_renCursor = 0;
			if (m_renCursor > len) m_renCursor = len;
		}

		// Save previous for edge detection next frame
		m_prevA = a; 
		m_prevB = b; 
		m_prevY = y;
		m_prevButtons = btn;
		m_prevWhite = w; 
		m_prevBlack = k;
	}

	void OnPad_Menu(const XBGAMEPAD& pad)
	{
		const DWORD btn = pad.wButtons;

		// Edge-detected dpad (like keyboard) + allow quick analog nudge
		bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)   && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))   || (pad.sThumbLY >  16000);
		bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN)) || (pad.sThumbLY < -16000);

		if (up)   { if (m_menuSel > 0)              --m_menuSel; }
		if (down) { if (m_menuSel < m_menuCount-1)  ++m_menuSel; }

		// Edge-detect face buttons
		unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
		unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
		unsigned char x = pad.bAnalogButtons[XINPUT_GAMEPAD_X];

		bool aTrig = (a > 30 && m_prevA <= 30);
		bool bTrig = (b > 30 && m_prevB <= 30);
		bool xTrig = (x > 30 && m_prevX <= 30);

		if (aTrig) {
			const MenuItem& mi = m_menu[m_menuSel];
			if (mi.enabled) ExecuteAction(mi.act);   // may close menu / change mode
		} else if (bTrig || xTrig) {
			CloseMenu();
		}

		// Save previous states for next frame
		m_prevButtons = btn;
		m_prevA = a; m_prevB = b; m_prevX = x;
		m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
		m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
	}

	void DrawRename(){
		if (!m_renActive) return;

		D3DVIEWPORT8 vp; m_pd3dDevice->GetViewport(&vp);
		const FLOAT panelW = max(520.0f, vp.Width * 0.55f);
		const FLOAT panelH = max(280.0f, vp.Height*0.45f);
		const FLOAT x = Snap((vp.Width  - panelW)*0.5f);
		const FLOAT y = Snap((vp.Height - panelH)*0.5f);

		DrawRect(x-8, y-8, panelW+16, panelH+16, 0xA0101010);
		DrawRect(x, y, panelW, panelH, 0xE0222222);

		DrawAnsi(m_font, x+12, y+8, 0xFFFFFFFF, "Rename");
		DrawHLine(x, y+28, panelW, 0x60FFFFFF);

		char hdr[640];
		_snprintf(hdr, sizeof(hdr), "In: %s", m_renParent); hdr[sizeof(hdr)-1]=0;
		DrawAnsi(m_font, x+12, y+34, 0xFFCCCCCC, hdr);

		const FLOAT boxY = Snap(y + 60.0f);
		const FLOAT boxH = 30.0f;
		DrawRect(x+12, boxY, panelW-24, boxH, 0xFF0E0E0E);

		// text
		DrawAnsi(m_font, x+18, boxY+4, 0xFFFFFF00, m_renBuf);

		// caret at cursor
		char tmp = m_renBuf[m_renCursor];
		m_renBuf[m_renCursor] = 0;
		FLOAT caretX = Snap(x+18 + MeasureTextW(m_renBuf));
		m_renBuf[m_renCursor] = tmp;
		DrawRect(caretX, boxY+4, 2.0f, boxH-8.0f, 0x90FFFF00);

		// keyboard grid (integer-snap to avoid drift)
		const FLOAT padX     = 12.0f;
		const FLOAT contentW = panelW - 2.0f*padX;
		const FLOAT cellH    = kLineH + 6.0f;
		const FLOAT gridTop  = Snap(boxY + boxH + 16.0f);

		// visual gaps between keys
		const FLOAT gapX     = 6.0f;   // horizontal gap between keys
		const FLOAT gapY     = 4.0f;   // vertical gap

		const FLOAT colW12   = contentW / 12.0f;

		for (int row=0; row<5; ++row){
			int cols = s_kb_cols[row];
			FLOAT rowY = Snap(gridTop + row*(cellH + gapY));

			for (int col=0; col<cols; ++col){
				// raw cell edges
				FLOAT x0, x1;
				if (row < 4){
					x0 = Snap(x + padX +  col      * colW12);
					x1 = Snap(x + padX + (col + 1) * colW12);
				}else{
					FLOAT colW = contentW / (FLOAT)cols;
					x0 = Snap(x + padX +  col      * colW);
					x1 = Snap(x + padX + (col + 1) * colW);
				}

				// make highlight symmetric: half gap left + half right
				const FLOAT drawX = x0 + gapX * 0.5f;
				const FLOAT drawW = (x1 - x0) - gapX;
				const bool  sel   = (row==m_renSelRow && col==m_renSelCol);

				DrawRect(drawX, rowY, drawW, cellH, sel ? 0x60FFFF00 : 0x30202020);

				// label text (centered)
				if (row < 4){
					char s[2]; s[0] = KbCharAt(row, col); s[1] = 0;
					FLOAT tw, th; MeasureTextWH(s, tw, th);
					const FLOAT tx = Snap(drawX + (drawW - tw) * 0.5f);
					const FLOAT ty = Snap(rowY + (cellH - th) * 0.5f);
					DrawAnsi(m_font, tx, ty, 0xFFE0E0E0, s);
				}else{
					const char* cap =
						(col==0) ? "Backspace" :
					(col==1) ? "Space"     :
					(col==2) ? "Aa"        :
					(col==3) ? "OK"        : "Cancel";
					FLOAT tw, th; MeasureTextWH(cap, tw, th);
					const FLOAT tx = Snap(drawX + (drawW - tw) * 0.5f);
					const FLOAT ty = Snap(rowY + (cellH - th) * 0.5f);
					DrawAnsi(m_font, tx, ty, 0xFFE0E0E0, cap);
				}
			}
		}

		DrawAnsi(m_font, x+12, y+panelH-20, 0xFFBBBBBB,
			"A: Select   B: Cancel   Start: OK   Y/Aa: Case   White/Black: Move cursor");
	}

	void ExecuteAction(Action act){
		Pane& src = m_pane[m_active];
		Pane& dst = m_pane[1 - m_active];

		const Item* sel = NULL;
		if (!src.items.empty()) sel = &src.items[src.sel];

		char srcFull[512]="";
		if (sel && src.mode==1 && !sel->isUpEntry) JoinPath(srcFull, sizeof(srcFull), src.curPath, sel->name);

		switch (act){
		case ACT_OPEN:
			if (sel){
				if (sel->isUpEntry) { UpOne(src); }
				else if (sel->isDir) { EnterSelection(src); }
			}
			CloseMenu(); break;

		case ACT_COPY:
			{
				if (!sel || sel->isUpEntry) { SetStatus("Nothing to copy"); CloseMenu(); break; }

				char dstDir[512];
				if (!ResolveDestDir(dstDir, sizeof(dstDir))) {
					SetStatus("Pick a destination (open a folder or select a drive)");
					CloseMenu(); break;
				}
				if ((dstDir[0]=='D' || dstDir[0]=='d') && dstDir[1]==':') {
					SetStatus("Cannot copy to D:\\ (read-only)");
					CloseMenu(); break;
				}

				NormalizeDirA(dstDir);
				if (!CanWriteHereA(dstDir)){
					SetStatusLastErr("Dest not writable");
					CloseMenu(); break;
				}

				if (CopyRecursiveA(srcFull, dstDir)) {
					Pane& dstp = m_pane[1 - m_active];
					if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0) ListDirectory(dstp.curPath, dstp.items);
					SetStatus("Copied to %s", dstDir);
				} else {
					SetStatusLastErr("Copy failed");
				}
				RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
				CloseMenu();
				break;
			}

		case ACT_MOVE:
			{
				if (!sel || sel->isUpEntry) { SetStatus("Nothing to move"); CloseMenu(); break; }

				char dstDir[512];
				if (!ResolveDestDir(dstDir, sizeof(dstDir))) {
					SetStatus("Pick a destination (open a folder or select a drive)");
					CloseMenu(); break;
				}
				if ((dstDir[0]=='D' || dstDir[0]=='d') && dstDir[1]==':') {
					SetStatus("Cannot move to D:\\ (read-only)");
					CloseMenu(); break;
				}

				NormalizeDirA(dstDir);
				if (!CanWriteHereA(dstDir)){
					SetStatusLastErr("Dest not writable");
					CloseMenu(); break;
				}

				if (CopyRecursiveA(srcFull, dstDir)) {
					if (!DeleteRecursiveA(srcFull)) {
						SetStatusLastErr("Move: delete source failed");
					} else {
						Pane& srcp = m_pane[m_active];
						Pane& dstp = m_pane[1 - m_active];
						if (dstp.mode==1 && _stricmp(dstp.curPath, dstDir)==0) ListDirectory(dstp.curPath, dstp.items);
						if (srcp.mode==1) { ListDirectory(srcp.curPath, srcp.items); if (srcp.sel >= (int)srcp.items.size()) srcp.sel = (int)srcp.items.size()-1; }
						SetStatus("Moved to %s", dstDir);
					}
				} else {
					SetStatusLastErr("Move: copy failed");
				}
				RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
				CloseMenu();
				break;
			}

		case ACT_DELETE:
			if (sel){
				if (DeleteRecursiveA(srcFull)){
					ListDirectory(src.curPath, src.items);
					if (src.sel >= (int)src.items.size()) src.sel = (int)src.items.size()-1;
					SetStatus("Deleted");
				} else SetStatus("Delete failed");
			}
			RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
			CloseMenu(); break;

		case ACT_RENAME:
			{
				if (sel && src.mode==1 && !sel->isUpEntry){
					BeginRename(src.curPath, sel->name); // BeginRename will hide menu & set MODE_RENAME
					CloseMenu();                         // safe (won't change mode unless we were in MENU)
				} else {
					SetStatus("Open a folder and select an item");
					CloseMenu();
				}
				break;
			}

		case ACT_MKDIR:
			{
				char baseDir[512] = {0};

				if (src.mode == 1) {
					_snprintf(baseDir, sizeof(baseDir), "%s", src.curPath);
				} else if (!src.items.empty()) {
					const Item& di = src.items[src.sel];
					if (di.isDir && !di.isUpEntry) {
						_snprintf(baseDir, sizeof(baseDir), "%s", di.name); // e.g. "E:\"
					}
				}
				baseDir[sizeof(baseDir)-1] = 0;
				if (!baseDir[0]) { SetStatus("Open a folder or select a drive first"); CloseMenu(); break; }

				if ((baseDir[0]=='D' || baseDir[0]=='d') && baseDir[1]==':') {
					SetStatus("Cannot create on D:\\ (read-only)"); CloseMenu(); break;
				}

				NormalizeDirA(baseDir);
				if (!CanWriteHereA(baseDir)) { SetStatusLastErr("Dest not writable"); CloseMenu(); break; }

				char nameBuf[64]; 
				char target[512];
				int idx = 0;
				for (;;){
					if (idx == 0) _snprintf(nameBuf, sizeof(nameBuf), "NewFolder");
					else          _snprintf(nameBuf, sizeof(nameBuf), "NewFolder%d", idx);
					nameBuf[sizeof(nameBuf)-1]=0;

					JoinPath(target, sizeof(target), baseDir, nameBuf);

					if (!DirExistsA(target)) {
						if (CreateDirectoryA(target, NULL)) {
							SetStatus("Created %s", nameBuf);
						} else {
							SetStatusLastErr("Create folder failed");
						}
						break;
					}
					if (++idx > 999){ SetStatus("Create folder failed (names exhausted)"); break; }
				}

				RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);

				if (src.mode == 1 && _stricmp(src.curPath, baseDir) == 0) {
					SelectItemInPane(src, nameBuf);
				}

				CloseMenu();
				break;
			}

		case ACT_CALCSIZE:
			if (sel){
				ULONGLONG bytes = DirSizeRecursiveA(srcFull);
				char tmp[64]; FormatSize(bytes, tmp, sizeof(tmp));
				SetStatus("%s", tmp);
			}
			CloseMenu(); break;

		case ACT_GOROOT:
			{
				if (src.mode == 1){
					if (!IsDriveRoot(src.curPath)){
						// go to current drive’s root
						char root[4] = { src.curPath[0], ':', '\\', 0 };
						strncpy(src.curPath, root, sizeof(src.curPath)-1);
						src.curPath[3] = 0;
						src.sel = 0; src.scroll = 0;
						ListDirectory(src.curPath, src.items);
					} else {
						// already at root -> show all drives
						src.mode = 0; src.curPath[0] = 0; src.sel = 0; src.scroll = 0;
						BuildDriveItems(src.items);
					}
				} else {
					// if we were on the drives list already, just ensure it’s built
					BuildDriveItems(src.items);
				}
				CloseMenu();
				break;
			}

		case ACT_SWITCHMEDIA:
			m_active = 1 - m_active; CloseMenu(); break;
		}
	}

	void DrawMenu(){
		if (!m_menuOpen) return;

		const FLOAT menuW = 340.0f;
		const FLOAT rowH  = kLineH + 6.0f;
		const FLOAT menuH = 12.0f + m_menuCount*rowH + 12.0f;

		const FLOAT paneX = (m_active==0) ? kListX_L : (kListX_L + kListW + kPaneGap);
		const FLOAT x = paneX + (kListW - menuW)*0.5f;
		const FLOAT y = kListY + 20.0f;

		// backdrop
		DrawRect(x-6.0f, y-6.0f, menuW+12.0f, menuH+12.0f, 0xA0101010);
		DrawRect(x, y, menuW, menuH, 0xE0222222);

		// title
		DrawAnsi(m_font, x+10.0f, y+6.0f, 0xFFFFFFFF, "Select action");
		DrawHLine(x, y+26.0f, menuW, 0x60FFFFFF);

		// items
		FLOAT iy = y + 30.0f;
		for (int i=0;i<m_menuCount;++i){
			bool sel = (i == m_menuSel);
			D3DCOLOR row = sel ? 0x60FFFF00 : 0x20202020;
			DrawRect(x+6.0f, iy-2.0f, menuW-12.0f, rowH, row);

			const MenuItem& mi = m_menu[i];
			DWORD col = mi.enabled? (sel?0xFF202020:0xFFE0E0E0) : 0xFF7A7A7A;
			DrawAnsi(m_font, x+16.0f, iy, col, mi.label);
			iy += rowH;
		}
	}

	HRESULT Initialize(){
		if (FAILED(m_font.Create("D:\\Media\\Font.xpr", 0))) {
			m_font.Create("D:\\Media\\CourierNew.xpr", 0);
		}
		XBInput_CreateGamepads(); MapStandardDrives_Io(); RescanDrives();
		BuildDriveItems(m_pane[0].items); BuildDriveItems(m_pane[1].items);

		// compute resolution-aware layout from viewport
		D3DVIEWPORT8 vp; m_pd3dDevice->GetViewport(&vp);

		const FLOAT sideMargin = max(24.0f,  vp.Width  * 0.04f);
		const FLOAT gap        = max(24.0f,  vp.Width  * 0.035f);

		kPaneGap  = gap;
		kListX_L  = sideMargin;

		const FLOAT totalUsable = (FLOAT)vp.Width - (sideMargin * 2.0f) - gap;
		kListW = max(260.0f, (totalUsable / 2.0f) - 10.0f);

		kHdrW   = kListW + 30.0f;
		kHdrY   = max(12.0f, vp.Height * 0.03f);
		kHdrH   = max(22.0f, vp.Height * 0.04f);
		kListY  = max(60.0f, kHdrY + kHdrH + 34.0f); // room under "Name/Size"

		kLineH  = max(22.0f, vp.Height * 0.036f);

		// compute rows by viewport (leave footer space)
		FLOAT bottomY = (FLOAT)vp.Height - max(48.0f, vp.Height * 0.09f);
		FLOAT usableH = bottomY - kListY; if (usableH < 0) usableH = 0;
		m_visible = (int)(usableH / kLineH); if (m_visible < 6) m_visible=6; if (m_visible>30) m_visible=30;

		return S_OK;
	}

	void EnsureListing(Pane& p){
		if (p.mode==0) BuildDriveItems(p.items);
		else           ListDirectory(p.curPath, p.items);

		if (p.sel >= (int)p.items.size()) p.sel = (int)p.items.size()-1;
		if (p.sel < 0) p.sel = 0;

		if (p.scroll > p.sel) p.scroll = p.sel;
		int maxScroll = max(0, (int)p.items.size() - m_visible);
		if (p.scroll > maxScroll) p.scroll = maxScroll;
	}

	void EnterSelection(Pane& p){
		if (p.items.empty()) return;
		const Item& it = p.items[p.sel];
		if (p.mode==0){ // into drive
			strncpy(p.curPath,it.name,sizeof(p.curPath)-1); p.curPath[sizeof(p.curPath)-1]=0;
			p.mode=1; p.sel=0; p.scroll=0; ListDirectory(p.curPath,p.items); return;
		}
		if (it.isUpEntry){
			if (strlen(p.curPath)<=3){ p.mode=0; p.sel=0; p.scroll=0; BuildDriveItems(p.items); p.curPath[0]=0; }
			else { ParentPath(p.curPath); p.sel=0; p.scroll=0; ListDirectory(p.curPath,p.items); }
			return;
		}
		if (it.isDir){
			char next[512]; JoinPath(next,sizeof(next),p.curPath,it.name);
			strncpy(p.curPath,next,sizeof(p.curPath)-1); p.curPath[sizeof(p.curPath)-1]=0;
			p.sel=0; p.scroll=0; ListDirectory(p.curPath,p.items); return;
		}
		// files: no-op in this build
	}

	void UpOne(Pane& p){
		if (p.mode==0) return;
		if (strlen(p.curPath)<=3){ p.mode=0; p.sel=0; p.scroll=0; BuildDriveItems(p.items); p.curPath[0]=0; return; }
		ParentPath(p.curPath); p.sel=0; p.scroll=0; ListDirectory(p.curPath,p.items);
	}

	void OnPad(const XBGAMEPAD& pad){
		// Rename modal takes over
		if (m_mode == MODE_RENAME){ OnPad_Rename(pad); return; }

		// Context menu modal
		if (m_mode == MODE_MENU){ OnPad_Menu(pad); return; }

		// Browse mode
		OnPad_Browse(pad);
	}



	void OnPad_Browse(const XBGAMEPAD& pad)
	{
		const DWORD btn = pad.wButtons;
		Pane& p = m_pane[m_active];

		// -------- NAVIGATION (auto-repeat with initial delay) --------
	const DWORD now   = GetTickCount();
	const int   kDead = 16000;     // analog deadzone
	const DWORD kInit = 230;       // ms before first repeat
	const DWORD kRep  = 120;       // ms between repeats

	int ud = 0;
	if ((btn & XINPUT_GAMEPAD_DPAD_UP)   || pad.sThumbLY >  kDead) ud = -1;
	if ((btn & XINPUT_GAMEPAD_DPAD_DOWN) || pad.sThumbLY < -kDead) ud = +1;

	if (ud == 0){
		m_navUDHeld = false;
		m_navUDDir  = 0;
	}else{
		if (!m_navUDHeld || m_navUDDir != ud){
			// first step immediately
			if (ud < 0){
				if (p.sel > 0){ --p.sel; if (p.sel < p.scroll) p.scroll = p.sel; }
			}else{
				int maxSel = (int)p.items.size() - 1;
				if (p.sel < maxSel){ ++p.sel; if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1); }
			}
			m_navUDHeld = true;
			m_navUDDir  = ud;
			m_navUDNext = now + kInit;
		}else if (now >= m_navUDNext){
			// held long enough -> repeat
			if (ud < 0){
				if (p.sel > 0){ --p.sel; if (p.sel < p.scroll) p.scroll = p.sel; }
			}else{
				int maxSel = (int)p.items.size() - 1;
				if (p.sel < maxSel){ ++p.sel; if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1); }
			}
			m_navUDNext = now + kRep;
		}
	}
		// Pane switch (edge-detected, no repeat)
		bool leftTrig  = (btn & XINPUT_GAMEPAD_DPAD_LEFT)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_LEFT);
		bool rightTrig = (btn & XINPUT_GAMEPAD_DPAD_RIGHT) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
		if (leftTrig)  m_active = 0;
		if (rightTrig) m_active = 1;

		// -------- Other buttons (edge-detected) --------
		unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
		unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
		unsigned char x = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
		unsigned char w = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE]; // page down
		unsigned char k = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK]; // page up

		bool aTrig = (a > 30 && m_prevA <= 30);
		bool bTrig = (b > 30 && m_prevB <= 30);
		bool xTrig = (x > 30 && m_prevX <= 30);
		bool wTrig = (w > 30 && m_prevWhite <= 30);
		bool kTrig = (k > 30 && m_prevBlack <= 30);
		bool startTrig = ((btn & XINPUT_GAMEPAD_START) && !(m_prevButtons & XINPUT_GAMEPAD_START));

		if (xTrig) { OpenMenu(); goto store_prev; } // open once

		if (aTrig) EnterSelection(p);
		if (bTrig) UpOne(p);

		if (kTrig){ // BLACK: page up
			p.sel -= m_visible; if (p.sel < 0) p.sel = 0;
			if (p.sel < p.scroll) p.scroll = p.sel;
		}
		if (wTrig){ // WHITE: page down
			int maxSel = (int)p.items.size()-1;
			p.sel += m_visible; if (p.sel > maxSel) p.sel = maxSel;
			if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1);
		}

		if (startTrig){
			MapStandardDrives_Io(); RescanDrives();
			EnsureListing(m_pane[0]); EnsureListing(m_pane[1]);
		}

store_prev:
		// remember states for edge detection next frame
		m_prevButtons = btn;
		m_prevA = a; m_prevB = b; m_prevX = x;
		m_prevWhite = w; m_prevBlack = k;
	}



	HRESULT Render(){
		m_pd3dDevice->Clear(0,NULL,D3DCLEAR_TARGET,0x20202020,1.0f,0);
		m_pd3dDevice->BeginScene();
		m_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
		m_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		m_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

		// left + right panes
		DrawPane(kListX_L,                     m_pane[0], m_active==0);
		DrawPane(kListX_L + kListW + kPaneGap, m_pane[1], m_active==1);

		// footer
		D3DVIEWPORT8 vp2; m_pd3dDevice->GetViewport(&vp2);
		const FLOAT footerY = (FLOAT)vp2.Height - max(48.0f, vp2.Height * 0.09f);
		DrawRect(HdrX(kListX_L), footerY, kHdrW*2 + kPaneGap + 30.0f, 28.0f, 0x802A2A2A);
		if (m_pane[m_active].mode==0){
			DrawAnsi(m_font, HdrX(kListX_L)+5.0f, footerY+4.0f, 0xFFCCCCCC,
				"D-Pad: Move  |  Left/Right: Switch pane  |  A: Enter  |  X: Menu  |  Start: Map+Rescan  |  Black/White: Page");
		} else {
			ULONGLONG fb=0, tb=0; GetDriveFreeTotal(m_pane[m_active].curPath, fb, tb);
			char fstr[64], tstr[64], bar[420];
			FormatSize(fb, fstr, sizeof(fstr)); FormatSize(tb, tstr, sizeof(tstr));
			_snprintf(bar,sizeof(bar),
				"Active: %s   |   B: Up   |   Free: %s / Total: %s   |   Left/Right: Switch pane   |   X: Menu   |   Black/White: Page",
				m_active==0?"Left":"Right", fstr, tstr);
			bar[sizeof(bar)-1]=0;
			DrawAnsi(m_font, HdrX(kListX_L)+5.0f, footerY+4.0f, 0xFFCCCCCC, bar);
		}

		// transient status (over footer, if any)
		DWORD now = GetTickCount();
		if (now < m_statusUntilMs && m_status[0]){
			DrawAnsi(m_font, HdrX(kListX_L)+5.0f, footerY-18.0f, 0xFFBBDDEE, m_status);
		}

		// modal menu
		DrawMenu();

		// rename overlay (modal)
		DrawRename();

		m_pd3dDevice->EndScene(); m_pd3dDevice->Present(NULL,NULL,NULL,NULL);
		return S_OK;
	}

	// input pump
	HRESULT FrameMove(){
		XBInput_GetInput();
		OnPad(g_Gamepads[0]);
		return S_OK;
	}

	// pane draw (placed after other methods for readability)
	void DrawPane(FLOAT baseX, Pane& p, bool active){
		// header
		FLOAT hx = HdrX(baseX);
		DrawRect(hx, kHdrY, kHdrW, kHdrH, active ? 0xFF3A3A3A : 0x802A2A2A);
		if (p.mode==0) DrawAnsi(m_font, hx+5.0f, kHdrY+6.0f, 0xFFFFFFFF, "Detected Drives");
		else { char hdr[600]; _snprintf(hdr,sizeof(hdr),"%s", p.curPath); hdr[sizeof(hdr)-1]=0; DrawAnsi(m_font, hx+5.0f, kHdrY+6.0f, 0xFFFFFFFF, hdr); }

		// compute Size column width/X for this pane
		const FLOAT sizeColW = ComputeSizeColW(p);
		const FLOAT sizeColX = baseX + kListW - (kScrollBarW + kPaddingX + sizeColW);
		const FLOAT sizeRight = sizeColX + sizeColW;

		// column header
		DrawRect(baseX-10.0f, kHdrY+kHdrH+6.0f, kListW+20.0f, 22.0f, 0x60333333);
		DrawAnsi(m_font, NameColX(baseX),      kHdrY+kHdrH+10.0f, 0xFFDDDDDD, "Name");
		DrawRightAligned("Size",               sizeRight,         kHdrY+kHdrH+10.0f, 0xFFDDDDDD);
		DrawHLine(baseX-10.0f, kHdrY+kHdrH+28.0f, kListW+20.0f, 0x80444444);
		DrawVLine(sizeColX - 8.0f, kHdrY+kHdrH+7.0f, 22.0f, 0x40444444);

		// list bg
		DrawRect(baseX-10.0f, kListY-6.0f, kListW+20.0f, kLineH*m_visible+12.0f, 0x30101010);

		// stripes + selection
		int end=p.scroll+m_visible; if(end>(int)p.items.size()) end=(int)p.items.size();
		int rowIndex=0;
		for(int i=p.scroll;i<end;++i,++rowIndex){
			D3DCOLOR stripe=(rowIndex&1)?0x201E1E1E:0x10000000;
			DrawRect(baseX, kListY + rowIndex*kLineH - 2.0f, kListW-8.0f, kLineH, stripe);
		}
		if(!p.items.empty() && p.sel>=p.scroll && p.sel<end){
			int selRow=p.sel-p.scroll;
			DrawRect(baseX, kListY + selRow*kLineH - 2.0f, kListW-8.0f, kLineH, active?0x60FFFF00:0x30FFFF00);
		}

		// rows
		FLOAT y=kListY;
		for(int i=p.scroll, r=0; i<end; ++i,++r){
			const Item& it=p.items[i];
			DWORD nameCol=(i==p.sel)?0xFFFFFF00:0xFFE0E0E0;
			DWORD sizeCol=(i==p.sel)?0xFFFFFF00:0xFFB0B0B0;
			D3DCOLOR ico = it.isUpEntry ? 0xFFAAAAAA : (it.isDir ? 0xFF5EA4FF : 0xFF89D07E);
			DrawRect(baseX+2.0f, y+6.0f, kGutterW-8.0f, kLineH-12.0f, ico);
			const char* glyph = it.isUpEntry ? ".." : (it.isDir?"+":"-");
			DrawAnsi(m_font, baseX+4.0f, y+4.0f, 0xFFFFFFFF, glyph);

			char nameBuf[300]; _snprintf(nameBuf,sizeof(nameBuf),"%s",it.name); nameBuf[sizeof(nameBuf)-1]=0;
			DrawAnsi(m_font, NameColX(baseX), y, nameCol, nameBuf);

			char sz[64]=""; if(!it.isDir && !it.isUpEntry){ FormatSize(it.size, sz, sizeof(sz)); }
			DrawRightAligned(sz, sizeRight, y, sizeCol);

			y += kLineH;
		}

		// scrollbar
		if((int)p.items.size()>m_visible){
			FLOAT trackX = baseX + kListW - kScrollBarW - 4.0f;
			FLOAT trackY = kListY - 2.0f;
			FLOAT trackH = m_visible * kLineH;
			DrawRect(trackX, trackY, kScrollBarW, trackH, 0x40282828);
			int total=(int)p.items.size();
			FLOAT thumbH=(FLOAT)m_visible/(FLOAT)total*trackH; if(thumbH<10.0f) thumbH=10.0f;
			FLOAT maxScroll=(FLOAT)(total-m_visible);
			FLOAT t=(maxScroll>0.0f)?((FLOAT)p.scroll/maxScroll):0.0f;
			FLOAT thumbY=trackY + t*(trackH-thumbH);
			DrawRect(trackX, thumbY, kScrollBarW, thumbH, 0x80C0C0C0);
		}
	}
};

// ---- static layout defs (defaults; overwritten in Initialize) ----
FLOAT FileBrowserApp::kListX_L    = 50.0f;
FLOAT FileBrowserApp::kListY      = 100.0f;
FLOAT FileBrowserApp::kListW      = 540.0f;
FLOAT FileBrowserApp::kLineH      = 26.0f;

FLOAT FileBrowserApp::kHdrX_L     = 35.0f;
FLOAT FileBrowserApp::kHdrY       = 22.0f;
FLOAT FileBrowserApp::kHdrW       = 570.0f;
FLOAT FileBrowserApp::kHdrH       = 28.0f;

FLOAT FileBrowserApp::kGutterW    = 18.0f;
FLOAT FileBrowserApp::kPaddingX   = 6.0f;
FLOAT FileBrowserApp::kScrollBarW = 3.0f;
FLOAT FileBrowserApp::kPaneGap    = 60.0f;

// OG XDK prefers main() entry
int __cdecl main(){ FileBrowserApp app; app.Create(); return app.Run(); }
