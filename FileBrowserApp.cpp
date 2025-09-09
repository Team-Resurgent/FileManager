#include "FileBrowserApp.h"
#include "AppActions.h"
#include "GfxPrims.h"
#include "FsUtil.h"
#include <wchar.h>
#include <stdarg.h>
#include <algorithm>
#include <stdio.h> // _snprintf
#include <xgraphics.h> 
/*
===============================================================================
 FileBrowserApp
  - Main application controller for the dual-pane file browser.
  - Owns input routing, pane state, context menu, OSD keyboard, progress HUD,
    and the high-level render loop.
  - Keeps allocations minimal and uses ACP->UTF-16 conversions for fonts.
===============================================================================
*/

// Simple getter used by overlay/status timers.
DWORD FileBrowserApp::StatusUntilMs() const { return m_statusUntilMs; }

// ---- local helpers (no header pollution) -----------------------------------
// These helpers are file-local to avoid leaking symbols into headers/other TUs.
namespace {
    // Small math/layout helpers
    inline FLOAT MaxF(FLOAT a, FLOAT b){ return (a>b)?a:b; }
	inline FLOAT MinF(FLOAT a, FLOAT b) { return (a < b) ? a : b; }
    inline int   MaxI (int a,   int b){ return (a>b)?a:b; }
    inline FLOAT Snap (FLOAT v){ return (FLOAT)((int)(v + 0.5f)); } // pixel-align

    // Draw ANSI string via CXBFont (expects UTF-16); convert on the fly.
    inline void DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
        WCHAR wbuf[512];
        MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
        font.DrawText(x,y,color,wbuf,0,0.0f);
	}


	// Footer geometry used everywhere
	inline FLOAT FooterBandPx(FLOAT screenH)   { return MaxF(48.0f, screenH * 0.09f); }
	// Always keep a little gap above the footer so rows don't "kiss" it.
	inline FLOAT FooterSpacerPx(FLOAT screenH) { return MaxF(6.0f,  screenH * 0.012f); }
		

	// ---- character-step marquee (same tuning as panes/OSK) ---------------------
static const DWORD kProgInitPauseMs = 900;
static const DWORD kProgStepMs      = 150;
static const DWORD kProgEndPauseMs  = 1200;
static const int   kProgStepChars   = 1;

struct ProgMarquee {
    FLOAT px;         // start index (characters)
    FLOAT fitWLock;   // lock width for stable end calc
    DWORD nextTick;   // next update tick
    DWORD resetPause; // nonzero while pausing at end
    char  last[768];  // last string drawn (for reset-on-change)
    ProgMarquee() : px(0.f), fitWLock(0.f), nextTick(0), resetPause(0) { last[0]=0; }
};

// Draw one line at (x,y) clipped to maxW using a character-step marquee.
static void DrawLabelFittedOrMarquee(CXBFont& font,
                                     FLOAT x, FLOAT y, FLOAT maxW,
                                     DWORD color, const char* text,
                                     ProgMarquee& M)
{
    if (!text) text = "";

    // right guard + tolerance like OSK
    const FLOAT kRightGuard = 1.5f;
    const FLOAT kTol        = 2.0f;

    const FLOAT fitW_now = (maxW > kRightGuard) ? (maxW - kRightGuard) : 0.0f;

    // Measure full once
    WCHAR wfull[768]; MultiByteToWideChar(CP_ACP, 0, text, -1, wfull, 768);
    FLOAT fullW=0, fullH=0; font.GetTextExtent(wfull, &fullW, &fullH);

    // If it fits, draw + reset
    if (fullW <= fitW_now + kTol){
        font.DrawText((FLOAT)((int)(x+0.5f)), (FLOAT)((int)(y+0.5f)), color, wfull, 0, 0.0f);
        M = ProgMarquee();
        return;
    }

    // Reset if label changed
    if (_stricmp(M.last, text) != 0){
        _snprintf(M.last, sizeof(M.last), "%s", text); M.last[sizeof(M.last)-1]=0;
        M.px = 0.0f; M.fitWLock = 0.0f; M.resetPause = 0; M.nextTick = GetTickCount() + kProgInitPauseMs;
    }

    const DWORD now = GetTickCount();
    if (M.fitWLock <= 0.0f) M.fitWLock = fitW_now;
    const FLOAT fitW = (FLOAT)((int)(M.fitWLock + 0.5f));

    const char* s = text; const int len = (int)strlen(s);

    // earliest start where the whole tail fits
    int lo = 0, hi = len;
    while (lo < hi){
        const int mid = (lo + hi) / 2;
        WCHAR wtmp[768]; MultiByteToWideChar(CP_ACP, 0, s + mid, -1, wtmp, 768);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kTol) hi = mid; else lo = mid + 1;
    }
    const int lastStart = (lo > len) ? len : lo;

    int startIdx = (int)M.px;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > lastStart) startIdx = lastStart;

    // longest substring from start that fits
    const char* startPtr = s + startIdx;
    int lo2 = 0, hi2 = (int)strlen(startPtr);
    while (lo2 < hi2){
        const int mid = (lo2 + hi2 + 1) / 2;
        char tmp[768]; _snprintf(tmp, sizeof(tmp), "%.*s", mid, startPtr);
        WCHAR wtmp[768]; MultiByteToWideChar(CP_ACP, 0, tmp, -1, wtmp, 768);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kTol) lo2 = mid; else hi2 = mid - 1;
    }

    char vis[768]; _snprintf(vis, sizeof(vis), "%.*s", lo2, startPtr); vis[sizeof(vis)-1]=0;
    WCHAR wvis[768]; MultiByteToWideChar(CP_ACP, 0, vis, -1, wvis, 768);
    font.DrawText((FLOAT)((int)(x+0.5f)), (FLOAT)((int)(y+0.5f)), color, wvis, 0, 0.0f);

    // step/pause/reset
    if (now >= M.nextTick){
        if (M.resetPause){
            M.px        = 0.0f;
            M.resetPause= 0;
            M.nextTick  = now + kProgInitPauseMs;
        } else {
            if (startIdx >= lastStart){
                M.px        = (FLOAT)lastStart;
                M.resetPause= now + kProgEndPauseMs;
                M.nextTick  = M.resetPause;
            } else {
                M.px       = (FLOAT)(startIdx + kProgStepChars);
                M.nextTick = now + kProgStepMs;
            }
        }
    }
}


    // Center a one-line ANSI string horizontally in a span.
    inline void DrawAnsiCenteredX(CXBFont& font, FLOAT left, FLOAT width, FLOAT y, DWORD color, const char* text){
        WCHAR wbuf[512];
        MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
        FLOAT tw=0, th=0; font.GetTextExtent(wbuf, &tw, &th);
        const FLOAT x = Snap(left + (width - tw) * 0.5f);
        font.DrawText(x, y, color, wbuf, 0, 0.0f);
    }

    // Measure ANSI string (after ACP->UTF16 conversion).
    inline void MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH){
        WCHAR wbuf[512];
        MultiByteToWideChar(CP_ACP, 0, s ? s : "", -1, wbuf, 512);
        font.GetTextExtent(wbuf, &outW, &outH);
    }

    // Ellipsize the left side to fit maxW, keeping the tail (useful for paths).
    inline void LeftEllipsizeToFit(CXBFont& font, const char* src, FLOAT maxW,
                                   char* out, size_t cap){
        if (!src) { out[0]=0; return; }
        FLOAT w=0,h=0; MeasureTextWH(font, src, w, h);
        if (w <= maxW){ _snprintf(out, (int)cap, "%s", src); out[cap-1]=0; return; }

        const char* tail = src + strlen(src);
        const char* p    = tail;
        char buf[1024];  // temp
        // Walk leftward until "...<suffix>" fits.
        for (;;){
            if (p == src) break;
            --p;
            _snprintf(buf, sizeof(buf), "...%s", p);
            MeasureTextWH(font, buf, w, h);
            if (w > maxW){
                ++p; // last good char is one ahead
                break;
            }
        }
        _snprintf(out, (int)cap, "...%s", p);
        out[cap-1]=0;
    }

    // Marquee helpers for progress overlay
    inline void MbToW(const char* s, WCHAR* w, int capW){
        MultiByteToWideChar(CP_ACP, 0, s ? s : "", -1, w, capW);
    }

    // Return first byte index whose rendered width reaches px (for marquee).
    static const char* SkipToPixelOffset(CXBFont& font, const char* s, FLOAT px, FLOAT& skippedW){
        skippedW = 0.0f;
        if (!s || px <= 0.0f) return s ? s : "";
        const char* p = s;
        WCHAR wtmp[256]; FLOAT tw=0, th=0;
        while (*p){
            int len = (int)(p - s + 1); if (len > 255) len = 255;
            char tmp[256]; _snprintf(tmp, sizeof(tmp), "%.*s", len, s);
            MbToW(tmp, wtmp, 256); font.GetTextExtent(wtmp, &tw, &th);
            if (tw >= px) break;
            ++p;
        }
        skippedW = tw;
        return p;
    }

	// Return last component of a path (handles trailing '\'). e.g. "E:\A\B\" -> "B"
	static void ExtractLastComponent(const char* path, char* out, size_t cap){
		if (!out || cap == 0) return;
		out[0] = 0;
		if (!path) return;

		size_t n = strlen(path);
		if (n == 0) return;

		// Skip trailing '\' (but keep root like "E:\")
		if (n > 3 && path[n-1] == '\\') --n;

		// Find last '\'
		int i = (int)n - 1;
		while (i >= 0 && path[i] != '\\') --i;

		const char* start = path + i + 1;
		size_t len = n - (i + 1);
		if (len >= cap) len = cap - 1;
		if (cap) { memcpy(out, start, len); out[len] = 0; }
	}


} // anonymous namespace


