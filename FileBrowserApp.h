#ifndef FILEBROWSERAPP_H
#define FILEBROWSERAPP_H

#include <xtl.h>
#include <vector>
#include "XBApp.h"
#include "XBFont.h"
#include "XBInput.h"
#include "FsUtil.h"
#include "OnScreenKeyboard.h"

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
    // ----- Pane model -----
    struct Pane {
        std::vector<Item> items;
        char  curPath[512];
        int   mode;   // 0=drives, 1=dir
        int   sel;
        int   scroll;
        Pane(){ curPath[0]=0; mode=0; sel=0; scroll=0; }
    };

    // TL vertex for solid rects
    struct TLVERT { float x,y,z,rhw; D3DCOLOR color; };
    enum { FVF_TLVERT = D3DFVF_XYZRHW | D3DFVF_DIFFUSE };

    // ----- UI helpers -----
    static FLOAT NameColX(FLOAT baseX){ return baseX + kGutterW + kPaddingX; }
    static FLOAT HdrX   (FLOAT baseX){ return baseX - 15.0f; }

    FLOAT MeasureTextW(const char* s);
    void  MeasureTextWH(const char* s, FLOAT& outW, FLOAT& outH);
    void  DrawRightAligned(const char* s, FLOAT rightX, FLOAT y, DWORD color);
    void  DrawRect(float x,float y,float w,float h,D3DCOLOR c);
    void  DrawHLine(float x,float y,float w,D3DCOLOR c){ DrawRect(x,y,w,1.0f,c); }
    void  DrawVLine(float x,float y,float h,D3DCOLOR c){ DrawRect(x,y,1.0f,h,c); }

    // ----- Layout / columns -----
    FLOAT ComputeSizeColW(const Pane& p);

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

    // ----- Context menu -----
    enum Action {
        ACT_OPEN, ACT_COPY, ACT_MOVE, ACT_DELETE, ACT_RENAME,
        ACT_MKDIR, ACT_CALCSIZE, ACT_GOROOT, ACT_SWITCHMEDIA
    };
    struct MenuItem { const char* label; Action act; bool enabled; };

    void  AddMenuItem(const char* label, Action act, bool enabled);
    void  BuildContextMenu();
    void  OpenMenu();
    void  CloseMenu();
    void  ExecuteAction(Action act);
    void  DrawMenu();

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

    // context menu
    bool      m_menuOpen;
    int       m_menuSel;
    MenuItem  m_menu[9];
    int       m_menuCount;
    enum { MODE_BROWSE, MODE_MENU, MODE_RENAME } m_mode;

    // status
    char  m_status[256];
    DWORD m_statusUntilMs;

    // drawing
    void DrawPane(FLOAT baseX, Pane& p, bool active);

    // On-screen keyboard (new, self-contained)
    OnScreenKeyboard m_kb;
};

#endif // FILEBROWSERAPP_H
