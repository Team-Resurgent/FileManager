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
  - Bottom action row (in UI): Backspace | Space
  - FATX name length respected in .cpp (42 chars)
  - Uses CXBFont for text and simple D3D8 rects for UI
============================================================================
*/

class OnScreenKeyboard {
public:
    // Result of handling input in a frame:
    //  - NONE:      Still editing
    //  - ACCEPTED:  User confirmed (Start or "Done")
    //  - CANCELED:  User aborted (B)
    enum Result { NONE, ACCEPTED, CANCELED };

    OnScreenKeyboard();

    // Global default for case at open time.
    // Change this constant if you want new sessions to start lowercase/uppercase by default.
    static const bool kDefaultStartLowerCase = true;  // false => start UPPERCASE

    // Open the keyboard with:
    //  - parentDir: path shown in the header ("In: <parentDir>") — purely informational
    //  - initialName: starting text in the edit box (will be clamped to FATX max in .cpp)
    //  - startLowerCase: initial Caps mode for the Alpha layout
    void Open(const char* parentDir, const char* initialName, bool startLowerCase);

    // Convenience overload uses the class default for startLowerCase.
    inline void Open(const char* parentDir, const char* initialName) {
        Open(parentDir, initialName, kDefaultStartLowerCase);
    }

    // Close the keyboard (no state is returned/validated here).
    void Close();

    // Whether the keyboard is currently active/displayed.
    bool Active() const { return m_active; }

    // Feed one frame of gamepad input.
    // Returns ACCEPTED/CANCELED when the user finishes; caller should Close() and consume Buffer().
    Result OnPad(const XBGAMEPAD& pad);

    // Draw the keyboard:
    //  - font: CXBFont used everywhere for text
    //  - dev:  D3D8 device for rects and font rendering
    //  - lineH: baseline row height from your app (used to scale key cells)
    void Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT lineH);

    // After ACCEPTED, read the new text and the parent dir string you opened with.
    const char* Buffer() const { return m_buf; }
    const char* Parent() const { return m_parent; }

private:
    // ------- Utility helpers (implemented in .cpp) -------
    // Small math/drawing helpers (kept here for inlining on VS2003).
    static FLOAT MaxF(FLOAT a, FLOAT b){ return (a>b)?a:b; }
    static FLOAT Snap (FLOAT v)        { return (FLOAT)((int)(v + 0.5f)); }

    static void  DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    void         DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    void         MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH);
    FLOAT        MeasureTextW (CXBFont& font, const char* s);

    // Returns the character at the current layout’s [row,col] applying Caps/Shift rules.
    // (Symbols ignores Caps/Shift; Alpha respects them.)
    char         KbCharAt(int row, int col) const;

private:
    // ------- Session state -------
    bool  m_active;        // modal visible/enabled
    bool  m_lower;         // Alpha layout caps state: false=UPPER, true=lower (see .cpp)
    bool  m_symbols;       // false=Alpha (ABC), true=Symbols

    // Context + edit buffer
    char  m_parent[512];   // informational header ("In: <parent>")
    char  m_old[256];      // original name when opened (not used by logic, kept for reference)
    char  m_buf[256];      // editable text buffer (FATX length enforced in .cpp)
    int   m_cursor;        // caret index into m_buf

    // Grid selection (character keys)
    int   m_row, m_col;    // current selection in the key grid

    // Debounce after Open() so opener key doesn’t leak into editing.
    bool  m_waitRelease;

    // Side column focus (Done / Shift / Caps / ABC|Symbols)
    bool  m_sideFocus;     // true -> focus is in left control column
    int   m_sideRow;       // 0..3 (Done, Shift, Caps, Symbols/ABC)
    bool  m_shiftOnce;     // one-shot shift (applies to next Alpha char only)

    // ------- Input edge detection (self-contained; not tied to the app’s edges) -------
    unsigned char m_prevA, m_prevB, m_prevY, m_prevX, m_prevLT, m_prevRT;
    DWORD         m_prevButtons;

    // (Legacy/internal; not used directly by callers. Kept for VS2003 parity if needed later.)
    const char* RowChars(int row) const;
    int         RowCols (int row) const;
};