// ----------------------------------------------------------------------------
// ComputeResponsiveLayout
//  - Derives all pane/header/row metrics from the current device viewport.
//  - Ensures perfect left/right symmetry: [margin] [left pane] [gap] [right pane] [margin].
//  - Keeps header bars exactly the same width as the panes.
// ----------------------------------------------------------------------------
void FileBrowserApp::ComputeResponsiveLayout()
{
    D3DVIEWPORT8 vp; m_pd3dDevice->GetViewport(&vp);

    // ---- outer geometry ----
    const FLOAT margin = MaxF(24.0f,  vp.Width  * 0.04f);
    const FLOAT gap    = MaxF(24.0f,  vp.Width  * 0.035f);
    const FLOAT paneW  = MaxF(260.0f, (vp.Width - (margin * 2.0f) - gap) * 0.5f);

    kPaneGap  = gap;
    kListX_L  = margin;
    kListW    = paneW;

    // header band
    kHdrW     = kListW;
    kHdrY     = MaxF(12.0f, vp.Height * 0.03f);
    kHdrH     = MaxF(22.0f, vp.Height * 0.04f);

    // row height
    kLineH    = MaxF(22.0f, vp.Height * 0.036f);

    // keep kListY as the "pane body top" (what the rest of the code expects)
    const FLOAT headerGap = MaxF(6.0f, kHdrH * 0.35f);
    kListY = kHdrY + kHdrH + headerGap;

    // ---- visible rows (match PaneRenderer::DrawPane vertical math) ----
    // DrawPane uses: listTop = (kListY) + colHdrH
    const FLOAT colHdrH = MaxF(22.0f, kLineH);
    const FLOAT listTopInRenderer = kListY + colHdrH;

    // leave room for the footer band + a small spacer at every resolution
    const FLOAT footerBand   = MaxF(48.0f, vp.Height * 0.09f);
    const FLOAT footerSpacer = MaxF(6.0f,  vp.Height * 0.012f);
    const FLOAT bottomY      = (FLOAT)vp.Height - footerBand - footerSpacer;

    FLOAT usableH = bottomY - listTopInRenderer;
    if (usableH < 0) usableH = 0;

    m_visible = (int)(usableH / kLineH);
    if (m_visible < 6)  m_visible = 6;
    if (m_visible > 30) m_visible = 30;

    // fixed micro-metrics
    kGutterW    = 18.0f;
    kPaddingX   = 6.0f;
    kScrollBarW = 3.0f;
}


