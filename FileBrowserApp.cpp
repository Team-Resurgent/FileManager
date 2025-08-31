#include "FileBrowserApp.h"
#include "AppActions.h"
#include "GfxPrims.h"
#include "FsUtil.h"
#include <wchar.h>
#include <stdarg.h>
#include <algorithm>
#include <stdio.h> // _snprintf

// ---- local helpers (no header pollution) ----
namespace {
    inline FLOAT MaxF(FLOAT a, FLOAT b){ return (a>b)?a:b; }
    inline int   MaxI (int a,   int b){ return (a>b)?a:b; }
    inline FLOAT Snap (FLOAT v){ return (FLOAT)((int)(v + 0.5f)); }

    inline void DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
        WCHAR wbuf[512];
        MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
        font.DrawText(x,y,color,wbuf,0,0.0f);
    }

    // NEW: center a single-line ANSI string within [left, left+width]
    inline void DrawAnsiCenteredX(CXBFont& font, FLOAT left, FLOAT width, FLOAT y, DWORD color, const char* text){
        WCHAR wbuf[512];
        MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
        FLOAT tw=0, th=0; font.GetTextExtent(wbuf, &tw, &th);
        const FLOAT x = Snap(left + (width - tw) * 0.5f);
        font.DrawText(x, y, color, wbuf, 0, 0.0f);
    }

	// measure ANSI text
	inline void MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH){
		WCHAR wbuf[512];
		MultiByteToWideChar(CP_ACP, 0, s ? s : "", -1, wbuf, 512);
		font.GetTextExtent(wbuf, &outW, &outH);
	}

	// left-ellipsize: keep the tail, prefix "..."
	inline void LeftEllipsizeToFit(CXBFont& font, const char* src, FLOAT maxW,
								char* out, size_t cap){
		if (!src) { out[0]=0; return; }
		FLOAT w=0,h=0; MeasureTextWH(font, src, w, h);
		if (w <= maxW){ _snprintf(out, (int)cap, "%s", src); out[cap-1]=0; return; }

		const char* tail = src + strlen(src);
		const char* p    = tail;
		char buf[1024];  // temp
		// grow suffix until it would overflow
		for (;;){
			if (p == src) break;
			--p;
			_snprintf(buf, sizeof(buf), "...%s", p);
			MeasureTextWH(font, buf, w, h);
			if (w > maxW){
				++p; // step back one char, previous was the last that fit
				break;
			}
		}
		_snprintf(out, (int)cap, "...%s", p);
		out[cap-1]=0;
	}

	// --- marquee helpers for progress overlay ---
	inline void MbToW(const char* s, WCHAR* w, int capW){
		MultiByteToWideChar(CP_ACP, 0, s ? s : "", -1, w, capW);
	}

	// Return pointer to the first byte that starts at (or just before) pixel offset px.
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

	
	inline unsigned int BuildDriveMask(){
        unsigned int mask = 0;
        for (char d='A'; d<='Z'; ++d){
            char root[4] = { d, ':', '\\', 0 };
            DWORD attr = GetFileAttributesA(root);
            if (attr != 0xFFFFFFFF) mask |= (1u << (d - 'A'));
        }
        return mask;
    }


}

FileBrowserApp::FileBrowserApp(){
    m_visible=13; m_prevA=0; m_prevB=0; m_prevX=0; m_prevY=0; m_active=0;
    m_prevButtons = 0; m_prevWhite = m_prevBlack = 0;
    m_navUDHeld=false; m_navUDDir=0; m_navUDNext=0;

    ZeroMemory(&m_d3dpp,sizeof(m_d3dpp));
    m_d3dpp.BackBufferWidth=1280; m_d3dpp.BackBufferHeight=720;
    m_d3dpp.BackBufferFormat=D3DFMT_X8R8G8B8; m_d3dpp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    m_d3dpp.FullScreen_RefreshRateInHz=60; m_d3dpp.EnableAutoDepthStencil=TRUE;
    m_d3dpp.AutoDepthStencilFormat=D3DFMT_D24S8;
    m_d3dpp.Flags = D3DPRESENTFLAG_PROGRESSIVE | D3DPRESENTFLAG_WIDESCREEN;

    m_mode=MODE_BROWSE;
    m_status[0]=0; m_statusUntilMs=0;
}

