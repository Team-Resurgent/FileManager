#include "OnScreenKeyboard.h"
#include "GfxPrims.h"
#include <stdio.h>
#include <string.h>

// Local keyboard layout
namespace {
    static const char s_kb_r0[] = "ABCDEFGHIJKL";     // 12
    static const char s_kb_r1[] = "MNOPQRSTUVWX";     // 12
    static const char s_kb_r2[] = "YZ0123456789";     // 12
    static const char s_kb_r3[] = "-_.()[]{}+&";      // 12
    static const int  s_kb_cols[5] = {12,12,12,12,5}; // Back, Space, Aa, OK, Cancel
}

OnScreenKeyboard::OnScreenKeyboard(){
    m_active = false; m_lower = false;
    m_parent[0]=m_old[0]=m_buf[0]=0;
    m_cursor=0; m_row=0; m_col=0;
    m_prevA=m_prevB=m_prevY=m_prevWhite=m_prevBlack=0;
    m_prevButtons=0;
}

void OnScreenKeyboard::Open(const char* parentDir, const char* initialName, bool startLowerCase){
    _snprintf(m_parent, sizeof(m_parent), "%s", parentDir ? parentDir : "");
    m_parent[sizeof(m_parent)-1]=0;
    _snprintf(m_old, sizeof(m_old), "%s", initialName ? initialName : "");
    m_old[sizeof(m_old)-1]=0;
    _snprintf(m_buf, sizeof(m_buf), "%s", initialName ? initialName : "");
    m_buf[sizeof(m_buf)-1]=0;
    m_cursor = (int)strlen(m_buf);
    m_row = 0; m_col = 0;
    m_lower = startLowerCase;
    m_active = true;

    // reset edge state
    m_prevA=m_prevB=m_prevY=m_prevWhite=m_prevBlack=0;
    m_prevButtons=0;
	m_waitRelease = true;
}

void OnScreenKeyboard::Close(){
    m_active = false;
}

char OnScreenKeyboard::KbCharAt(int row, int col) const{
    char ch=0;
    if (row==0) ch = s_kb_r0[col];
    else if (row==1) ch = s_kb_r1[col];
    else if (row==2) ch = s_kb_r2[col];
    else if (row==3) ch = s_kb_r3[col];
    if (m_lower && (ch>='A' && ch<='Z')) ch = (char)(ch + ('a' - 'A'));
    return ch;
}

void OnScreenKeyboard::DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
    WCHAR wbuf[512]; MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
    font.DrawText(x,y,color,wbuf,0,0.0f);
}
void OnScreenKeyboard::DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(dev, x, y, w, h, c);
}
void OnScreenKeyboard::MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH){
    WCHAR wbuf[256]; MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 256);
    font.GetTextExtent(wbuf, &outW, &outH);
}
FLOAT OnScreenKeyboard::MeasureTextW(CXBFont& font, const char* s){
    FLOAT w=0,h=0; MeasureTextWH(font, s, w, h); return w;
}

