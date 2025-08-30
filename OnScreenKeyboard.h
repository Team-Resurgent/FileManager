#pragma once
#include <xtl.h>
#include <wchar.h>
#include <string.h>
#include "XBFont.h"
#include "XBInput.h"

class OnScreenKeyboard {
public:
    enum Result { NONE, ACCEPTED, CANCELED };

    OnScreenKeyboard();

    // --- Global default for case at open time (change here only) ---
    static const bool kDefaultStartLowerCase = true;  // flip to false for UPPERCASE by default

    // Open with parent directory and initial file/folder name
    void Open(const char* parentDir, const char* initialName, bool startLowerCase);
    inline void Open(const char* parentDir, const char* initialName) {
        Open(parentDir, initialName, kDefaultStartLowerCase);
    }

    void Close();
    bool Active() const { return m_active; }

    // Handle pad input; returns ACCEPTED / CANCELED when user finishes
    Result OnPad(const XBGAMEPAD& pad);

    // Draw the keyboard; pass your font/device + list row height for sizing
    void Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT lineH);

    // Read the buffer/parent after ACCEPTED
    const char* Buffer() const { return m_buf; }
    const char* Parent() const { return m_parent; }

private:
    // --- helpers ---
    static FLOAT MaxF(FLOAT a, FLOAT b){ return (a>b)?a:b; }
    static FLOAT Snap (FLOAT v)        { return (FLOAT)((int)(v + 0.5f)); }

    static void  DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    void         DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    void         MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH);
    FLOAT        MeasureTextW (CXBFont& font, const char* s);
    char         KbCharAt(int row, int col) const;

private:
    // --- state ---
    bool  m_active;
    bool  m_lower;
    bool  m_symbols;
    char  m_parent[512];
    char  m_old[256];
    char  m_buf[256];
    int   m_cursor;
    int   m_row, m_col;
    bool  m_waitRelease;
    bool  m_sideFocus;   // are we in the left control column?
    int   m_sideRow;     // 0..3 within the control column
    bool  m_shiftOnce;   // one-shot shift (case flips for next char)

    // input edge detection (internal; independent of FileBrowserApp)
    unsigned char m_prevA, m_prevB, m_prevY, m_prevLT, m_prevRT;
    DWORD         m_prevButtons;

    const char* RowChars(int row) const;
    int         RowCols (int row) const;
};