// ----- draw prims -----
void FileBrowserApp::DrawRect(float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(m_pd3dDevice, x, y, w, h, c);
}

// ----- status -----
void FileBrowserApp::SetStatus(const char* fmt, ...){
    va_list ap; va_start(ap, fmt);
    _vsnprintf(m_status, sizeof(m_status), fmt, ap);
    va_end(ap);
    m_status[sizeof(m_status)-1]=0;
    m_statusUntilMs = GetTickCount() + 3000; // 3s
}
void FileBrowserApp::SetStatusLastErr(const char* prefix){
    DWORD e = GetLastError();
    char msg[64]; _snprintf(msg, sizeof(msg), "%s (err=%lu)", prefix, (unsigned long)e);
    msg[sizeof(msg)-1]=0;
    SetStatus("%s", msg);
}

// ----- listing helpers -----
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
        BuildDriveItems(p.items);
        if (p.sel >= (int)p.items.size()) p.sel = (int)p.items.size()-1;
        if (p.sel < 0) p.sel = 0;
        p.scroll = 0;
    }
}

bool FileBrowserApp::ResolveDestDir(char* outDst, size_t cap){
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

// ----- context menu (delegated) -----
void FileBrowserApp::AddMenuItem(const char* label, Action act, bool enabled){
    m_ctx.AddItem(label, act, enabled);
}
void FileBrowserApp::BuildContextMenu(){
    Pane& p   = m_pane[m_active];
    bool inDir  = (p.mode == 1);
    bool hasSel = !p.items.empty();
    int  marked = 0; for (size_t i=0;i<p.items.size();++i) if (p.items[i].marked) ++marked;

    m_ctx.Clear();

    // Decide whether to show "Open" or "Launch" (or nothing)
    if (hasSel) {
        const Item& cur = p.items[p.sel];

        bool showPrimary = false;
        const char* primaryLabel = "Open";

        if (p.mode == 0) {
            // Drive list: always allow Open (items are drives)
            showPrimary = true;
            primaryLabel = "Open";
        } else if (!cur.isUpEntry) {
            if (cur.isDir) {
                showPrimary = true;
                primaryLabel = "Open";
            } else if (HasXbeExt(cur.name)) {
                showPrimary = true;
                primaryLabel = "Launch";
            }
        }

        if (showPrimary)
            AddMenuItem(primaryLabel, ACT_OPEN, true);
    }

    // The rest of your menu (unchanged)
    AddMenuItem("Copy",            ACT_COPY,       hasSel);
    AddMenuItem("Move",            ACT_MOVE,       hasSel);
    AddMenuItem("Delete",          ACT_DELETE,     hasSel);
    AddMenuItem("Rename",          ACT_RENAME,     hasSel);
    AddMenuItem("Make new folder", ACT_MKDIR,      inDir);
    AddMenuItem("Calculate size",  ACT_CALCSIZE,   hasSel);
    AddMenuItem("Go to root",      ACT_GOROOT,     inDir);
    AddMenuItem("Switch pane",     ACT_SWITCHMEDIA,true);

    if (inDir) {
        // count selectable (exclude "..")
        int selectable = 0;
        for (size_t i=0;i<p.items.size(); ++i) if (!p.items[i].isUpEntry) ++selectable;
        if (selectable > 0) {
            AddMenuItem("Mark all",     ACT_MARK_ALL,      true);
            AddMenuItem("Invert marks", ACT_INVERT_MARKS,  true);
        }
    }

    if (marked) AddMenuItem("Clear marks", ACT_CLEAR_MARKS, true);
}


void FileBrowserApp::OpenMenu(){
    BuildContextMenu();

    const FLOAT menuW = 340.0f;
    const FLOAT rowH  = kLineH + 6.0f;

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

// ----- rename lifecycle (delegated to OnScreenKeyboard) -----
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

// Absorb current pad state so the press that closes the keyboard (A/B/Start) doesn’t fall through into browse mode.
void FileBrowserApp::AbsorbPadState(const XBGAMEPAD& pad){
    m_prevButtons = pad.wButtons;
    m_prevA       = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    m_prevB       = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    m_prevX       = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    m_prevY       = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
    m_prevWhite   = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    m_prevBlack   = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
}

// ----- input: rename modal -----
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


// ----- input: context menu (delegated) -----
void FileBrowserApp::OnPad_Menu(const XBGAMEPAD& pad){
    Action act;
    ContextMenu::Result r = m_ctx.OnPad(pad, act);
    if (r == ContextMenu::CHOSEN){
        AppActions::Execute(act, *this);   // <-- call external actions
        CloseMenu();
    } else if (r == ContextMenu::CLOSED){
        CloseMenu();
    }

    // sync browse edge state to avoid carry-over triggers
    m_prevButtons = pad.wButtons;
    m_prevA = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    m_prevB = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    m_prevX = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
}

// ----- input: browse mode (with auto-repeat) -----
void FileBrowserApp::OnPad_Browse(const XBGAMEPAD& pad){
    const DWORD btn = pad.wButtons;
    Pane& p = m_pane[m_active];

    // up/down auto-repeat with initial delay
    const DWORD now   = GetTickCount();
    const int   kDead = 16000;
    const DWORD kInit = 230;
    const DWORD kRep  = 120;

    int ud = 0;
    if ((btn & XINPUT_GAMEPAD_DPAD_UP)   || pad.sThumbLY >  kDead) ud = -1;
    if ((btn & XINPUT_GAMEPAD_DPAD_DOWN) || pad.sThumbLY < -kDead) ud = +1;

    if (ud == 0){
        m_navUDHeld = false; m_navUDDir = 0;
    }else{
        if (!m_navUDHeld || m_navUDDir != ud){
            if (ud < 0){
                if (p.sel > 0){ --p.sel; if (p.sel < p.scroll) p.scroll = p.sel; }
            }else{
                int maxSel = (int)p.items.size() - 1;
                if (p.sel < maxSel){ ++p.sel; if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1); }
            }
            m_navUDHeld = true; m_navUDDir = ud; m_navUDNext = now + kInit;
        }else if (now >= m_navUDNext){
            if (ud < 0){
                if (p.sel > 0){ --p.sel; if (p.sel < p.scroll) p.scroll = p.sel; }
            }else{
                int maxSel = (int)p.items.size() - 1;
                if (p.sel < maxSel){ ++p.sel; if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1); }
            }
            m_navUDNext = now + kRep;
        }
    }

    // pane switch (edge)
    bool leftTrig  = (btn & XINPUT_GAMEPAD_DPAD_LEFT)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_LEFT);
    bool rightTrig = (btn & XINPUT_GAMEPAD_DPAD_RIGHT) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_RIGHT);
    if (leftTrig)  m_active = 0;
    if (rightTrig) m_active = 1;

    // other buttons (edge)
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

    if (xTrig) {
    OpenMenu();
    m_prevButtons = btn;
    m_prevA = a; m_prevB = b; m_prevX = x;
    m_prevWhite = w; m_prevBlack = k;
    return;
}

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

	if (yTrig) {
    if (p.mode==1 && !p.items.empty()) {
        Item& it = p.items[p.sel];
        if (!it.isUpEntry) {
            it.marked = !it.marked;
            SetStatus(it.marked ? "Marked" : "Unmarked");

            // (optional) auto-advance for quick marking
            int maxSel = (int)p.items.size()-1;
            if (p.sel < maxSel) {
                ++p.sel;
                if (p.sel >= p.scroll + m_visible) p.scroll = p.sel - (m_visible - 1);
            }
        }
    }
}

    m_prevButtons = btn;
    m_prevA = a; m_prevB = b; m_prevX = x; m_prevY = y;
    m_prevWhite = w; m_prevBlack = k;
}