// ----------------------------------------------------------------------------
// ctor: set defaults for input, layout, D3D, and app state
FileBrowserApp::FileBrowserApp(){
    m_visible=13; m_prevA=0; m_prevB=0; m_prevX=0; m_prevY=0; m_active=0;
    m_prevButtons = 0; m_prevWhite = m_prevBlack = 0;
    m_navUDHeld=false; m_navUDDir=0; m_navUDNext=0; m_backConfirmArmed = false;
    m_backConfirmUntil = 0; m_prevBack = 0;
    m_dvdUsedBytes  = 0;
    m_dvdTotalBytes = 0;
    m_dvdHaveStats  = false;

    // --- Auto-detect video capabilities and set PresentParams ----------------
	ZeroMemory(&m_d3dpp, sizeof(m_d3dpp));

	// Dashboard video flags and TV standard
	const DWORD vf = XGetVideoFlags();
	const DWORD vs = XGetVideoStandard(); // XC_VIDEO_STANDARD_*

	const bool hasWS    = (vf & XC_VIDEO_FLAGS_WIDESCREEN)   != 0;
	const bool has480p  = (vf & XC_VIDEO_FLAGS_HDTV_480p)    != 0;
	const bool has720p  = (vf & XC_VIDEO_FLAGS_HDTV_720p)    != 0;
	const bool has1080i = (vf & XC_VIDEO_FLAGS_HDTV_1080i)   != 0;

	// Treat anything not NTSC-M as PAL family (PAL-I/M/N). We only need
	// this to decide the 576i PAL-50 fallback when 480p isn’t enabled.
	const bool isPAL = (vs != XC_VIDEO_STANDARD_NTSC_M);

	// Choose a backbuffer the TV actually supports.
	// Priority: 720p -> 1080i -> PAL-50 (576i) -> 480 (i/p depending on flags)
	if (has720p) {
		m_d3dpp.BackBufferWidth  = 1280;
		m_d3dpp.BackBufferHeight = 720;
		m_d3dpp.Flags            = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN; // 720p is always 16:9
		m_d3dpp.FullScreen_RefreshRateInHz = 60;
	}
	else if (has1080i) {
		m_d3dpp.BackBufferWidth  = 1920;
		m_d3dpp.BackBufferHeight = 1080;
		m_d3dpp.Flags            = D3DPRESENTFLAG_INTERLACED  | D3DPRESENTFLAG_WIDESCREEN; // 1080i is always 16:9
		m_d3dpp.FullScreen_RefreshRateInHz = 60;
	}
	else if (isPAL && !has480p) {
		// PAL-50: 576i
		m_d3dpp.BackBufferWidth  = 720;
		m_d3dpp.BackBufferHeight = 576;
		m_d3dpp.Flags            = D3DPRESENTFLAG_INTERLACED;
		if (hasWS) m_d3dpp.Flags |= D3DPRESENTFLAG_WIDESCREEN; // 16:9 if user enabled
		m_d3dpp.FullScreen_RefreshRateInHz = 50;
	}
	else {
		// 480 path (NTSC or PAL-60/480p)
		m_d3dpp.BackBufferWidth  = 640;
		m_d3dpp.BackBufferHeight = 480;
		m_d3dpp.Flags            = 0;
		if (has480p) m_d3dpp.Flags |= D3DPRESENTFLAG_PROGRESSIVE; // 480p if enabled
		else          m_d3dpp.Flags |= D3DPRESENTFLAG_INTERLACED;  // otherwise 480i
		if (hasWS)    m_d3dpp.Flags |= D3DPRESENTFLAG_WIDESCREEN;  // 16:9 anamorphic if enabled
		m_d3dpp.FullScreen_RefreshRateInHz = 60;                   // NTSC & PAL-60
	}

	// Common params
	m_d3dpp.BackBufferFormat       = D3DFMT_X8R8G8B8;
	m_d3dpp.SwapEffect             = D3DSWAPEFFECT_DISCARD;
	m_d3dpp.EnableAutoDepthStencil = TRUE;
	m_d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;
	// -------------------------------------------------------------------------

    m_mode=MODE_BROWSE;
    m_status[0]=0; m_statusUntilMs=0;
    m_dvdUsedBytes  = 0;
    m_dvdTotalBytes = 0;
    m_dvdHaveStats  = 0;
}




// ----- draw prims ------------------------------------------------------------
// Small wrapper to match project primitive helpers.
void FileBrowserApp::DrawRect(float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(m_pd3dDevice, x, y, w, h, c);
}
// --- Exit helper ------------------------------------------------------------
// Jump to dashboard via XLaunchNewImage (fallback)
void FileBrowserApp::ExitNow(){
    XLaunchNewImage(NULL, NULL); // returns to dashboard
}

// ----- status ---------------------------------------------------------------
// Set a 3s status toast at the footer (printf-style).
void FileBrowserApp::SetStatus(const char* fmt, ...){
    va_list ap; va_start(ap, fmt);
    _vsnprintf(m_status, sizeof(m_status), fmt, ap);
    va_end(ap);
    m_status[sizeof(m_status)-1]=0;
    m_statusUntilMs = GetTickCount() + 3000; // show for about 3s
}

// Append the last Win32 error to a prefix and show as status.
void FileBrowserApp::SetStatusLastErr(const char* prefix){
    DWORD e = GetLastError();
    char msg[64]; _snprintf(msg, sizeof(msg), "%s (err=%lu)", prefix, (unsigned long)e);
    msg[sizeof(msg)-1]=0;
    SetStatus("%s", msg);
}

// ----- listing helpers ------------------------------------------------------
// Refresh a pane while trying to preserve selection and scroll.
void FileBrowserApp::RefreshPane(Pane& p){
    if (p.mode==1){
        int prevSel   = p.sel;
        int prevScroll= p.scroll;

        ListDirectory(p.curPath, p.items);

        if (prevSel >= (int)p.items.size()) prevSel = (int)p.items.size()-1;
        if (prevSel < 0) prevSel = 0;
        p.sel = prevSel;

        int maxScroll = MaxI(0, (int)p.items.size() - m_visible);
        if (prevScroll > maxScroll) prevScroll = maxScroll;
        if (prevScroll < 0) prevScroll = 0;
        p.scroll = prevScroll;
    } else {
        // Drive list mode: re-enumerate mounted roots
        BuildDriveItems(p.items);
        if (p.sel >= (int)p.items.size()) p.sel = (int)p.items.size()-1;
        if (p.sel < 0) p.sel = 0;
        p.scroll = 0;
    }
}

