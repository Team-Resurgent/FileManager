#include "FileBrowserApp.h"
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

    m_menuOpen=false; m_menuSel=0; m_menuCount=0; m_mode=MODE_BROWSE;
    m_status[0]=0; m_statusUntilMs=0;
}

// ----- draw prims -----
void FileBrowserApp::DrawRect(float x,float y,float w,float h,D3DCOLOR c){
    TLVERT v[4];
    v[0].x=x; v[0].y=y; v[1].x=x+w; v[1].y=y; v[2].x=x; v[2].y=y+h; v[3].x=x+w; v[3].y=y+h;
    for(int i=0;i<4;i++){ v[i].z=0.0f; v[i].rhw=1.0f; v[i].color=c; }
    m_pd3dDevice->SetTexture(0,NULL);
    m_pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_DISABLE);
    m_pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    m_pd3dDevice->SetVertexShader(FVF_TLVERT);
    m_pd3dDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(TLVERT));
}

FLOAT FileBrowserApp::MeasureTextW(const char* s){
    WCHAR wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 256);
    FLOAT w=0.0f, h=0.0f;
    m_font.GetTextExtent(wbuf, &w, &h);
    return w;
}
void FileBrowserApp::MeasureTextWH(const char* s, FLOAT& outW, FLOAT& outH){
    WCHAR wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 256);
    m_font.GetTextExtent(wbuf, &outW, &outH);
}
void FileBrowserApp::DrawRightAligned(const char* s, FLOAT rightX, FLOAT y, DWORD color){
    DrawAnsi(m_font, rightX - MeasureTextW(s), y, color, s);
}

// ----- size column -----
FLOAT FileBrowserApp::ComputeSizeColW(const Pane& p){
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
    maxW += 16.0f;
    const FLOAT minW = MaxF(90.0f, kLineH * 4.0f);
    const FLOAT maxWClamp = kListW * 0.40f;
    if(maxW < minW) maxW = minW;
    if(maxW > maxWClamp) maxW = maxWClamp;
    return maxW;
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

// ----- context menu -----
void FileBrowserApp::AddMenuItem(const char* label, Action act, bool enabled){
    m_menu[m_menuCount].label   = label;
    m_menu[m_menuCount].act     = act;
    m_menu[m_menuCount].enabled = enabled;
    ++m_menuCount;
}
void FileBrowserApp::BuildContextMenu(){
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
void FileBrowserApp::OpenMenu(){ BuildContextMenu(); m_menuOpen=true; m_mode=MODE_MENU; }
void FileBrowserApp::CloseMenu(){ m_menuOpen=false; if (m_mode==MODE_MENU) m_mode=MODE_BROWSE; }

// ----- rename lifecycle (now fully delegated to OnScreenKeyboard) -----
void FileBrowserApp::BeginRename(const char* parentDir, const char* oldName){
    m_menuOpen = false;
    // Start the external keyboard (case choice handled internally; start upper by default)
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

    // Use current active pane for parent/old (we're modal during rename, so selection hasn't changed)
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
        if (_stricmp(ap.curPath, ap.curPath)==0) SelectItemInPane(ap, clean);
    }else{
        SetStatusLastErr("Rename failed");
        RefreshPane(m_pane[0]); RefreshPane(m_pane[1]);
    }
    CancelRename();
}

// ----- input: rename modal (delegates to OnScreenKeyboard) -----
void FileBrowserApp::OnPad_Rename(const XBGAMEPAD& pad){
    OnScreenKeyboard::Result r = m_kb.OnPad(pad);
    if (r == OnScreenKeyboard::ACCEPTED){ AcceptRename(); return; }
    if (r == OnScreenKeyboard::CANCELED){ CancelRename(); return; }

    // keep edge state roughly synced with app (not required, but harmless)
    m_prevButtons = pad.wButtons;
    m_prevA = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    m_prevB = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    m_prevX = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    m_prevY = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
    m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
}

// ----- input: context menu -----
void FileBrowserApp::OnPad_Menu(const XBGAMEPAD& pad){
    const DWORD btn = pad.wButtons;
    bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)   && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))   || (pad.sThumbLY >  16000);
    bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN)) || (pad.sThumbLY < -16000);

    if (up)   { if (m_menuSel > 0)              --m_menuSel; }
    if (down) { if (m_menuSel < m_menuCount-1)  ++m_menuSel; }

    unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    unsigned char x = pad.bAnalogButtons[XINPUT_GAMEPAD_X];

    bool aTrig = (a > 30 && m_prevA <= 30);
    bool bTrig = (b > 30 && m_prevB <= 30);
    bool xTrig = (x > 30 && m_prevX <= 30);

    if (aTrig) {
        const MenuItem& mi = m_menu[m_menuSel];
        if (mi.enabled) ExecuteAction(mi.act);
    } else if (bTrig || xTrig) {
        CloseMenu();
    }

    m_prevButtons = btn;
    m_prevA = a; m_prevB = b; m_prevX = x;
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

    if (xTrig) { OpenMenu(); goto store_prev; }

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
    if (!m_menuOpen) return;

    const FLOAT menuW = 340.0f;
    const FLOAT rowH  = kLineH + 6.0f;
    const FLOAT menuH = 12.0f + m_menuCount*rowH + 12.0f;

    const FLOAT paneX = (m_active==0) ? kListX_L : (kListX_L + kListW + kPaneGap);
    const FLOAT x = paneX + (kListW - menuW)*0.5f;
    const FLOAT y = kListY + 20.0f;

    DrawRect(x-6.0f, y-6.0f, menuW+12.0f, menuH+12.0f, 0xA0101010);
    DrawRect(x, y, menuW, menuH, 0xE0222222);

    DrawAnsi(m_font, x+10.0f, y+6.0f, 0xFFFFFFFF, "Select action");
    DrawHLine(x, y+26.0f, menuW, 0x60FFFFFF);

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