// ----- input router -----
void FileBrowserApp::OnPad(const XBGAMEPAD& pad){
    if (m_mode == MODE_RENAME){ OnPad_Rename(pad); return; }
    if (m_mode == MODE_MENU)  { OnPad_Menu(pad);   return; }
    OnPad_Browse(pad);
}

HRESULT FileBrowserApp::FrameMove(){
    XBInput_GetInput();

    // --- Auto Map+Rescan if drive set changes ---
    {
        static DWORD        s_nextPollMs = 0;
        static unsigned int s_lastMask   = 0;

        DWORD now = GetTickCount();
        if (now >= s_nextPollMs) {
            s_nextPollMs = now + 1200;              // poll ~1.2s we can weak this if needed.

            MapStandardDrives_Io();                 // ensure standard links are present
            unsigned int mask = BuildDriveMask();   // cheap presence mask A:..Z:

            if (s_lastMask == 0) {
				s_lastMask = mask;					// prime on first run to avoid immediate toast
			} else if (mask != s_lastMask) {
				s_lastMask = mask;
                RescanDrives();                     // re-enumerate volumes
                EnsureListing(m_pane[0]);           // refresh both panes
                EnsureListing(m_pane[1]);
                SetStatus("Drives refreshed");      // brief toast (optional)
            }
        }
    }
    // --------------------------------------------

    OnPad(g_Gamepads[0]);
    return S_OK;
}

