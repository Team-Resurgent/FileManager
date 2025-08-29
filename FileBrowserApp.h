#ifndef FILEBROWSERAPP_H
#define FILEBROWSERAPP_H

#include <xtl.h>
#include <vector>
#include <wchar.h>
#include "XBApp.h"
#include "XBFont.h"
#include "XBInput.h"
#include "FsUtil.h"
#include "OnScreenKeyboard.h"
#include "ContextMenu.h"
#include "PaneModel.h"
#include "PaneRenderer.h"

// Dual-pane file browser application (OG Xbox XDK / VS2003)
class FileBrowserApp : public CXBApplication {
public:
    FileBrowserApp();

    // CXBApplication overrides
    virtual HRESULT Initialize();
    virtual HRESULT FrameMove();
    virtual HRESULT Render();

    // Static layout (computed in Initialize, defaults in .cpp)
    static FLOAT kListX_L, kListY, kListW, kLineH;
    static FLOAT kHdrX_L,  kHdrY,  kHdrW,  kHdrH;
    static FLOAT kGutterW, kPaddingX, kScrollBarW;
    static FLOAT kPaneGap;

private:
    // ----- UI helpers -----
    static FLOAT HdrX(FLOAT baseX){ return baseX - 15.0f; }
    void  DrawRect(float x,float y,float w,float h,D3DCOLOR c);
    void  DrawHLine(float x,float y,float w,D3DCOLOR c){ DrawRect(x,y,w,1.0f,c); }
    void  DrawVLine(float x,float y,float h,D3DCOLOR c){ DrawRect(x,y,1.0f,h,c); }

    // ----- Status line -----
    void  SetStatus(const char* fmt, ...);
    void  SetStatusLastErr(const char* prefix);

    // ----- Data refresh / navigation -----
    void  EnsureListing(Pane& p);
    void  EnterSelection(Pane& p);
    void  UpOne(Pane& p);
    void  RefreshPane(Pane& p);
    bool  ResolveDestDir(char* outDst, size_t cap);
    void  SelectItemInPane(Pane& p, const char* name);

    // ----- Context menu (delegated) -----
    void  AddMenuItem(const char* label, Action act, bool enabled);
    void  BuildContextMenu();
    void  OpenMenu();
    void  CloseMenu();

    // ----- Rename OSD / keyboard -----
    void  BeginRename(const char* parentDir, const char* oldName);
    void  CancelRename();
    void  AcceptRename();
    void  DrawRename();

    // ----- Input routing -----
    void  OnPad(const XBGAMEPAD& pad);
    void  OnPad_Browse(const XBGAMEPAD& pad);
    void  OnPad_Menu(const XBGAMEPAD& pad);
    void  OnPad_Rename(const XBGAMEPAD& pad);

    // ----- Members -----
    CXBFont m_font;
    int     m_visible;              // rows visible

    // edge-detect state
    unsigned char m_prevA, m_prevB, m_prevX, m_prevY;
    unsigned char m_prevWhite, m_prevBlack;
    DWORD         m_prevButtons;

    Pane    m_pane[2];
    int     m_active;               // 0=left, 1=right

    // up/down auto-repeat
    bool  m_navUDHeld;
    int   m_navUDDir;       // -1 up, +1 down
    DWORD m_navUDNext;      // next repeat time (ms)

    // mode & UI components
    enum { MODE_BROWSE, MODE_MENU, MODE_RENAME } m_mode;
    ContextMenu      m_ctx;
    OnScreenKeyboard m_kb;
    PaneRenderer     m_renderer;
	
	// draws the already-open context menu
	void DrawMenu();

    // status
    char  m_status[256];
    DWORD m_statusUntilMs;

    // actions (still here for now)
    void ExecuteAction(Action act);
};

#endif // FILEBROWSERAPP_H