void FileBrowserApp::DrawRename(){
    if (!m_kb.Active()) return;
    m_kb.Draw(m_font, m_pd3dDevice, kLineH);
}

void FileBrowserApp::ExecuteAction(Action act){
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
        if (!CanWriteHereA(dstDir)){ SetStatusLastErr("Dest not writable"); CloseMenu(); break; }

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
        if (!CanWriteHereA(dstDir)){ SetStatusLastErr("Dest not writable"); CloseMenu(); break; }

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
        if (sel && src.mode==1 && !sel->isUpEntry){
            BeginRename(src.curPath, sel->name); CloseMenu();
        } else {
            SetStatus("Open a folder and select an item"); CloseMenu();
        }
        break;

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
        if (src.mode == 1){
            if (!IsDriveRoot(src.curPath)){
                char root[4] = { src.curPath[0], ':', '\\', 0 };
                strncpy(src.curPath, root, sizeof(src.curPath)-1);
                src.curPath[3] = 0;
                src.sel = 0; src.scroll = 0;
                ListDirectory(src.curPath, src.items);
            } else {
                src.mode = 0; src.curPath[0] = 0; src.sel = 0; src.scroll = 0;
                BuildDriveItems(src.items);
            }
        } else {
            BuildDriveItems(src.items);
        }
        CloseMenu(); break;

    case ACT_SWITCHMEDIA:
        m_active = 1 - m_active; CloseMenu(); break;
    }
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

    // left + right panes
    DrawPane(kListX_L,                     m_pane[0], m_active==0);
    DrawPane(kListX_L + kListW + kPaneGap, m_pane[1], m_active==1);

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

// ----- pane draw -----
void FileBrowserApp::DrawPane(FLOAT baseX, Pane& p, bool active){
    FLOAT hx = HdrX(baseX);
    DrawRect(hx, kHdrY, kHdrW, kHdrH, active ? 0xFF3A3A3A : 0x802A2A2A);
    if (p.mode==0) DrawAnsi(m_font, hx+5.0f, kHdrY+6.0f, 0xFFFFFFFF, "Detected Drives");
    else { char hdr[600]; _snprintf(hdr,sizeof(hdr),"%s", p.curPath); hdr[sizeof(hdr)-1]=0; DrawAnsi(m_font, hx+5.0f, kHdrY+6.0f, 0xFFFFFFFF, hdr); }

    // compute Size column width/X
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
        D3DCOLOR ico = it.isUpEntry ? 0xFFAAAAAA : ( it.isDir ? 0xFF5EA4FF : 0xFF89D07E );
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