// ----- draw: menu / rename / panes -----
void FileBrowserApp::DrawMenu(){
    if (!m_ctx.IsOpen()) return;

    // (coordinates already set when opening)
    m_ctx.Draw(m_font, m_pd3dDevice);
}

void FileBrowserApp::DrawRename(){
    if (!m_kb.Active()) return;
    m_kb.Draw(m_font, m_pd3dDevice, kLineH);
}

// ----- listing / navigation -----
void FileBrowserApp::EnsureListing(Pane& p){
    if (p.mode==0) BuildDriveItems(p.items);
    else           ListDirectory(p.curPath, p.items);

    if (p.sel >= (int)p.items.size()) p.sel = (int)p.items.size()-1;
    if (p.sel < 0) p.sel = 0;

    if (p.scroll > p.sel) p.scroll = p.sel;
    int maxScroll = MaxI(0, (int)p.items.size() - m_visible);
    if (p.scroll > maxScroll) p.scroll = maxScroll;
}

void FileBrowserApp::EnterSelection(Pane& p){
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


   // files:
	if (!it.isDir && !it.isUpEntry) {
		if (HasXbeExt(it.name)) {
			char full[512]; 
			JoinPath(full, sizeof(full), p.curPath, it.name);

			// Optional cosmetic flush so the screen updates before the jump
			m_pd3dDevice->Present(NULL, NULL, NULL, NULL);
			Sleep(10);

			if (!LaunchXbeA(full)) {
				SetStatusLastErr("Launch failed");
			}
		}
		return;
	}
}

void FileBrowserApp::UpOne(Pane& p){
    if (p.mode==0) return;
    if (strlen(p.curPath)<=3){ p.mode=0; p.sel=0; p.scroll=0; BuildDriveItems(p.items); p.curPath[0]=0; return; }
    ParentPath(p.curPath); p.sel=0; p.scroll=0; ListDirectory(p.curPath,p.items);
}