// Resolve the destination directory for copy/move based on the other pane.
// Returns normalized path with trailing slash in outDst on success.
bool FileBrowserApp::ResolveDestDir(char* outDst, size_t cap){
    Pane& dst = m_pane[1 - m_active];
    outDst[0] = 0;

    if (dst.mode == 1) {
        _snprintf(outDst, (int)cap, "%s", dst.curPath);
        outDst[cap-1]=0;
        NormalizeDirA(outDst);
        return true;
    }
    // If the other pane is the drive list, allow selecting a drive as destination.
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

// Select an item in a pane by name and adjust scroll to reveal it.
void FileBrowserApp::SelectItemInPane(Pane& p, const char* name){
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

// ----- context menu (delegated) ---------------------------------------------
// Pass-through to the ContextMenu component.
void FileBrowserApp::AddMenuItem(const char* label, Action act, bool enabled){
    m_ctx.AddItem(label, act, enabled);
}

// Build the context menu based on current mode and selection.
void FileBrowserApp::BuildContextMenu(){
    Pane& p   = m_pane[m_active];
    bool inDir  = (p.mode == 1);
    bool hasSel = !p.items.empty();
    int  marked = 0; for (size_t i=0; i<p.items.size(); ++i) if (p.items[i].marked) ++marked;

    m_ctx.Clear();

    // Primary action label depends on context (Open vs Launch for .xbe).
    if (hasSel) {
        const Item& cur = p.items[p.sel];
        bool showPrimary = false;
        const char* primaryLabel = "Open";
        if (p.mode == 0) { showPrimary = true; primaryLabel = "Open"; }
        else if (!cur.isUpEntry) {
            if (cur.isDir) { showPrimary = true; primaryLabel = "Open"; }
            else if (HasXbeExt(cur.name)) { showPrimary = true; primaryLabel = "Launch"; }
        }
        if (showPrimary) AddMenuItem(primaryLabel, ACT_OPEN, true);
    }

    // Common operations
    AddMenuItem("Copy",            ACT_COPY,        hasSel);
    AddMenuItem("Move",            ACT_MOVE,        hasSel);
    AddMenuItem("Delete",          ACT_DELETE,      hasSel);
    AddMenuItem("Rename",          ACT_RENAME,      hasSel);
    AddMenuItem("Make new folder", ACT_MKDIR,       inDir);
    AddMenuItem("Calculate size",  ACT_CALCSIZE,    hasSel);
    AddMenuItem("Go to root",      ACT_GOROOT,      inDir);
    AddMenuItem("Switch pane",     ACT_SWITCHMEDIA, true);

    // Marking tools (directory mode only; skip the ".." row)
    if (inDir) {
        int selectable = 0;
        for (size_t i=0;i<p.items.size(); ++i) if (!p.items[i].isUpEntry) ++selectable;
        if (selectable > 0) {
            AddMenuItem("Mark all",     ACT_MARK_ALL,     true);
            AddMenuItem("Invert marks", ACT_INVERT_MARKS, true);
        }
    }
    if (marked) AddMenuItem("Clear marks", ACT_CLEAR_MARKS, true);

    // Bottom-only item on drive list: destructive cache format.
    if (p.mode == 0) {
        m_ctx.AddSeparator();                               // visual separator (non-selectable)
        AddMenuItem("Format cache (X/Y/Z)", ACT_FORMAT_CACHE, true);
    }
}

// Open and close the menu; switch mode so browse input pauses.
void FileBrowserApp::OpenMenu(){
    BuildContextMenu();

    const FLOAT menuW = 340.0f;
    const FLOAT rowH  = kLineH + 6.0f;

    // Center the popup over the active pane.
    const FLOAT paneX = (m_active==0) ? kListX_L : (kListX_L + kListW + kPaneGap);
    const FLOAT x = paneX + (kListW - menuW)*0.5f;
    const FLOAT y = kListY + 20.0f;

    m_ctx.OpenAt(x, y, menuW, rowH);
    m_mode=MODE_MENU;
}
void FileBrowserApp::CloseMenu(){
    m_ctx.Close();
    if (m_mode==MODE_MENU) m_mode=MODE_BROWSE;
}

// ----- rename lifecycle (OnScreenKeyboard) ----------------------------------
// Start rename using the OSD keyboard. Close menu first so the A press
// that opened the keyboard does not trigger other UI.
void FileBrowserApp::BeginRename(const char* parentDir, const char* oldName){
    m_ctx.Close();             // ensure menu closes
    m_kb.Open(parentDir ? parentDir : "", oldName ? oldName : "");
    m_mode = MODE_RENAME;
}
void FileBrowserApp::CancelRename(){
    m_kb.Close();
    m_mode=MODE_BROWSE;
}
void FileBrowserApp::AcceptRename(){
    const char* newName = m_kb.Buffer();
    if (!newName) { CancelRename(); return; }

    Pane& ap = m_pane[m_active];
    if (ap.mode != 1 || ap.items.empty()) { SetStatus("Rename failed: no selection"); CancelRename(); return; }

    const Item& sel = ap.items[ap.sel];
    if (sel.isUpEntry) { SetStatus("Rename failed: invalid selection"); CancelRename(); return; }

    // Sanitize to FATX-friendly, then MoveFile within current dir.
    char clean[256]; _snprintf(clean, sizeof(clean), "%s", newName); clean[sizeof(clean)-1]=0;
    SanitizeFatxNameInPlace(clean);
    if (_stricmp(clean, sel.name)==0){ SetStatus("No change"); CancelRename(); return; }

    char oldPath[512]; JoinPath(oldPath, sizeof(oldPath), ap.curPath, sel.name);
    char newPath[512]; JoinPath(newPath, sizeof(newPath), ap.curPath, clean);

    if (MoveFileA(oldPath, newPath)){
        SetStatus("Renamed to %s", clean);
        RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
        SelectItemInPane(ap, clean);
    }else{
        SetStatusLastErr("Rename failed");
        RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
    }
    CancelRename();
}

// Absorb the current pad state so the button used to accept/cancel the keyboard
// does not fall through and act in browse/menu mode.
void FileBrowserApp::AbsorbPadState(const XBGAMEPAD& pad){
    m_prevButtons = pad.wButtons;
    m_prevA       = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    m_prevB       = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    m_prevX       = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    m_prevY       = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
    m_prevWhite   = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    m_prevBlack   = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
}

// ----- input: rename modal --------------------------------------------------
// While the keyboard is active, route inputs to it only.
void FileBrowserApp::OnPad_Rename(const XBGAMEPAD& pad){
    OnScreenKeyboard::Result r = m_kb.OnPad(pad);

    if (r == OnScreenKeyboard::ACCEPTED){
        AbsorbPadState(pad);
        AcceptRename();
        return;
    }
    if (r == OnScreenKeyboard::CANCELED){
        AbsorbPadState(pad);
        CancelRename();
        return;
    }

    // Still in keyboard: keep edge state roughly synced
    AbsorbPadState(pad);
}


// ----- input: context menu (delegated) -------------------------------------
// Feed pad to the menu; on selection, execute via central AppActions.
void FileBrowserApp::OnPad_Menu(const XBGAMEPAD& pad){
    Action act;
    ContextMenu::Result r = m_ctx.OnPad(pad, act);
    if (r == ContextMenu::CHOSEN){
        //SetStatus("Chosen action=%d", (int)act);   // debug toast
        AppActions::Execute(act, *this);          // perform action
        CloseMenu();
    } else if (r == ContextMenu::CLOSED){
        CloseMenu();
    }

    // Sync edge state so the next browse frame does not double-fire.
    m_prevButtons = pad.wButtons;
    m_prevA = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    m_prevB = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    m_prevX = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
}

// ----- input: browse mode (with auto-repeat) -------------------------------
// Main file-list navigation. Handles dpad/analog, paging, selection,
// pane switching, and quick mark/unmark on Y.
void FileBrowserApp::OnPad_Browse(const XBGAMEPAD& pad){
    const DWORD btn = pad.wButtons;
    Pane& p = m_pane[m_active];

    // up/down auto-repeat with initial delay
    const DWORD now   = GetTickCount();
    const int   kDead = 16000; // analog deadzone for vertical stick
    const DWORD kInit = 230;   // initial delay (ms)
    const DWORD kRep  = 120;   // repeat rate (ms)

    // Compute desired move: -1 up, +1 down, 0 none.
    int ud = 0;
    if ((btn & XINPUT_GAMEPAD_DPAD_UP)   || pad.sThumbLY >  kDead) ud = -1;
    if ((btn & XINPUT_GAMEPAD_DPAD_DOWN) || pad.sThumbLY < -kDead) ud = +1;

    // Repeat-state machine for smooth scrolling.
    if (ud == 0){
        m_navUDHeld = false; m_navUDDir = 0;
    }else{
        if (!m_navUDHeld || m_navUDDir != ud){
            // first step
            if (ud < 0){
                if (p.sel > 0){ --p.sel; if (p.sel < p.scroll) p.scroll = p.sel; }
            }else{
                int maxSel = (int)p.items.size() - 1;
                if (p.sel < maxSel){ ++p.sel; if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1); }
            }
            m_navUDHeld = true; m_navUDDir = ud; m_navUDNext = now + kInit;
        }else if (now >= m_navUDNext){
            // repeats
            if (ud < 0){
                if (p.sel > 0){ --p.sel; if (p.sel < p.scroll) p.scroll = p.sel; }
            }else{
                int maxSel = (int)p.items.size() - 1;
                if (p.sel < maxSel){ ++p.sel; if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1); }
            }
            m_navUDNext = now + kRep;
        }
    }

    // pane switch via dpad left/right (edge-detected)
    bool leftTrig  = (btn & XINPUT_GAMEPAD_DPAD_LEFT)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_LEFT);
    bool rightTrig = (btn & XINPUT_GAMEPAD_DPAD_RIGHT) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
    if (leftTrig)  m_active = 0;
    if (rightTrig) m_active = 1;

    // Other buttons (edge-detected)
    unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    unsigned char x = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    unsigned char y = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
    unsigned char w = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE]; // page down
    unsigned char k = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK]; // page up

    bool aTrig = (a > 30 && m_prevA <= 30);
    bool bTrig = (b > 30 && m_prevB <= 30);
    bool xTrig = (x > 30 && m_prevX <= 30);
    bool yTrig = (y > 30 && m_prevY <= 30);
    bool wTrig = (w > 30 && m_prevWhite <= 30);
    bool kTrig = (k > 30 && m_prevBlack <= 30);

    // Open context menu on X
    if (xTrig) {
        OpenMenu();
        // absorb state so the X press does not leak into menu
        m_prevButtons = btn;
        m_prevA = a; m_prevB = b; m_prevX = x;
        m_prevWhite = w; m_prevBlack = k;
        return;
    }

    // A = enter, B = up one
    if (aTrig) EnterSelection(p);
    if (bTrig) UpOne(p);

    // Black/White = page up/down
    if (kTrig){ // BLACK: page up
        p.sel -= m_visible; if (p.sel < 0) p.sel = 0;
        if (p.sel < p.scroll) p.scroll = p.sel;
    }
    if (wTrig){ // WHITE: page down
        int maxSel = (int)p.items.size()-1;
        p.sel += m_visible; if (p.sel > maxSel) p.sel = maxSel;
        if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1);
    }

    // Y = quick toggle mark (skips the ".." entry)
    if (yTrig) {
        if (p.mode==1 && !p.items.empty()) {
            Item& it = p.items[p.sel];
            if (!it.isUpEntry) {
                it.marked = !it.marked;
                SetStatus(it.marked ? "Marked" : "Unmarked");

                // Optional auto-advance to speed through marking multiple items.
                int maxSel = (int)p.items.size()-1;
                if (p.sel < maxSel) {
                    ++p.sel;
                    if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1);
                }
            }
        }
    }

    // Save edge baselines for next frame.
    m_prevButtons = btn;
    m_prevA = a; m_prevB = b; m_prevX = x; m_prevY = y;
    m_prevWhite = w; m_prevBlack = k;
}

