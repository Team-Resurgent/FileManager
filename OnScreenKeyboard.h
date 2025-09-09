#pragma once
#include <xtl.h>
#include <wchar.h>
#include <string.h>
#include "XBFont.h"
#include "XBInput.h"

/*
============================================================================
 OnScreenKeyboard
  Modal on-screen keyboard for OG Xbox.
  - Two layouts: Alpha (QWERTY+digits) and Symbols
  - Side column actions: Done, Shift (one-shot), Caps, ABC/Symbols
  - Bottom action row: Backspace | Space
  - FATX name length respected in .cpp (42 chars)
  - Uses CXBFont for text and simple D3D8 rects for UI
============================================================================
*/

class OnScreenKeyboard {
public:
    enum Result { NONE, ACCEPTED, CANCELED };

    OnScreenKeyboard();

    static const bool kDefaultStartLowerCase = true;  // false => start UPPERCASE

    void   Open(const char* parentDir, const char* initialName, bool startLowerCase);
    inline void Open(const char* parentDir, const char* initialName) {
        Open(parentDir, initialName, kDefaultStartLowerCase);
    }

    void   Close();
    bool   Active() const { return m_active; }
    Result OnPad(const XBGAMEPAD& pad);
    void   Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT lineH);

    const char* Buffer() const { return m_buf; }
    const char* Parent() const { return m_parent; }

private:
    // Small math/drawing helpers (inline for VS2003).
    static FLOAT MaxF(FLOAT a, FLOAT b){ return (a>b)?a:b; }
    static void  DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    void         DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    void         MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH);
    FLOAT        MeasureTextW (CXBFont& font, const char* s);
    char         KbCharAt(int row, int col) const;

private:
    // ------- Session state -------
    bool  m_active;
    bool  m_lower;       // Alpha caps state: false=UPPER, true=lower
    bool  m_symbols;     // false=Alpha (ABC), true=Symbols

    char  m_parent[512]; // header ("In: <parent>")
    char  m_old[256];    // original name when opened
    char  m_buf[256];    // editable text buffer (FATX limit in .cpp)
    int   m_cursor;      // caret index

    // Grid selection (character keys)
    int   m_row, m_col;

    // Debounce after Open()
    bool  m_waitRelease;

    // Side column focus (Done / Shift / Caps / ABC|Symbols)
    bool  m_sideFocus;
    int   m_sideRow;     // 0..3
    bool  m_shiftOnce;   // one-shot shift for alpha

    // Input edge detection
    unsigned char m_prevA, m_prevB, m_prevY, m_prevX, m_prevLT, m_prevRT;
    DWORD         m_prevButtons;
};