// ----- Initialize / Render -----
HRESULT FileBrowserApp::Initialize(){
    if (FAILED(m_font.Create("D:\\Media\\Font.xpr", 0))) {
        m_font.Create("D:\\Media\\CourierNew.xpr", 0);
    }
    XBInput_CreateGamepads(); MapStandardDrives_Io(); RescanDrives();
    BuildDriveItems(m_pane[0].items); BuildDriveItems(m_pane[1].items);

    D3DVIEWPORT8 vp; m_pd3dDevice->GetViewport(&vp);

    const FLOAT sideMargin = MaxF(24.0f,  vp.Width  * 0.04f);
    const FLOAT gap        = MaxF(24.0f,  vp.Width  * 0.035f);

    kPaneGap  = gap;
    kListX_L  = sideMargin;

    const FLOAT totalUsable = (FLOAT)vp.Width - (sideMargin * 2.0f) - gap;
    kListW = MaxF(260.0f, (totalUsable / 2.0f) - 10.0f);

    kHdrW   = kListW + 30.0f;
    kHdrY   = MaxF(12.0f, vp.Height * 0.03f);
    kHdrH   = MaxF(22.0f, vp.Height * 0.04f);
    kListY  = MaxF(60.0f, kHdrY + kHdrH + 34.0f);

    kLineH  = MaxF(22.0f, vp.Height * 0.036f);

    FLOAT bottomY = (FLOAT)vp.Height - MaxF(48.0f, vp.Height * 0.09f);
    FLOAT usableH = bottomY - kListY; if (usableH < 0) usableH = 0;
    m_visible = (int)(usableH / kLineH); if (m_visible < 6) m_visible=6; if (m_visible>30) m_visible=30;

    return S_OK;
}

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

void FileBrowserApp::UpdateProgress(ULONGLONG done, ULONGLONG total, const char* label){
    m_prog.done  = done;
    m_prog.total = (total ? total : m_prog.total);
    if (label){
        _snprintf(m_prog.current, sizeof(m_prog.current), "%s", label);
        m_prog.current[sizeof(m_prog.current)-1] = 0;
    }

    // Throttle paints to ~25fps so copying isn't slowed too much
    DWORD now = GetTickCount();
    if (now - m_prog.lastPaintMs >= 40){
        m_prog.lastPaintMs = now;

        // redraw one frame (keep your normal Render path)
        Render();
        // tiny yield
        Sleep(0);
    }
}

void FileBrowserApp::EndProgress(){
    m_prog.active = false;
}