// ----- input router ---------------------------------------------------------
// Route pad to sub-handlers based on current modal state.
void FileBrowserApp::OnPad(const XBGAMEPAD& pad){
    
	// --- Back-to-exit (press Back twice) -----------------------------------
	const bool backNow  = (pad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
	const bool backTrig = backNow && !(m_prevButtons & XINPUT_GAMEPAD_BACK);
	DWORD now = GetTickCount();

	if (backTrig){
		if (m_backConfirmArmed && now < m_statusUntilMs){
			ExitNow();
			return;
		} else {
			m_backConfirmArmed = true;
			SetStatus("Press Back again to exit");
			m_backConfirmUntil = m_statusUntilMs; // optional snapshot
		}
	}

	if (m_backConfirmArmed && now >= m_statusUntilMs){
		m_backConfirmArmed = false;
	}

    // -----------------------------------------------------------------------

    if (m_mode == MODE_RENAME){ OnPad_Rename(pad); return; }
    if (m_mode == MODE_MENU)  { OnPad_Menu(pad);   return; }
    OnPad_Browse(pad);
}


// Per-frame app logic. Also poll for drive-set changes and refresh panes.
HRESULT FileBrowserApp::FrameMove(){
    XBInput_GetInput();

    // --- Poll for general drive-set changes (ignore D:) ----------------------
    {
        static DWORD        s_nextPollMs  = 0;
        static unsigned int s_lastMaskNoD = 0xFFFFFFFF; // sentinel = uninitialized

        DWORD now = GetTickCount();
        if (now >= s_nextPollMs) {
            s_nextPollMs = now + 1200; // ~1.2s

            const unsigned int dBit = (1u << ('D' - 'A'));
            unsigned int mask    = QueryDriveMaskAZ();    // A:..Z:
            unsigned int maskNoD = (mask & ~dBit);      // exclude D:

            if (s_lastMaskNoD == 0xFFFFFFFF) {
                s_lastMaskNoD = maskNoD;               // prime (no toast)
            } else if (maskNoD != s_lastMaskNoD) {
                s_lastMaskNoD = maskNoD;

                RescanDrives();                         // non-D changes
                EnsureListing(m_pane[0]);
                EnsureListing(m_pane[1]);

                // comment out if you never want this toast:
                 SetStatus("Drives refreshed");
            }
        }
    }

    // --- DVD tray/media polling + serial watchdog (no stale listings) ----------
{
    static DWORD s_nextDvdPollMs = 0;
    static DWORD s_lastDvdSerial = 0xFFFFFFFF; // unknown
    static BOOL  s_dMapped       = FALSE;      // our belief about D: mapping

    DWORD now2 = GetTickCount();
    if (now2 >= s_nextDvdPollMs) {
        s_nextDvdPollMs = now2 + 250; // ~4 Hz

        DWORD code = DvdGetDriveStateOneShot(); // DRIVE_* or DRIVE_READY (no change)
        if (code != DRIVE_READY) {
            BOOL needRefresh = FALSE;

            switch (code) {
            case DRIVE_OPEN:
            case DRIVE_CLOSED_NO_MEDIA:
                DvdUnmap_Io();
                s_dMapped       = FALSE;
                s_lastDvdSerial = 0xFFFFFFFF;   // forget old serial
				m_dvdHaveStats  = false;          // <— clear
				m_dvdUsedBytes  = 0;
				m_dvdTotalBytes = 0;
                needRefresh     = TRUE;
                if (code == DRIVE_OPEN) SetStatus("DVD: Tray Open");
                else                     SetStatus("DVD: No Disc");
                break;

            case DRIVE_CLOSED_MEDIA_PRESENT: {
				DvdColdRemount();
				s_dMapped = TRUE;

				DWORD curSer = 0;
				if (GetDvdVolumeSerial(&curSer)) s_lastDvdSerial = curSer;

				// Cache total (capacity) and used (sum of files on disc)
				ULONGLONG fb=0, tb=0;
				GetDriveFreeTotal("D:\\", fb, tb);
				m_dvdTotalBytes = tb;
				m_dvdUsedBytes  = DirSizeRecursiveA("D:\\");   // from FsUtil
				m_dvdHaveStats  = true;

				{ char lbl[64]; if (DvdDetectMediaSimple(lbl, sizeof(lbl))) SetStatus("%s", lbl); }
				needRefresh = TRUE;
				break; }


            case DRIVE_NOT_READY:
            default:
                break;
            }

            if (needRefresh) {
                RescanDrives();

                // Refresh both panes; bounce out of D:\ if it vanished
                for (int iPane = 0; iPane < 2; ++iPane) {
                    Pane& P = m_pane[iPane];

                    if (P.mode == 0) {
                        BuildDriveItems(P.items);
                        if (P.sel >= (int)P.items.size()) P.sel = (int)P.items.size()-1;
                        if (P.sel < 0) P.sel = 0;
                        P.scroll = 0;
                    } else if (IsDPath(P.curPath)) {
                        if (s_dMapped) {
                            // HARD re-list so a new disc shows correct files
                            ListDirectory(P.curPath, P.items);
                            if (P.sel >= (int)P.items.size()) P.sel = (int)P.items.size()-1;
                            if (P.sel < 0) P.sel = 0;
                            if (P.scroll > P.sel) P.scroll = P.sel;
                            { int maxScroll = (int)P.items.size() - m_visible; if (maxScroll < 0) maxScroll = 0; if (P.scroll > maxScroll) P.scroll = maxScroll; }
                        } else {
                            // D: gone => drive list
                            P.mode = 0; P.curPath[0] = 0;
                            BuildDriveItems(P.items);
                            P.sel = 0; P.scroll = 0;
                        }
                    } else {
                        RefreshPane(P);
                    }
                }
            }
        }
    }

    // Serial watchdog: catches fast swaps if tray state change was missed
    {
        static DWORD s_nextSerChk = 0;
        DWORD now3 = GetTickCount();
        if (now3 >= s_nextSerChk) {
            s_nextSerChk = now3 + 800; // ~1.25 Hz

            if (s_dMapped) {
                DWORD curSer = 0;
                if (GetDvdVolumeSerial(&curSer)) {
                    if (s_lastDvdSerial != 0xFFFFFFFF && curSer != s_lastDvdSerial) {
                        // New disc detected silently — force remount and refresh
                        DvdColdRemount();
                        s_lastDvdSerial = curSer;
						// refresh cached used/total
						ULONGLONG fb=0, tb=0;
						GetDriveFreeTotal("D:\\", fb, tb);
						m_dvdTotalBytes = tb;
						m_dvdUsedBytes  = DirSizeRecursiveA("D:\\");
						m_dvdHaveStats  = true;


                        RescanDrives();
                        for (int iPane = 0; iPane < 2; ++iPane) {
                            Pane& P = m_pane[iPane];
                            if (P.mode == 0) {
                                BuildDriveItems(P.items);
                                if (P.sel >= (int)P.items.size()) P.sel = (int)P.items.size()-1;
                                if (P.sel < 0) P.sel = 0;
                                P.scroll = 0;
                            } else if (IsDPath(P.curPath)) {
                                ListDirectory(P.curPath, P.items);
                                if (P.sel >= (int)P.items.size()) P.sel = (int)P.items.size()-1;
                                if (P.sel < 0) P.sel = 0;
                                if (P.scroll > P.sel) P.scroll = P.sel;
                                { int maxScroll = (int)P.items.size() - m_visible; if (maxScroll < 0) maxScroll = 0; if (P.scroll > maxScroll) P.scroll = maxScroll; }
                            } else {
                                RefreshPane(P);
                            }
                        }

                        SetStatus("DVD: Media changed");
                    }
                }
            }
        }
    }
}

    OnPad(g_Gamepads[0]);
    return S_OK;
}





// ----- draw: menu / rename / panes -----------------------------------------
// Draw the context menu if open (coordinates set in OpenMenu).
void FileBrowserApp::DrawMenu(){
    if (!m_ctx.IsOpen()) return;
    m_ctx.Draw(m_font, m_pd3dDevice);
}

// Draw OSD keyboard if active.
void FileBrowserApp::DrawRename(){
    if (!m_kb.Active()) return;
    m_kb.Draw(m_font, m_pd3dDevice, kLineH);
}

// ----- listing / navigation -------------------------------------------------
// Ensure a pane has items and indices are in range (after mode/path changes).
void FileBrowserApp::EnsureListing(Pane& p){
    if (p.mode==0) BuildDriveItems(p.items);
    else           ListDirectory(p.curPath, p.items);

    if (p.sel >= (int)p.items.size()) p.sel = (int)p.items.size()-1;
    if (p.sel < 0) p.sel = 0;

    if (p.scroll > p.sel) p.scroll = p.sel;
    int maxScroll = MaxI(0, (int)p.items.size() - m_visible);
    if (p.scroll > maxScroll) p.scroll = maxScroll;
}

// Enter current selection (drive -> directory, ".." -> up, dir -> descend,
// .xbe -> launch). Other files are no-op here.
void FileBrowserApp::EnterSelection(Pane& p){
    if (p.items.empty()) return;
    const Item& it = p.items[p.sel];

    // Drive list -> go into the chosen drive.
    if (p.mode==0){
        strncpy(p.curPath,it.name,sizeof(p.curPath)-1); p.curPath[sizeof(p.curPath)-1]=0;
        p.mode=1; p.sel=0; p.scroll=0; ListDirectory(p.curPath,p.items); return;
    }

    // Directory listing
	if (it.isUpEntry){
		// Reselect the folder we’re leaving (same as UpOne)
		char childName[256]; ExtractLastComponent(p.curPath, childName, sizeof(childName));

		if (strlen(p.curPath) <= 3){
			char driveRoot[4] = { p.curPath[0], ':', '\\', 0 };
			p.mode = 0;
			p.sel = 0; p.scroll = 0;
			BuildDriveItems(p.items);
			for (int i=0; i<(int)p.items.size(); ++i){
				if (_stricmp(p.items[i].name, driveRoot) == 0){ p.sel = i; break; }
			}
			if (p.sel < p.scroll) p.scroll = p.sel;
			if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1);
			p.curPath[0] = 0;
		} else {
			ParentPath(p.curPath);
			p.sel = 0; p.scroll = 0;
			ListDirectory(p.curPath, p.items);
			SelectItemInPane(p, childName);
		}
		return;
	}
    if (it.isDir){
        // Descend into subdirectory.
        char next[512]; JoinPath(next,sizeof(next),p.curPath,it.name);
        strncpy(p.curPath,next,sizeof(p.curPath)-1); p.curPath[sizeof(p.curPath)-1]=0;
        p.sel=0; p.scroll=0; ListDirectory(p.curPath,p.items); return;
    }

    // Files: launch .xbe if selected (other file types are no-op here).
    if (!it.isDir && !it.isUpEntry) {
        if (HasXbeExt(it.name)) {
            char full[512];
            JoinPath(full, sizeof(full), p.curPath, it.name);

            // Small Present for a snappy visual handoff before XLaunchNewImageA.
            m_pd3dDevice->Present(NULL, NULL, NULL, NULL);
            Sleep(10);

            if (!LaunchXbeA(full)) {
                SetStatusLastErr("Launch failed");
            }
        }
        return;
    }
}

