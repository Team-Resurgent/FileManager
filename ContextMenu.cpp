#include "ContextMenu.h"

#include "GfxPrims.h"
#include "FileBrowserApp.h"
#include "TextUtils.h"

/*
===============================================================================
 ContextMenu
  - Small, controller-driven popup menu for the file browser.
  - Works on OG Xbox (XDK / VS2003): ANSI strings, no STL allocations here.
  - Supports non-selectable separator rows that are skipped by navigation.

  Life cycle:
    * AddItem() / AddSeparator()
    * OpenAt(x,y,width,rowH)   -> becomes active and edge-debounced
    * Draw(font, device)       -> render when m_open = true
    * OnPad(pad, outAct)       -> handles movement / choose / close
    * Close()                  -> hide

  Input:
    * D-Pad Up/Down or stick Y to move selection (skips separators/disabled)
    * A to choose (CHOSEN result + outAct set)
    * B or X to close (CLOSED result)
    * Start ignored here (caller handles global Start if needed)

  Rendering:
    * Simple framed panel with dynamic header height, divider line,
      then N rows of fixed height (m_rowH).
===============================================================================
*/

ContextMenu::ContextMenu(){
    m_count=0; m_sel=0;
    m_open=false; m_waitRelease=false;
    m_x=0; m_y=0; m_w=0; m_rowH=28;
    m_mw = 160;
    m_prevA=m_prevB=m_prevX=m_prevWhite=m_prevBlack=0;
    m_prevButtons=0;
    m_parentMenu = NULL;
    m_label[0] = '\0';
}

void ContextMenu::Clear(){ m_count=0; m_sel=0; }

// Add a selectable row
void ContextMenu::AddItem(const char* label, Action act, bool enabled){
    if (m_count >= (int)(sizeof(m_items)/sizeof(m_items[0]))) return;
    strncpy(m_items[m_count].label, label, sizeof(m_items[m_count].label) - 1);
    m_items[m_count].label[sizeof(m_items[m_count].label) - 1] = '\0';
    m_items[m_count].act     = act;
    m_items[m_count].enabled = enabled;
    m_items[m_count].separator = false;
    m_items[m_count].child = NULL;
    ++m_count;
}

// Add a non-selectable separator row (drawn as a thin line)
void ContextMenu::AddSeparator(){
    if (m_count >= (int)(sizeof(m_items)/sizeof(m_items[0]))) return;
    m_items[m_count].label[0]     = 0;       // not used for separators
    m_items[m_count].act       = ACT_NONE; // placeholder; ignored
    m_items[m_count].enabled   = false;
    m_items[m_count].separator = true;     // <-- key bit
    m_items[m_count].child = NULL;
    ++m_count;
}

//Add a selectable submenu
void ContextMenu::AddSubMenu(const char* label, ContextMenu* submenu, bool enabled) {
    if (m_count >= (int)(sizeof(m_items) / sizeof(m_items[0]))) return;
    strncpy(m_items[m_count].label, label, sizeof(m_items[m_count].label) - 1);
    m_items[m_count].label[sizeof(m_items[m_count].label) - 1] = '\0';
    m_items[m_count].act = ACT_NONE;   // not used for submenu
    m_items[m_count].enabled = enabled;
    m_items[m_count].separator = false;
    m_items[m_count].child = submenu;    // <--- SUBMENU POINTER
    ++m_count;
}

// Row can be chosen iff enabled and not a separator
bool ContextMenu::IsSelectable(int idx) const{
    if (idx < 0 || idx >= m_count) return false;
    const Item& it = m_items[idx];
    return (it.enabled && !it.separator);
}

// Scan forward/backward for the next selectable row; return -1 if none
int ContextMenu::FindNextSelectable(int start, int dir) const{
    int i = start;
    while (i >= 0 && i < m_count){
        if (IsSelectable(i)) return i;
        i += dir;
    }
    return -1;
}

// Position/size the menu and make it active.
// Also snaps the selection onto a valid (selectable) row and
// arms a small "wait for release" window so the A/X that opened
// the menu doesn’t immediately trigger a choose/close here.
void ContextMenu::OpenAt(float x, float y, float width, float rowH) {
    m_x=x; m_y=y; 
    m_w=width; m_rowH=rowH;

    int count = m_count + 1; // Quick fix, but why?

    if (m_dev) {
        D3DVIEWPORT8 vp;
        m_dev->GetViewport(&vp);

        float screenW = (float)vp.Width;
        float screenH = (float)vp.Height - 30.0f; // Safety, but why?

        // Right clamp
        if (m_x + m_w > screenW) m_x = screenW - m_w;

        // Left clamp
        if (m_x < 0) m_x = 0;

        // Bottom clamp
        if (m_y + (count * m_rowH) > screenH) m_y = screenH - (count * m_rowH);

        // Top clamp
        if (m_y < 0) m_y = 0;
    }

    if (m_sel < 0) m_sel = 0;
    if (m_sel >= count) m_sel = (count > 0) ? (count - 1) : 0;

    // If current selection is not selectable, try forward then backward.
    if (!IsSelectable(m_sel)){
        int fwd  = FindNextSelectable(m_sel, +1);
        int back = FindNextSelectable(m_sel, -1);
        if (fwd >= 0)       m_sel = fwd;
        else if (back >= 0) m_sel = back;
        else                m_sel = 0; // no selectable items; harmless default
    }

    m_open=true;
    m_waitRelease=true; // avoid immediate A/X carry-over
    m_prevA=m_prevB=m_prevX=m_prevWhite=m_prevBlack=0;
    m_prevButtons = 0;
}

