#include "FileBrowserApp.h"
#include "AppActions.h"
#include "GfxPrims.h"
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

    m_ctx.Clear();
    if (inDir) AddMenuItem("Open",            ACT_OPEN,       hasSel);
    AddMenuItem("Copy",            ACT_COPY,       hasSel);
    AddMenuItem("Move",            ACT_MOVE,       hasSel);
    AddMenuItem("Delete",          ACT_DELETE,     hasSel);
    AddMenuItem("Rename",          ACT_RENAME,     hasSel);
    AddMenuItem("Make new folder", ACT_MKDIR,      inDir);
    AddMenuItem("Calculate size",  ACT_CALCSIZE,   hasSel);
    AddMenuItem("Go to root",      ACT_GOROOT,     inDir);
    AddMenuItem("Switch pane",     ACT_SWITCHMEDIA,true);
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
    unsigned char w = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE]; // page down
    unsigned char k = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK]; // page up

    bool aTrig = (a > 30 && m_prevA <= 30);
    bool bTrig = (b > 30 && m_prevB <= 30);
    bool xTrig = (x > 30 && m_prevX <= 30);
    bool wTrig = (w > 30 && m_prevWhite <= 30);
    bool kTrig = (k > 30 && m_prevBlack <= 30);
    bool startTrig = ((btn & XINPUT_GAMEPAD_START) && !(m_prevButtons & XINPUT_GAMEPAD_START));

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

    if (startTrig){
        MapStandardDrives_Io(); RescanDrives();
        EnsureListing(m_pane[0]); EnsureListing(m_pane[1]);
    }

    m_prevButtons = btn;
    m_prevA = a; m_prevB = b; m_prevX = x;
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
    // files: no-op
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

    m_renderer.DrawPane(m_font, m_pd3dDevice, kListX_L,                     m_pane[0], m_active==0, st);
    m_renderer.DrawPane(m_font, m_pd3dDevice, kListX_L + kListW + kPaneGap, m_pane[1], m_active==1, st);

    // footer
    D3DVIEWPORT8 vp2; m_pd3dDevice->GetViewport(&vp2);
    const FLOAT footerY = (FLOAT)vp2.Height - MaxF(48.0f, vp2.Height * 0.09f);
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

    // transient status
    DWORD now = GetTickCount();
    if (now < m_statusUntilMs && m_status[0]){
        DrawAnsi(m_font, HdrX(kListX_L)+5.0f, footerY-18.0f, 0xFFBBDDEE, m_status);
    }

    DrawMenu();
    DrawRename();

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