// Move up one level; from root goes back to drive list.
void FileBrowserApp::UpOne(Pane& p){
    if (p.mode==0) return;

    // Name of the child we’re currently inside (to reselect in parent)
    char childName[256]; ExtractLastComponent(p.curPath, childName, sizeof(childName));

    // If at drive root, go back to drive list and select that drive
    if (strlen(p.curPath) <= 3){
        char driveRoot[4] = { p.curPath[0], ':', '\\', 0 };
        p.mode = 0;
        p.sel = 0; p.scroll = 0;
        BuildDriveItems(p.items);

        // Try to select the drive we came from
        for (int i=0; i<(int)p.items.size(); ++i){
            if (_stricmp(p.items[i].name, driveRoot) == 0){ p.sel = i; break; }
        }
        if (p.sel < p.scroll) p.scroll = p.sel;
        if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1);

        p.curPath[0] = 0;
        return;
    }

    // Go to parent and select the child folder we just left
    ParentPath(p.curPath);
    p.sel = 0; p.scroll = 0;
    ListDirectory(p.curPath, p.items);
    SelectItemInPane(p, childName);
}


// ----- Initialize / Render --------------------------------------------------
// Create font, input, initial drive mapping, and compute responsive layout.
HRESULT FileBrowserApp::Initialize(){
    if (FAILED(m_font.Create("D:\\Media\\Font.xpr", 0))) {
        m_font.Create("D:\\Media\\CourierNew.xpr", 0);
    }

    XBInput_CreateGamepads();
    MapStandardDrives_Io();
    RescanDrives();
    BuildDriveItems(m_pane[0].items);
    BuildDriveItems(m_pane[1].items);

    // Layout derived from current backbuffer size (works for any resolution)
    ComputeResponsiveLayout();

    return S_OK;
}


