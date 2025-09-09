#ifndef FILEBROWSERAPP_H
#define FILEBROWSERAPP_H

/*
===============================================================================
 FileBrowserApp
  - Core app header for the dual-pane file browser (OG Xbox, XDK, VS2003).
  - Owns UI state, input routing, drawing, and delegates file operations to
    helpers in FsUtil.* and AppActions.*.
===============================================================================
*/

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
#include "AppActions.h"

// Allow AppActions to call back into private helpers without exposing them.
namespace AppActions { void Execute(Action, class FileBrowserApp&); }

/*
------------------------------------------------------------------------------
 ProgState
  - Lightweight progress HUD state (used during copy/move).
------------------------------------------------------------------------------
*/
struct ProgState {
    bool        active;         // overlay visible when true
    ULONGLONG   done;           // bytes completed so far
    ULONGLONG   total;          // total bytes (0 if unknown)
    char        current[256];   // current path/file shown in overlay
    DWORD       lastPaintMs;    // throttle overlay redraw rate
    char        title[24];      // short title ("Copying...", etc.)

    ProgState()
        : active(false), done(0), total(0), lastPaintMs(0)
    {
        current[0] = 0;
        title[0]   = 0;
    }
};

class FileBrowserApp : public CXBApplication {
        // Keep first for tighter packing; overlay is accessed frequently.
        ProgState m_prog;

public:
    // Allow centralized action runner to call private helpers/members.
    friend void AppActions::Execute(Action, FileBrowserApp&);

    FileBrowserApp();

    // --- CXBApplication lifecycle ------------------------------------------
    virtual HRESULT Initialize(); // create font, map drives, compute layout
    virtual HRESULT FrameMove();  // input + periodic drive rescan
    virtual HRESULT Render();     // draw panes, footer, overlays

    // Recompute responsive layout using the current D3D viewport.
    void ComputeResponsiveLayout();

    // --- Progress HUD API ---------------------------------------------------
    // Show the progress overlay and initialize counters/labels.
    void BeginProgress(ULONGLONG total, const char* firstLabel, const char* title = "Working...");
    // Update progress (bytes + optional label). Throttles Render() internally.
    void UpdateProgress(ULONGLONG done, ULONGLONG total, const char* label);
    // Hide progress overlay.
    void EndProgress();
    // Draw the overlay; called from Render() when m_prog.active.
    void DrawProgressOverlay();

    // --- Status line (footer toast) ----------------------------------------
    void  SetStatus(const char* fmt, ...);      // printf-style
    void  SetStatusLastErr(const char* prefix); // append GetLastError()
    DWORD StatusUntilMs() const;                // used by Render() to time out

private:
    // --- UI helpers ---------------------------------------------------------
    static FLOAT HdrX(FLOAT baseX){ return baseX - 15.0f; } // header left offset
    void  DrawRect(float x,float y,float w,float h,D3DCOLOR c);
    void  DrawHLine(float x,float y,float w,D3DCOLOR c){ DrawRect(x,y,w,1.0f,c); }
    void  DrawVLine(float x,float y,float h,D3DCOLOR c){ DrawRect(x,y,1.0f,h,c); }

    // --- Data refresh / navigation -----------------------------------------
    void  EnsureListing(Pane& p);                  // clamp indices and items
    void  EnterSelection(Pane& p);                 // drive->dir, ".."->up, dir->descend, .xbe->launch
    void  UpOne(Pane& p);                          // go up one or back to drive list
    void  RefreshPane(Pane& p);                    // rebuild items, preserve selection/scroll
    bool  ResolveDestDir(char* outDst, size_t cap);// determine destination dir from other pane
    void  SelectItemInPane(Pane& p, const char* name);

    // --- Context menu -------------------------------------------------------
    void  AddMenuItem(const char* label, Action act, bool enabled);
    void  BuildContextMenu(); // build items based on mode/selection
    void  OpenMenu();         // position and open popup
    void  CloseMenu();        // close and return to browse mode

    // --- Rename OSD / keyboard ---------------------------------------------
    void  BeginRename(const char* parentDir, const char* oldName);
    void  CancelRename();
    void  AcceptRename();     // validate, sanitize, perform MoveFileA
    void  DrawRename();

    // --- Input routing ------------------------------------------------------
    void  OnPad(const XBGAMEPAD& pad);      // router
    void  OnPad_Browse(const XBGAMEPAD& pad);
    void  OnPad_Menu(const XBGAMEPAD& pad);
    void  OnPad_Rename(const XBGAMEPAD& pad);

    // --- Members ------------------------------------------------------------
    CXBFont m_font;           // UI font (XPR asset)
    int     m_visible;        // number of visible rows per pane

    // Edge-detect state for buttons/analog (prevent double triggers).
    unsigned char m_prevA, m_prevB, m_prevX, m_prevY;
    unsigned char m_prevWhite, m_prevBlack;
    DWORD         m_prevButtons;

    Pane    m_pane[2];        // left (0) and right (1) pane data
    int     m_active;         // active pane: 0=left, 1=right

    // Up/down auto-repeat (list navigation).
    bool  m_navUDHeld;        // currently repeating
    int   m_navUDDir;         // -1 up, +1 down
    DWORD m_navUDNext;        // next repeat time (GetTickCount ms)

    // Mode and UI components
    enum { MODE_BROWSE, MODE_MENU, MODE_RENAME } m_mode;
    ContextMenu      m_ctx;       // popup menu
    OnScreenKeyboard m_kb;        // rename overlay
    PaneRenderer     m_renderer;  // draws a pane (headers, rows, scrollbar)

    // Draw the already-open context menu (used by Render()).
    void DrawMenu();

    // Status text buffer and expiry tick for footer toast.
    char  m_status[256];
    DWORD m_statusUntilMs;

    // Legacy action entry point (kept for compatibility).
    void ExecuteAction(Action act);

    // Per-action helpers (legacy; main impl lives in AppActions.cpp).
    void Act_Open();
    void Act_Copy();
    void Act_Move();
    void Act_Delete();
    void Act_Rename();
    void Act_Mkdir();
    void Act_CalcSize();
    void Act_GoRoot();
    void Act_SwitchMedia();
    void Act_FormatCache(); // destructive: formats X/Y/Z cache

    // Copy current pad state so inputs do not leak between modes.
    void AbsorbPadState(const XBGAMEPAD& pad);

	// --- Back-to-exit confirm state (press Back twice to exit) ---
	bool  m_backConfirmArmed;   // true after first Back press
	DWORD m_backConfirmUntil;   // snapshot of status expiry (informational)
	unsigned char m_prevBack;   // edge detect for Back

	// Exit helper (define one of the strategies inside)
	void ExitNow();
	
	ULONGLONG m_dvdUsedBytes;   // no in-class init here
	ULONGLONG m_dvdTotalBytes;  // "
	bool      m_dvdHaveStats;

    // -------------------------------------------------------------------------
    // Responsive layout state (computed in ComputeResponsiveLayout()).
    // These replace the old static k* constants so we can adapt to any
    // viewport size/aspect without #defines or hardcoded coordinates.
    // -------------------------------------------------------------------------
    FLOAT kPaneGap;        // gap between the two panes
    FLOAT kListX_L;        // left pane X
    FLOAT kListW;          // pane width

    FLOAT kHdrY,  kHdrH, kHdrW; // header band geometry per pane
    FLOAT kListY, kLineH;       // list top and per-row height

    FLOAT kGutterW;       // icon gutter width
    FLOAT kPaddingX;      // left/right text padding inside pane
    FLOAT kScrollBarW;    // scrollbar track width
};

#endif // FILEBROWSERAPP_H