void FileBrowserApp::DrawProgressOverlay(){
    if (!m_prog.active) return;

    D3DVIEWPORT8 vp; m_pd3dDevice->GetViewport(&vp);

    // ensure room for ~42 chars (worst-case with wide glyphs)
    char fortyTwo[64]; for (int i=0;i<42;++i) fortyTwo[i]='W'; fortyTwo[42]=0;
    FLOAT fileW=0, fileH=0; MeasureTextWH(m_font, fortyTwo, fileW, fileH);

    const FLOAT margin = 18.0f;
    FLOAT w = MaxF(MaxF(420.0f, vp.Width * 0.50f), fileW + margin*2.0f);
    if (w > vp.Width - margin*2.0f) w = vp.Width - margin*2.0f;

    const FLOAT h = 116.0f; // a bit taller so bar + % sit well below text
    const FLOAT x = Snap((vp.Width  - w)*0.5f);
    const FLOAT y = Snap((vp.Height - h)*0.5f);

    DrawRect(x-6, y-6, w+12, h+12, 0xA0101010);
    DrawRect(x,   y,   w,    h,    0xE0222222);

    // lines layout
    const FLOAT titleY  = y + 10.0f;
    const FLOAT folderY = titleY + 24.0f;
    const FLOAT fileY   = folderY + 22.0f;
    const FLOAT barY    = fileY   + 26.0f;   // << moved down
    const FLOAT barH    = 20.0f;
    const FLOAT barX    = x + margin;
    const FLOAT barW    = w - margin*2.0f;

    // title
    DrawAnsi(m_font, x + margin, titleY, 0xFFFFFFFF, m_prog.title[0] ? m_prog.title : "Working...");

    // split folder/file from current label
    const char* label = m_prog.current;
    const char* slash = label ? strrchr(label, '\\') : NULL;

    char folder[512] = {0};
    char file  [256] = {0};

    if (slash){
        size_t n = (size_t)(slash - label + 1); // include trailing '\'
        if (n >= sizeof(folder)) n = sizeof(folder)-1;
        memcpy(folder, label, n); folder[n] = 0;
        _snprintf(file, sizeof(file), "%s", slash + 1); file[sizeof(file)-1]=0;
    } else {
        folder[0] = 0;
        _snprintf(file, sizeof(file), "%s", label ? label : "");
        file[sizeof(file)-1]=0;
    }

    // left-truncate folder if too long for the content width
    // Marquee the folder path if too wide
{
    static char  s_lastFolder[512] = {0};
    static DWORD s_nextTick = 0;
    static FLOAT s_px = 0.0f;
    static int   s_dir = +1; // +1 forward, -1 backward

    // measure full width
    WCHAR wfull[512]; MbToW(folder, wfull, 512);
    FLOAT fullW=0, fullH=0; m_font.GetTextExtent(wfull, &fullW, &fullH);

    const FLOAT drawX = x + margin;
    const DWORD now   = GetTickCount();

    // Reset scroll when a new operation starts or the folder path changes
    if (m_prog.done == 0 || _stricmp(s_lastFolder, folder) != 0){
        _snprintf(s_lastFolder, sizeof(s_lastFolder), "%s", folder);
        s_px      = 0.0f;
        s_dir     = +1;
        s_nextTick= now + 600; // initial pause
    }

    if (fullW <= barW){
        // Fits: draw as-is
        DrawAnsi(m_font, drawX, folderY, 0xFF5EA4FF, folder);
    }else{
        // Advance the marquee
        const DWORD kStepMs     = 25;
        const FLOAT kStepPx     = 2.0f;
        const DWORD kInitPause  = 600;
        const DWORD kEndPause   = 800;
        const FLOAT maxScroll   = fullW - barW;

        if (now >= s_nextTick){
            s_px += (s_dir > 0 ? kStepPx : -kStepPx);
            if (s_px <= 0.0f){ s_px = 0.0f; s_dir = +1; s_nextTick = now + kInitPause; }
            else if (s_px >= maxScroll){ s_px = maxScroll; s_dir = -1; s_nextTick = now + kEndPause; }
            else { s_nextTick = now + kStepMs; }
        }

        // Build visible slice starting at pixel offset s_px
        FLOAT skipped=0.0f;
        const char* start = SkipToPixelOffset(m_font, folder, s_px, skipped);

        char vis[512]; vis[0]=0; int n=0;
        WCHAR wtmp[512]; FLOAT tw=0, th=0;
        const char* p = start;
        while (*p && n < (int)sizeof(vis)-2){
            vis[n++] = *p; vis[n] = 0;
            MbToW(vis, wtmp, 512); m_font.GetTextExtent(wtmp, &tw, &th);
            if (tw > barW){ vis[--n] = 0; break; }
            ++p;
        }

        DrawAnsi(m_font, drawX, folderY, 0xFF5EA4FF, vis);
    }
}

    // filename (shown as-is; 42-char names fit due to width above)
    DrawAnsi(m_font, x + margin, fileY, 0xFF89D07E, file);

    // progress value
    FLOAT pct = 0.0f;
    if (m_prog.total > 0){
        pct = (FLOAT)((double)m_prog.done / (double)m_prog.total);
        if (pct < 0) pct = 0; if (pct > 1) pct = 1;
    }

    // bar
    DrawRect(barX, barY, barW,       barH, 0xFF0E0E0E);
    DrawRect(barX, barY, barW * pct, barH, 0x90FFFF00);

    // % label aligned with the bar, right side and vertically centered to it
    char t[32]; _snprintf(t, sizeof(t), "%u%%", (unsigned int)(pct*100.0f + 0.5f)); t[sizeof(t)-1]=0;
    FLOAT tw=0, th=0; MeasureTextWH(m_font, t, tw, th);
    const FLOAT tx = Snap(barX + barW - tw);
    const FLOAT ty = Snap(barY + (barH - th) * 0.5f);
    DrawAnsi(m_font, tx, ty, 0xFFEEEEEE, t);
}