// Render the menu panel + rows. Separators are drawn as thin centered lines.
// Disabled rows are dimmed and cannot be focused via navigation.
void ContextMenu::Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev) const {
    if (!m_open || m_count <= 0) return;

    bool drawHeader = false;
    if (m_label[0] != '\0') drawHeader = true;

    FLOAT tw = 0.0f;
    for (int i = 0; i < m_count; ++i) {
        const Item& it = m_items[i];
        FLOAT cw = GetAnsiW(font, it.label);
        tw = (tw > cw) ? tw : cw;
    }

    FLOAT hdrW, hdrH;
    GetAnsiWH(font, m_label, &hdrW, &hdrH);

    tw = (tw > hdrW) ? tw : hdrW;
    tw = (tw > m_mw) ? tw : m_mw;

    //const FLOAT menuW = m_w;
    const FLOAT menuW = tw + 32.0f; // + 32.0f (label padding)
    const FLOAT rowH  = m_rowH;

    m_w = menuW; // menuW is unneccessary but leaving it for now

    // Layout constants (dynamic header height support)
    const FLOAT headerTopPad    = 8.0f;
    FLOAT headerBottomPad = 6.0f;
    if (!drawHeader) headerBottomPad = 3.0f; //inelegant solution to size correctly without the header
    const FLOAT bottomPad       = 10.0f;

    const FLOAT x = m_x;
    const FLOAT y = m_y;

    const FLOAT lineY   = y + headerTopPad + hdrH + headerBottomPad; // divider Y
    const FLOAT listTop = lineY + 6.0f;                              // first row Y
    const FLOAT menuH   = (listTop - y) + (m_count * rowH) + bottomPad;

    // Frame/background
    DrawSolidRect(dev, x - 4.5f, y - 4.5f, menuW + 9.0f, menuH + 9.0f, 0xA0101010);
    DrawSolidRect(dev, x, y, menuW, menuH, 0xE0222222);

    // Header
    if (drawHeader) {
        DrawAnsi(font, x + 10.0f, y + headerTopPad, 0xFFFFFFFF, &DefaultColors, m_label);
        DrawSolidRect(dev, x, lineY, menuW, 1.0f, 0x60FFFFFF);
    }

    // Rows
    for (int i = 0; i < m_count; ++i){
        const FLOAT rowY = listTop + i * rowH;
        const Item& it = m_items[i];

        if (it.separator) {
            // Non-selectable divider line centered within the row box
            DrawSolidRect(dev, x + 10.0f, rowY + rowH * 0.5f, menuW - 20.0f, 1.0f, 0x50FFFFFF);
            continue;
        }

        // Selection highlight (only meaningful on selectable rows)
        bool sel = (i == m_sel);
        D3DCOLOR row = sel ? 0x60FFFF00 : 0x20202020;
        DrawSolidRect(dev, x + 6.0f, rowY - 2.0f, menuW - 12.0f, rowH, row);

        // Text color: normal/selected/disabled
        DWORD col = it.enabled ? (sel ? 0xFF202020 : 0xFFE0E0E0) : 0xFF7A7A7A;

        DrawAnsiCentered(font, x + 16.0f, rowY, col, &DefaultColors, it.label, NULL, rowH);
    }
}

// Handle controller input for the menu.
// Returns CHOSEN with an Action when A is pressed on a selectable row,
// CLOSED when B or X is pressed, or NOOP if nothing to do this frame.
ContextMenu::Result ContextMenu::OnPad(const XBGAMEPAD& pad, Action& outAct) {
    outAct = ACT_OPEN;   // harmless default
    if (!m_open) return NOOP;

    const DWORD btn = pad.wButtons;
    unsigned char a = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    unsigned char b = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    unsigned char x = pad.bAnalogButtons[XINPUT_GAMEPAD_X];

    // Debounce: absorb the input that opened the menu (prevents immediate choose/close)
    if (m_waitRelease) {
        bool held = (a>30)||(b>30)||(x>30)||
                    (btn & XINPUT_GAMEPAD_START)||
                    (btn & XINPUT_GAMEPAD_DPAD_UP)||
                    (btn & XINPUT_GAMEPAD_DPAD_DOWN);
        m_prevA=a; m_prevB=b; m_prevX=x; m_prevButtons=btn;
        m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
        m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];
        if (!held) m_waitRelease=false;
        return NOOP;
    }

    // Navigation (edge + analog)
    bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)   && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))   || (pad.sThumbLY >  16000);
    bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN)) || (pad.sThumbLY < -16000);

    if (up) {
        int i = FindNextSelectable(m_sel - 1, -1);
        if (i >= 0) m_sel = i;
    }
    if (down) {
        int i = FindNextSelectable(m_sel + 1, +1);
        if (i >= 0) m_sel = i;
    }

    // Button edges
    bool aTrig = (a > 30 && m_prevA <= 30);
    bool bTrig = (b > 30 && m_prevB <= 30);
    bool xTrig = (x > 30 && m_prevX <= 30);

    // Save previous for next frame's edge detection
    m_prevButtons = btn;
    m_prevA = a; m_prevB = b; m_prevX = x;
    m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
    m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];

    // Choose / close behavior
    if (aTrig) {
        const Item& it = m_items[m_sel];

        if (it.child != NULL) {
            float subX = m_x + m_w;
            float subY = m_y + (m_sel * m_rowH);
            it.child->m_parentMenu = this;       // so it can close back to us
            it.child->OpenAt(subX, subY, m_w, m_rowH);

            return SUBMENU_OPENED;
        }
        if (IsSelectable(m_sel)) {
            outAct = it.act; 
            return CHOSEN; 
        }

        return NOOP; // ignore A on non-selectable (e.g., separator)
    }

    if (bTrig || xTrig) return CLOSED;

    return NOOP;
}
