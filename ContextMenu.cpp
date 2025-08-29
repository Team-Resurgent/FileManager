#include "ContextMenu.h"
#include "GfxPrims.h"
#include <wchar.h>

ContextMenu::ContextMenu(){
    m_count=0; m_sel=0;
    m_open=false; m_waitRelease=false;
    m_x=0; m_y=0; m_w=320; m_rowH=28;
    m_prevA=m_prevB=m_prevX=m_prevWhite=m_prevBlack=0;
    m_prevButtons=0;
}

void ContextMenu::Clear(){ m_count=0; m_sel=0; }

void ContextMenu::AddItem(const char* label, Action act, bool enabled){
    if (m_count >= (int)(sizeof(m_items)/sizeof(m_items[0]))) return;
    m_items[m_count].label   = label;
    m_items[m_count].act     = act;
    m_items[m_count].enabled = enabled;
    ++m_count;
}

void ContextMenu::OpenAt(float x, float y, float width, float rowH){
    m_x=x; m_y=y; m_w=width; m_rowH=rowH;
    if (m_sel < 0) m_sel = 0;
    if (m_sel >= m_count) m_sel = (m_count>0)?(m_count-1):0;
    m_open=true;
    m_waitRelease=true; // avoid immediate A/X carry-over
    m_prevA=m_prevB=m_prevX=m_prevWhite=m_prevBlack=0;
    m_prevButtons=0;
}

void ContextMenu::Close(){ m_open=false; }

void ContextMenu::DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
    WCHAR wbuf[512]; MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
    font.DrawText(x,y,color,wbuf,0,0.0f);
}

void ContextMenu::DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(dev, x, y, w, h, c);
}

void ContextMenu::Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev) const{
    if (!m_open || m_count<=0) return;

    const FLOAT menuW = m_w;
    const FLOAT rowH  = m_rowH;
    const FLOAT menuH = 12.0f + m_count*rowH + 12.0f;
    const FLOAT x = m_x;
    const FLOAT y = m_y;

    DrawRect(dev, x-6.0f, y-6.0f, menuW+12.0f, menuH+12.0f, 0xA0101010);
    DrawRect(dev, x, y, menuW, menuH, 0xE0222222);

    DrawAnsi(font, x+10.0f, y+6.0f, 0xFFFFFFFF, "Select action");
    DrawRect(dev, x, y+26.0f, menuW, 1.0f, 0x60FFFFFF);

    FLOAT iy = y + 30.0f;
    for (int i=0;i<m_count;++i){
        bool sel = (i == m_sel);
        D3DCOLOR row = sel ? 0x60FFFF00 : 0x20202020;
        DrawRect(dev, x+6.0f, iy-2.0f, menuW-12.0f, rowH, row);

        const Item& it = m_items[i];
        DWORD col = it.enabled? (sel?0xFF202020:0xFFE0E0E0) : 0xFF7A7A7A;
        DrawAnsi(font, x+16.0f, iy, col, it.label);
        iy += rowH;
    }
}

ContextMenu::Result ContextMenu::OnPad(const XBGAMEPAD& pad, Action& outAct){
    outAct = ACT_OPEN;
    if (!m_open) return NOOP;

    const DWORD btn = pad.wButtons;
    unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    unsigned char x = pad.bAnalogButtons[XINPUT_GAMEPAD_X];

    // absorb the press that opened the menu
    if (m_waitRelease){
        bool held = (a>30)||(b>30)||(x>30)||(btn & XINPUT_GAMEPAD_START)||
                    (btn & XINPUT_GAMEPAD_DPAD_UP)||(btn & XINPUT_GAMEPAD_DPAD_DOWN);
        m_prevA=a; m_prevB=b; m_prevX=x; m_prevButtons=btn;
        m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
        m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
        if (!held) m_waitRelease=false;
        return NOOP;
    }

    bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)   && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))   || (pad.sThumbLY >  16000);
    bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN)) || (pad.sThumbLY < -16000);

    if (up)   { if (m_sel > 0)             --m_sel; }
    if (down) { if (m_sel < m_count-1)     ++m_sel; }

    bool aTrig = (a > 30 && m_prevA <= 30);
    bool bTrig = (b > 30 && m_prevB <= 30);
    bool xTrig = (x > 30 && m_prevX <= 30);

    m_prevButtons = btn;
    m_prevA = a; m_prevB = b; m_prevX = x;
    m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];

    if (aTrig){
        const Item& it = m_items[m_sel];
        if (it.enabled){ outAct = it.act; return CHOSEN; }
        return NOOP;
    }
    if (bTrig || xTrig) return CLOSED;

    return NOOP;
}