// ----- progress overlay API -------------------------------------------------
// Begin a copy/move progress session (title and first label for marquee).
void FileBrowserApp::BeginProgress(ULONGLONG total, const char* firstLabel, const char* title) {
    m_prog.active = true;
    m_prog.done   = 0;
    m_prog.total  = total;

    _snprintf(m_prog.current, sizeof(m_prog.current), "%s", firstLabel ? firstLabel : "");
    m_prog.current[sizeof(m_prog.current)-1] = 0;

    _snprintf(m_prog.title, sizeof(m_prog.title), "%s", title ? title : "Working...");
    m_prog.title[sizeof(m_prog.title)-1] = 0;

    m_prog.lastPaintMs = 0;
}

// Update the overlay counters and optional label; throttles repaint to ~25 fps.
void FileBrowserApp::UpdateProgress(ULONGLONG done, ULONGLONG total, const char* label){
    m_prog.done  = done;
    m_prog.total = (total ? total : m_prog.total);
    if (label){
        _snprintf(m_prog.current, sizeof(m_prog.current), "%s", label);
        m_prog.current[sizeof(m_prog.current)-1] = 0;
    }

    DWORD now = GetTickCount();
    if (now - m_prog.lastPaintMs >= 40){
        m_prog.lastPaintMs = now;

        // redraw one frame (use the normal Render path)
        Render();
        Sleep(0);
    }
}

// End and clear the progress overlay state.
void FileBrowserApp::EndProgress(){
    m_prog.active = false;
}

// Draw the modal progress HUD (centered panel with marquee and bar).
void FileBrowserApp::DrawProgressOverlay(){
    if (!m_prog.active) return;

    D3DVIEWPORT8 vp; m_pd3dDevice->GetViewport(&vp);

    // Ensure filename line has room for about 42 glyphs at current font size
    char fortyTwo[64]; for (int i=0;i<42;++i) fortyTwo[i]='W'; fortyTwo[42]=0;
    FLOAT fileW=0, fileH=0; MeasureTextWH(m_font, fortyTwo, fileW, fileH);

    const FLOAT margin = 18.0f;
    FLOAT w = MaxF(MaxF(420.0f, vp.Width * 0.50f), fileW + margin*2.0f);
    if (w > vp.Width - margin*2.0f) w = vp.Width - margin*2.0f;

    const FLOAT h = 116.0f;
    const FLOAT x = Snap((vp.Width  - w)*0.5f);
    const FLOAT y = Snap((vp.Height - h)*0.5f);

    DrawRect(x-6, y-6, w+12, h+12, 0xA0101010);
    DrawRect(x,   y,   w,    h,    0xE0222222);

    // layout lines
    const FLOAT titleY  = y + 10.0f;
    const FLOAT folderY = titleY + 24.0f;
    const FLOAT fileY   = folderY + 22.0f;
    const FLOAT barY    = fileY   + 26.0f;
    const FLOAT barH    = 20.0f;
    const FLOAT barX    = x + margin;
    const FLOAT barW    = w - margin*2.0f;

    // title
    DrawAnsi(m_font, x + margin, titleY, 0xFFFFFFFF, m_prog.title[0] ? m_prog.title : "Working...");

    // hint (right)
    {
        const char* hint = "B: Cancel";
        FLOAT hw=0, hh=0; MeasureTextWH(m_font, hint, hw, hh);
        DrawAnsi(m_font, x + w - margin - hw, titleY, 0xFFCCCCCC, hint);
    }

    // Split current label into folder + file parts
    const char* label = m_prog.current;
    const char* slash = label ? strrchr(label, '\\') : NULL;

    char folder[512] = {0};
    char file  [256] = {0};
    if (slash){
        size_t n = (size_t)(slash - label + 1);
        if (n >= sizeof(folder)) n = sizeof(folder)-1;
        memcpy(folder, label, n); folder[n] = 0;
        _snprintf(file, sizeof(file), "%s", slash + 1); file[sizeof(file)-1]=0;
    } else {
        _snprintf(file, sizeof(file), "%s", label ? label : ""); file[sizeof(file)-1]=0;
        folder[0] = 0;
    }

    // --- character-step marquees (like panes/OSK) ----------------------------
    static ProgMarquee s_marqFolder;
    static ProgMarquee s_marqFile;

    // If a new operation just started, clear state so we get the initial pause.
    if (m_prog.done == 0) { s_marqFolder = ProgMarquee(); s_marqFile = ProgMarquee(); }

    DrawLabelFittedOrMarquee(m_font, x + margin, folderY, barW, 0xFF5EA4FF, folder, s_marqFolder);
    DrawLabelFittedOrMarquee(m_font, x + margin, fileY,   barW, 0xFF89D07E, file,   s_marqFile);

    // progress percent and bar
    FLOAT pct = 0.0f;
    if (m_prog.total > 0){
        pct = (FLOAT)((double)m_prog.done / (double)m_prog.total);
        if (pct < 0) pct = 0; if (pct > 1) pct = 1;
    }

    DrawRect(barX, barY, barW,       barH, 0xFF0E0E0E);
    DrawRect(barX, barY, barW * pct, barH, 0x90FFFF00);

    char t[32]; _snprintf(t, sizeof(t), "%u%%", (unsigned int)(pct*100.0f + 0.5f)); t[sizeof(t)-1]=0;
    FLOAT tw=0, th=0; MeasureTextWH(m_font, t, tw, th);
    const FLOAT tx = Snap(barX + barW - tw);
    const FLOAT ty = Snap(barY + (barH - th) * 0.5f);
    DrawAnsi(m_font, tx, ty, 0xFFEEEEEE, t);
}