void OnScreenKeyboard::Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT lineH){
    if (!m_active) return;

    D3DVIEWPORT8 vp; dev->GetViewport(&vp);

    const FLOAT panelW = MaxF(520.0f, vp.Width * 0.55f);
    const FLOAT panelH = MaxF(280.0f, vp.Height*0.45f);
    const FLOAT x = Snap((vp.Width  - panelW)*0.5f);
    const FLOAT y = Snap((vp.Height - panelH)*0.5f);

    // frame
    DrawRect(dev, x-8, y-8, panelW+16, panelH+16, 0xA0101010);
    DrawRect(dev, x, y, panelW, panelH, 0xE0222222);

    DrawAnsi(font, x+12, y+8, 0xFFFFFFFF, "Rename");
    // header line
    DrawRect(dev, x, y+28, panelW, 1.0f, 0x60FFFFFF);

    char hdr[640];
    _snprintf(hdr, sizeof(hdr), "In: %s", m_parent); hdr[sizeof(hdr)-1]=0;
    DrawAnsi(font, x+12, y+34, 0xFFCCCCCC, hdr);

    const FLOAT boxY = Snap(y + 60.0f);
    const FLOAT boxH = 30.0f;
    DrawRect(dev, x+12, boxY, panelW-24, boxH, 0xFF0E0E0E);

    // current name
    DrawAnsi(font, x+18, boxY+4, 0xFFFFFF00, m_buf);

    // caret
    char tmp = m_buf[m_cursor]; m_buf[m_cursor]=0;
    FLOAT caretX = Snap(x+18 + MeasureTextW(font, m_buf));
    m_buf[m_cursor]=tmp;
    DrawRect(dev, caretX, boxY+4, 2.0f, boxH-8.0f, 0x90FFFF00);

    // keyboard grid
    const FLOAT padX     = 12.0f;
    const FLOAT contentW = panelW - 2.0f*padX;
    const FLOAT cellH    = lineH + 6.0f;
    const FLOAT gridTop  = Snap(boxY + boxH + 16.0f);
    const FLOAT gapX     = 6.0f;
    const FLOAT gapY     = 4.0f;
    const FLOAT colW12   = contentW / 12.0f;

    for (int row=0; row<5; ++row){
        int cols = s_kb_cols[row];
        FLOAT rowY = Snap(gridTop + row*(cellH + gapY));

        for (int col=0; col<cols; ++col){
            FLOAT x0, x1;
            if (row < 4){
                x0 = Snap(x + padX +  col      * colW12);
                x1 = Snap(x + padX + (col + 1) * colW12);
            }else{
                FLOAT colW = contentW / (FLOAT)cols;
                x0 = Snap(x + padX +  col      * colW);
                x1 = Snap(x + padX + (col + 1) * colW);
            }

            const FLOAT drawX = x0 + gapX * 0.5f;
            const FLOAT drawW = (x1 - x0) - gapX;
            const bool  sel   = (row==m_row && col==m_col);

            DrawRect(dev, drawX, rowY, drawW, cellH, sel ? 0x60FFFF00 : 0x30202020);

            if (row < 4){
                char s[2]; s[0] = KbCharAt(row, col); s[1] = 0;
                FLOAT tw, th; MeasureTextWH(font, s, tw, th);
                const FLOAT tx = Snap(drawX + (drawW - tw) * 0.5f);
                const FLOAT ty = Snap(rowY + (cellH - th) * 0.5f);
                DrawAnsi(font, tx, ty, 0xFFE0E0E0, s);
            }else{
                const char* cap =
                    (col==0) ? "Backspace" :
                    (col==1) ? "Space"     :
                    (col==2) ? "Aa"        :
                    (col==3) ? "OK"        : "Cancel";
                FLOAT tw, th; MeasureTextWH(font, cap, tw, th);
                const FLOAT tx = Snap(drawX + (drawW - tw) * 0.5f);
                const FLOAT ty = Snap(rowY + (cellH - th) * 0.5f);
                DrawAnsi(font, tx, ty, 0xFFE0E0E0, cap);
            }
        }
    }

    DrawAnsi(font, x+12, y+panelH-20, 0xFFBBBBBB,
             "A: Select   B: Cancel   Start: OK   Y/Aa: Case   White/Black: Move cursor");
}

OnScreenKeyboard::Result OnPadResultFrom(bool a, bool b){
    return a ? OnScreenKeyboard::ACCEPTED : (b ? OnScreenKeyboard::CANCELED : OnScreenKeyboard::NONE);
}

