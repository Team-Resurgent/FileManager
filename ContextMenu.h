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
    enum Result { NOOP, CHOSEN, CLOSED };

    ContextMenu();

    // Clear all menu items and reset selection
    void Clear();

    // Add a selectable menu item (label text, action ID, enabled/disabled)
    void AddItem(const char* label, Action act, bool enabled);

    // Add a visual separator line (non-selectable, just a divider)
    void AddSeparator(); 

    // Open menu at screen coordinates (x,y), with given width and row height
    void OpenAt(float x, float y, float width, float rowH);

    // Close the menu
    void Close();

    // Query: is the menu currently visible?
    bool IsOpen() const { return m_open; }

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
        const char* label;   // Display text (ANSI string)
        Action      act;     // Action ID (from AppActions.h)
        bool        enabled; // Disabled items are greyed out
        bool        separator; // True if this is just a divider row
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

    bool  m_open;        // true if menu is open
    bool  m_waitRelease; // absorbs the button press that opened the menu

    // ---- layout ----
    float m_x, m_y;      // top-left position
    float m_w;           // width
    float m_rowH;        // row height (pixels)

    // ---- input edge detection ----
    unsigned char m_prevA, m_prevB, m_prevX, m_prevWhite, m_prevBlack;
    DWORD         m_prevButtons;
};