// ----- main render ----------------------------------------------------------
// Draw both panes, footer and hints, transient status, and any overlays.
HRESULT FileBrowserApp::Render(){
    m_pd3dDevice->Clear(0,NULL,D3DCLEAR_TARGET,0x20202020,1.0f,0);
    m_pd3dDevice->BeginScene();

    m_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pd3dDevice->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    m_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    // Recompute layout if resolution changes on the fly (safe & cheap)
    ComputeResponsiveLayout();

    // ---- panes ----
    PaneStyle st;
    st.listW       = kListW;
    st.listY       = kListY;
    st.lineH       = kLineH;
    st.hdrY        = kHdrY;
    st.hdrH        = kHdrH;
    st.hdrW        = kHdrW;         // header exactly equals pane width
    st.gutterW     = kGutterW;
    st.paddingX    = kPaddingX;
    st.scrollBarW  = kScrollBarW;
    st.visibleRows = m_visible;

	// ---- panes ----
	PaneRenderer::BeginFrameSharedCols();                         // reset per frame
	m_renderer.PrimeSharedSizeColW(m_font, m_pane[0], st);        // prime from left
	m_renderer.PrimeSharedSizeColW(m_font, m_pane[1], st);        // prime from right

	// Symmetric base X for panes: [margin] [left] [gap] [right] [margin]
	const FLOAT leftX  = kListX_L;
	const FLOAT rightX = kListX_L + kListW + kPaneGap;

	// Draw in order (left then right)
	m_renderer.DrawPane(m_font, m_pd3dDevice, leftX,  m_pane[0], m_active==0, st, 0);
	m_renderer.DrawPane(m_font, m_pd3dDevice, rightX, m_pane[1], m_active==1, st, 1);


    // ---- footer / hints ----
    D3DVIEWPORT8 vp2; m_pd3dDevice->GetViewport(&vp2);

    // Footer matches the combined pane area (two panes + gap), centered.
	const FLOAT footerMargin = MaxF(10.0f, vp2.Width * 0.01f);
	const FLOAT footerW      = MinF(kHdrW * 2.0f + kPaneGap, (FLOAT)vp2.Width - footerMargin * 2.0f);
	const FLOAT footerX      = floorf(((FLOAT)vp2.Width - footerW) * 0.5f);
	const FLOAT footerY      = (FLOAT)vp2.Height - FooterBandPx((FLOAT)vp2.Height);  // <-- unified


    // footer bar
    DrawRect(footerX, footerY, footerW, 28.0f, 0x802A2A2A);

    // --- (unchanged) footer text building and status toast follow here ---
    {
        const Pane& ap   = m_pane[m_active];
        const Item* cur  = (ap.items.empty()? NULL : &ap.items[ap.sel]);
        const char* yLab = (cur && !cur->isUpEntry && cur->marked) ? "Unmark" : "Mark";

		const bool isLowRes = (vp2.Height < 700); // 480i/p or 576i count as "small"
		const bool smallFooter = (isLowRes || footerW <= 620.0f);

        if (m_pane[m_active].mode == 0){
            const char* hintsVerbose = "D-Pad: Move  |  Left/Right: Switch pane  |  A: Enter  |  X: Menu  |  Black/White: Page";
            const char* hintsCompact = "DPad:Move | L/R:Pane | A:Enter | X:Menu | Pg:Blk/Wht";
            const char* base = smallFooter ? hintsCompact : hintsVerbose;

            char fitted[256];
            LeftEllipsizeToFit(m_font, base, footerW - 10.0f, fitted, sizeof(fitted));
            DrawAnsiCenteredX(m_font, footerX, footerW, footerY + 4.0f, 0xFFCCCCCC, fitted);
        } else {
            const char* curPath = m_pane[m_active].curPath;
            char       leftLabel[16] = "Free";
            ULONGLONG  leftVal = 0, rightVal = 0;
            if (IsDPath(curPath) && m_dvdHaveStats) {
                strcpy(leftLabel, "Size");
                leftVal  = m_dvdUsedBytes;
                rightVal = m_dvdTotalBytes;
            } else {
                ULONGLONG fb=0, tb=0; GetDriveFreeTotal(curPath, fb, tb);
                leftVal  = fb; rightVal = tb;
            }

            char leftStr[64], rightStr[64];
            FormatSize(leftVal,  leftStr,  sizeof(leftStr));
            FormatSize(rightVal, rightStr, sizeof(rightStr));

            char bar[420];
            if (smallFooter) {
                _snprintf(bar, sizeof(bar),
                    "Active:%s | B:Up | %s:%s/%s | X:Menu | Y:%s | Pg:Blk/Wht",
                    (m_active==0 ? "L" : "R"), leftLabel, leftStr, rightStr, yLab);
            } else {
                _snprintf(bar, sizeof(bar),
                    "Active: %s   |   B: Up   |   %s: %s / Total: %s   |   X: Menu   |   Y: %s   |   Black/White: Page",
                    (m_active==0 ? "Left" : "Right"), leftLabel, leftStr, rightStr, yLab);
            }
            bar[sizeof(bar)-1] = 0;

            char fitted[420];
            LeftEllipsizeToFit(m_font, bar, footerW - 10.0f, fitted, sizeof(fitted));
            DrawAnsiCenteredX(m_font, footerX, footerW, footerY + 4.0f, 0xFFCCCCCC, fitted);
        }

        // ---- status toast (also centered and fitted) ----
        DWORD now = GetTickCount();
        if (now < m_statusUntilMs && m_status[0]){
            char fitted[256];
            LeftEllipsizeToFit(m_font, m_status, footerW - 10.0f, fitted, sizeof(fitted));
            DrawAnsiCenteredX(m_font, footerX, footerW, footerY + 25.0f, 0xFFBBDDEE, fitted);
        }
    }

    // ---- overlays ----
    DrawMenu();
    DrawRename();
    DrawProgressOverlay();

    m_pd3dDevice->EndScene();
    m_pd3dDevice->Present(NULL,NULL,NULL,NULL);
    return S_OK;
}