OnScreenKeyboard::Result OnScreenKeyboard::OnPad(const XBGAMEPAD& pad){
    if (!m_active) return NONE;

    const DWORD btn = pad.wButtons;
    unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    unsigned char y = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
    unsigned char w = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    unsigned char k = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];

    // NEW: Debounce — ignore inputs until A/B/Start/Y/White/Black are released once
    if (m_waitRelease) {
        bool anyHeld = (a > 30) || (b > 30) || (y > 30) ||
                       (w > 30) || (k > 30) ||
                       (btn & XINPUT_GAMEPAD_START);
        // keep prev in sync so edge detection behaves after release
        m_prevA = a; m_prevB = b; m_prevY = y;
        m_prevWhite = w; m_prevBlack = k; m_prevButtons = btn;

        if (!anyHeld) m_waitRelease = false; // ready to accept real input next frame
        return NONE;
    }

    // edge + analog (unchanged from your version)
    bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)    && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))    || (pad.sThumbLY >  16000);
    bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN))  || (pad.sThumbLY < -16000);
    bool left  = ((btn & XINPUT_GAMEPAD_DPAD_LEFT)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_LEFT))  || (pad.sThumbLX < -16000);
    bool right = ((btn & XINPUT_GAMEPAD_DPAD_RIGHT) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) || (pad.sThumbLX >  16000);

    if (up)   { if (m_row > 0) m_row--; Sleep(120); }
    if (down) { if (m_row < 4) m_row++; Sleep(120); }

    int cols = /* s_kb_cols[m_row] */ (m_row<4?12:5);
    if (m_col >= cols) m_col = cols - 1;

    if (left)  { if (m_col > 0)        m_col--; Sleep(120); }
    if (right) { if (m_col < cols - 1) m_col++; Sleep(120); }

    bool aTrig = (a > 30 && m_prevA <= 30);
    bool bTrig = (b > 30 && m_prevB <= 30);
    bool yTrig = (y > 30 && m_prevY <= 30);
    bool startTrig = ((btn & XINPUT_GAMEPAD_START) && !(m_prevButtons & XINPUT_GAMEPAD_START));

    if (yTrig){ m_lower = !m_lower; Sleep(120); }

    if (aTrig){
        if (m_row <= 3){
            int len = (int)strlen(m_buf);
            const int cap = (int)sizeof(m_buf) - 1;
            if (len < cap){
                char ch = KbCharAt(m_row, m_col);
                for (int i=len; i>=m_cursor; --i) m_buf[i+1] = m_buf[i];
                m_buf[m_cursor++] = ch;
            }
        }else{
            // Back | Space | Aa | OK | Cancel
            if (m_col == 0){ // backspace
                if (m_cursor > 0){
                    int len = (int)strlen(m_buf);
                    for (int i=m_cursor-1; i<=len; ++i) m_buf[i] = m_buf[i+1];
                    m_cursor--;
                }
            }else if (m_col == 1){ // space
                int len = (int)strlen(m_buf);
                const int cap = (int)sizeof(m_buf) - 1;
                if (len < cap){
                    for (int i=len; i>=m_cursor; --i) m_buf[i+1] = m_buf[i];
                    m_buf[m_cursor++] = ' ';
                }
            }else if (m_col == 2){ // Aa
                m_lower = !m_lower;
            }else if (m_col == 3){ // OK
                m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
                return ACCEPTED;
            }else if (m_col == 4){ // Cancel
                m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
                return CANCELED;
            }
        }
        Sleep(140);
    }

    if (startTrig){
        m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
        return ACCEPTED;
    }
    if (bTrig){
        m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevWhite=w; m_prevBlack=k;
        return CANCELED;
    }

    bool wTrig = (w > 30 && m_prevWhite <= 30);
    bool kTrig = (k > 30 && m_prevBlack <= 30);
    if (wTrig){ if (m_cursor > 0) m_cursor--; }
    if (kTrig){ int len=(int)strlen(m_buf); if (m_cursor < len) m_cursor++; }

    // clamp caret
    { int len=(int)strlen(m_buf);
      if (m_cursor < 0)   m_cursor = 0;
      if (m_cursor > len) m_cursor = len; }

    // save prev
    m_prevA = a; m_prevB = b; m_prevY = y;
    m_prevButtons = btn; m_prevWhite = w; m_prevBlack = k;

    return NONE;
}
