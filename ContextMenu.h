#pragma once
#include <xtl.h>
#include "XBFont.h"
#include "XBInput.h"
#include "AppActions.h"

/*
============================================================================
 ContextMenu
 A lightweight context menu component.
 Handles:
   - A list of selectable items (text + Action ID)
   - Optional visual separators (non-selectable rows)
   - Drawing the menu with basic styling
   - Navigation + selection via gamepad (A = select, B/X = close)
============================================================================
*/

class ContextMenu {
public:
    // Return codes from OnPad()
    enum Result { NOOP, CHOSEN, CLOSED, SUBMENU_OPENED };

    ContextMenu();

    // Clear all menu items and reset selection
    void Clear();

    // Add a selectable menu item (label text, action ID, enabled/disabled)
    void AddItem(const char* label, Action act, bool enabled);

    // Add a visual separator line (non-selectable, just a divider)
    void AddSeparator(); 

    //Add a selectable submenu item (label text, submenu, enabled/disabled)
    void AddSubMenu(const char* label, ContextMenu* submenu, bool enabled);

    ContextMenu* GetSelectedChildMenu() const {
        if (m_sel < 0 || m_sel >= m_count) return NULL;
        return m_items[m_sel].child;
    }

    void AbsorbPadState(const XBGAMEPAD& pad) {
        m_prevButtons = pad.wButtons;
        m_prevA = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
        m_prevB = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
        m_prevX = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
        m_prevWhite = pad.bAnalogButtons[XINPUT_GAMEPAD_WHITE];
        m_prevBlack = pad.bAnalogButtons[XINPUT_GAMEPAD_BLACK];

        m_waitRelease = true;
    }

    // Open menu at screen coordinates (x,y), with given width and row height
    void OpenAt(float x, float y, float width, float rowH);

    // Close the menu
    void Close();

    // Query: is the menu currently visible?
    bool IsOpen() const { return m_open; }

    // NEW: Store device pointer so OpenAt() can call GetViewport()
    void SetDevice(LPDIRECT3DDEVICE8 dev) { m_dev = dev; }

    void SetLabel(char* l) { strncpy(m_label, l, 64); m_label[sizeof(m_label) - 1] = '\0'; }

    // Render the menu (caller supplies font + D3D device)
    void Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev) const;

    // Handle pad input:
    //   - Returns CHOSEN when a valid item is selected (outAct filled)
    //   - Returns CLOSED when canceled (B/X pressed)
    //   - Returns NOOP otherwise
    Result OnPad(const XBGAMEPAD& pad, Action& outAct);

private:
    // Internal representation of one row in the menu
    struct Item {
        char label[64];   // Display text (ANSI string)
        Action      act;     // Action ID (from AppActions.h)
        bool        enabled; // Disabled items are greyed out
        bool        separator; // True if this is just a divider row
        ContextMenu* child;
    };

    // ---- helpers for drawing ----
    static void   DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    static void   DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    static inline FLOAT Snap(FLOAT v){ return (FLOAT)((int)(v + 0.5f)); }

    // ---- helpers for navigation ----
    bool IsSelectable(int idx) const;            // is item enabled & not separator?
    int  FindNextSelectable(int start, int dir) const; // step up/down to next valid row

    // ---- state ----
    Item  m_items[24];   // fixed-capacity list of menu rows
    int   m_count;       // number of items in the list
    int   m_sel;         // currently highlighted row index
    char  m_label[64];

    bool  m_open;        // true if menu is open
    bool  m_waitRelease; // absorbs the button press that opened the menu

    ContextMenu* m_parentMenu; // NEW: pointer to parent menu (for submenu return)

    // ---- layout ----
    float m_x, m_y;      // top-left position
    float m_w;           // width
    float m_rowH;        // row height (pixels)
    LPDIRECT3DDEVICE8 m_dev;

    // ---- input edge detection ----
    unsigned char m_prevA, m_prevB, m_prevX, m_prevWhite, m_prevBlack;
    DWORD         m_prevButtons;
};