HRESULT FileBrowserApp::Render(){
    m_pd3dDevice->Clear(0,NULL,D3DCLEAR_TARGET,0x20202020,1.0f,0);
    m_pd3dDevice->BeginScene();
    m_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    m_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    m_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);

    // left + right panes (now via PaneRenderer)
    PaneStyle st;
    st.listW       = kListW;
    st.listY       = kListY;
    st.lineH       = kLineH;
    st.hdrY        = kHdrY;
    st.hdrH        = kHdrH;
    st.hdrW        = kHdrW;
    st.gutterW     = kGutterW;
    st.paddingX    = kPaddingX;
    st.scrollBarW  = kScrollBarW;
    st.visibleRows = m_visible;

    m_renderer.DrawPane(m_font, m_pd3dDevice, kListX_L,                     m_pane[0], m_active==0, st, 0);
	m_renderer.DrawPane(m_font, m_pd3dDevice, kListX_L + kListW + kPaneGap, m_pane[1], m_active==1, st, 1);

    // footer
    D3DVIEWPORT8 vp2; m_pd3dDevice->GetViewport(&vp2);
	const FLOAT footerY = (FLOAT)vp2.Height - MaxF(48.0f, vp2.Height * 0.09f);
	const FLOAT footerX = HdrX(kListX_L);
	const FLOAT footerW = kHdrW*2 + kPaneGap + 30.0f;

	const Pane& ap = m_pane[m_active];
	const Item* cur = (ap.items.empty()? NULL : &ap.items[ap.sel]);
	const char* yLabel = (cur && !cur->isUpEntry && cur->marked) ? "Unmark" : "Mark";

	DrawRect(footerX, footerY, footerW, 28.0f, 0x802A2A2A);

	if (m_pane[m_active].mode==0){
		const char* hints = "D-Pad: Move  |  Left/Right: Switch pane  |  A: Enter  |  X: Menu  |  Black/White: Page";
		DrawAnsiCenteredX(m_font, footerX, footerW, footerY+4.0f, 0xFFCCCCCC, hints);
	} else {
		ULONGLONG fb=0, tb=0; GetDriveFreeTotal(m_pane[m_active].curPath, fb, tb);
		char fstr[64], tstr[64], bar[420];
		FormatSize(fb, fstr, sizeof(fstr)); FormatSize(tb, tstr, sizeof(tstr));
		_snprintf(bar,sizeof(bar),
				"Active: %s   |   B: Up   |   Free: %s / Total: %s   |   X: Menu   |   Y: %s   |   Black/White: Page",
    (			m_active==0?"Left":"Right"), fstr, tstr, yLabel);
		bar[sizeof(bar)-1]=0;
		DrawAnsiCenteredX(m_font, footerX, footerW, footerY+4.0f, 0xFFCCCCCC, bar);
	}

    // transient status
    DWORD now = GetTickCount();
    if (now < m_statusUntilMs && m_status[0]){
        DrawAnsi(m_font, HdrX(kListX_L)+5.0f, footerY-18.0f, 0xFFBBDDEE, m_status);
    }

    DrawMenu();
    DrawRename();
    DrawProgressOverlay();

    m_pd3dDevice->EndScene(); m_pd3dDevice->Present(NULL,NULL,NULL,NULL);
    return S_OK;
}

// ---- static layout defaults (overwritten in Initialize) ----
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
