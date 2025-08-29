#pragma once
#include <xtl.h>
#include "XBFont.h"
#include "XBInput.h"
#include "AppActions.h"

// Lightweight context menu component: drawing + input + selection.
class ContextMenu {
public:
    enum Result { NOOP, CHOSEN, CLOSED };

    ContextMenu();

    void Clear();
    void AddItem(const char* label, Action act, bool enabled);

    // Open menu at x,y with given width and row height (pixels)
    void OpenAt(float x, float y, float width, float rowH);
    void Close();
    bool IsOpen() const { return m_open; }

    // Render menu
    void Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev) const;

    // Handle input; if CHOSEN, outAct receives the selected Action
    Result OnPad(const XBGAMEPAD& pad, Action& outAct);

private:
    struct Item { const char* label; Action act; bool enabled; };

    // helpers
    static void   DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    static void   DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    static inline FLOAT Snap(FLOAT v){ return (FLOAT)((int)(v + 0.5f)); }

    Item  m_items[12];
    int   m_count;
    int   m_sel;

    bool  m_open;
    bool  m_waitRelease; // prevents the 'X' that opened the menu from immediately selecting

    // layout
    float m_x, m_y, m_w, m_rowH;

    // input edge detection (local to menu)
    unsigned char m_prevA, m_prevB, m_prevX, m_prevWhite, m_prevBlack;
    DWORD         m_prevButtons;
};
