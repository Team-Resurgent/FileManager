#include "OnScreenKeyboard.h"
#include "GfxPrims.h"
#include <stdio.h>
#include <string.h>
#include <wchar.h>   // for MultiByteToWideChar used by helpers

/*
============================================================================
 OnScreenKeyboard
  Modal on-screen keyboard used for renaming / text entry.
  - Two layouts: Alpha (QWERTY + digits) and Symbols
  - Side column actions: Done, Shift (one-shot), Caps (toggle), ABC/Symbols toggle
  - Bottom action row: Backspace | Space
  - FATX-safe length (42) enforced
============================================================================
*/

// ---------- local draw/measure helpers for the header path (marquee) ----------
static inline FLOAT KB_Snap(FLOAT v){ return (FLOAT)((int)(v + 0.5f)); }


// ------------------ Marquee for the path area ------------------
struct KB_Marquee {
    FLOAT px;         // current start index (in CHARACTERS, not pixels)
    FLOAT fitWLock;   // lock of visible width for stable end calc
    DWORD nextTick;   // next update time
    DWORD resetPause; // nonzero while pausing at end
    char  last[512];  // last path used (for reset-on-change)
    KB_Marquee() : px(0.0f), fitWLock(0.0f), nextTick(0), resetPause(0) { last[0] = 0; }
};
static KB_Marquee g_kbMarq;



// Draw the header path centered vertically in a band [x,y,w,h].
// Fixed "In:" label + path. When the path is too long: character-step marquee
// with initial/end pauses (mirrors PaneRenderer).
static void KB_DrawHeaderPath_FixedLabel(
    CXBFont& font, FLOAT x, FLOAT y, FLOAT w, FLOAT h,
    DWORD color, const char* parentPath)
{
    if (!parentPath) parentPath = "";

    // --- layout constants (match pane approach) ---
    const FLOAT leftPad     = 6.0f;
    const FLOAT rightPad    = 6.0f;
    const FLOAT gapPx       = 4.0f;     // space between "In:" and the path
    const FLOAT kTol        = 2.0f;     // measurement slack
    const FLOAT kBiasDown   = 1.0f;     // tiny downward bias
    const FLOAT kRightGuard = 1.5f;     // leave a hair on the right

    const DWORD kInitPauseMs = 900;
    const DWORD kStepMs      = 150;
    const DWORD kEndPauseMs  = 1200;
    const int   kStepChars   = 1;

    // Full band for label + path
    const FLOAT bandX = x + leftPad;
    const FLOAT bandW = (w > leftPad + rightPad) ? (w - leftPad - rightPad) : 0.0f;
    if (bandW <= 0.0f) return;

    // ---- draw the fixed "In:" label ----
    FLOAT inW=0, inH=0;
    {
        WCHAR wIn[16];
        MultiByteToWideChar(CP_ACP, 0, "In:", -1, wIn, 16);
        font.GetTextExtent(wIn, &inW, &inH);
        const FLOAT ty = KB_Snap(y + (h - inH) * 0.5f + kBiasDown);
        font.DrawText(KB_Snap(bandX), ty, color, wIn, 0, 0.0f);
    }

    // ---- path draw area (to the right of "In:") ----
    const FLOAT pathX = bandX + inW + gapPx;
    FLOAT       pathW = bandW - inW - gapPx;
    if (pathW <= 0.0f) return;

    const FLOAT fitW_now = (pathW > kRightGuard) ? (pathW - kRightGuard) : 0.0f;

    // Measure full path once
    WCHAR wPath[1024];
    MultiByteToWideChar(CP_ACP, 0, parentPath, -1, wPath, 1024);
    FLOAT fullW=0, fullH=0; font.GetTextExtent(wPath, &fullW, &fullH);
    const FLOAT ty = KB_Snap(y + (h - fullH) * 0.5f + kBiasDown);

    // If it fits (or nearly fits), draw and reset
    if (fullW <= fitW_now + kTol){
        font.DrawText(KB_Snap(pathX), ty, color, wPath, 0, 0.0f);
        g_kbMarq = KB_Marquee(); // zero it
        return;
    }

    // Reset marquee when text changes
    if (_stricmp(g_kbMarq.last, parentPath) != 0){
        _snprintf(g_kbMarq.last, sizeof(g_kbMarq.last), "%s", parentPath);
        g_kbMarq.last[sizeof(g_kbMarq.last)-1] = 0;
        g_kbMarq.px = 0.0f; g_kbMarq.fitWLock = 0.0f; g_kbMarq.resetPause = 0; g_kbMarq.nextTick = GetTickCount() + kInitPauseMs;
    }

    const DWORD now = GetTickCount();
    if (g_kbMarq.fitWLock <= 0.0f) g_kbMarq.fitWLock = fitW_now;
    const FLOAT fitW = (FLOAT)((int)(g_kbMarq.fitWLock + 0.5f)); // snap compare width

    const char* s   = parentPath;
    const int   len = (int)strlen(s);

    // Find earliest start index where the whole tail fits (binary search)
    int lo = 0, hi = len;
    while (lo < hi){
        const int mid = (lo + hi) / 2;
        WCHAR wtmp[1024]; MultiByteToWideChar(CP_ACP, 0, s + mid, -1, wtmp, 1024);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kTol) hi = mid; else lo = mid + 1;
    }
    const int lastStart = (lo > len) ? len : lo;

    // Current start index (stored as float)
    int startIdx = (int)g_kbMarq.px;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > lastStart) startIdx = lastStart;

    // Longest substring from s+startIdx that fits (binary search)
    const char* startPtr = s + startIdx;
    const int remaining  = len - startIdx;
    int lo2 = 0, hi2 = remaining;
    while (lo2 < hi2){
        const int mid = (lo2 + hi2 + 1) / 2;
        char tmp[1024]; _snprintf(tmp, sizeof(tmp), "%.*s", mid, startPtr);
        WCHAR wtmp[1024]; MultiByteToWideChar(CP_ACP, 0, tmp, -1, wtmp, 1024);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kTol) lo2 = mid; else hi2 = mid - 1;
    }

    // Draw visible slice
    char vis[1024]; _snprintf(vis, sizeof(vis), "%.*s", lo2, startPtr); vis[sizeof(vis)-1]=0;
    WCHAR wvis[1024]; MultiByteToWideChar(CP_ACP, 0, vis, -1, wvis, 1024);
    font.DrawText(KB_Snap(pathX), ty, color, wvis, 0, 0.0f);

    // Step/pause/reset (exactly like PaneRenderer)
    if (now >= g_kbMarq.nextTick){
        if (g_kbMarq.resetPause){
            g_kbMarq.px        = 0.0f;
            g_kbMarq.resetPause= 0;
            g_kbMarq.nextTick  = now + kInitPauseMs;
        } else {
            if (startIdx >= lastStart){
                g_kbMarq.px        = (FLOAT)lastStart;
                g_kbMarq.resetPause= now + kEndPauseMs;
                g_kbMarq.nextTick  = g_kbMarq.resetPause;
            } else {
                g_kbMarq.px       = (FLOAT)(startIdx + kStepChars);
                g_kbMarq.nextTick = now + kStepMs;
            }
        }
    }
}


// ------------------ Local keyboard layouts ------------------
// Alpha/number layer (your original). Rows target 10 keys each.
namespace {
    static const char s_kb_a0[] = "1234567890";     // row 0 (10)
    static const char s_kb_a1[] = "QWERTYUIOP";     // row 1 (10)
    static const char s_kb_a2[] = "ASDFGHJKL-";     // row 2 (10)
    static const char s_kb_a3[] = "ZXCVBNM_@.";     // row 3 (10)

    // Symbols layer: 5 raw rows. We normalize to keep first 4 rows at 10 keys.
    static const char s_kb_s0[] = "1234567890";       // row 0
    static const char s_kb_s1[] = ",;:'\"!?¡¿%";      // row 1
    static const char s_kb_s2[] = "[]{}\\`$£«»";      // row 2
    static const char s_kb_s3[] = "<>()^~¥|=&";       // row 3
    static const char s_kb_s4[] = "#*/+-@_.€©";       // row 4 (may be short; €/© may be missing in some fonts)
}

// ------------------ FATX params ------------------
static const int kFatxMaxName = 42; // includes extension; dashboards mirror this

// ------------------------------------------------------------
// Glyph filtering control (set to 0 if your font supports more)
// Some XPR fonts won’t render certain CP-1252 glyphs (€, ©).
// ------------------------------------------------------------
#ifndef OSK_FILTER_MISSING_GLYPHS
#define OSK_FILTER_MISSING_GLYPHS 1
#endif

// Only hide glyphs that the current XPR font can’t render.
// Keep this strict—better to omit than draw tofu boxes.
static bool IsGlyphSupported(char c) {
#if OSK_FILTER_MISSING_GLYPHS
    unsigned char uc = (unsigned char)c;  // CP-1252
    if (uc == 0x80 /*€*/ || uc == 0xA9 /*©*/) return false;
#endif
    return true;
}

// Filter a raw layout row down to visible glyphs (NUL-terminated).
// Returns number of characters copied.
static int BuildVisibleRow(const char* raw, char* out, int cap) {
    int n = 0;
    for (const char* p = raw; *p && n < cap - 1; ++p) {
        if (IsGlyphSupported(*p)) out[n++] = *p;
    }
    out[n] = 0;
    return n;
}

// Helper: map symbol row index to its raw string.
static const char* SymRawRow(int r) {
    return (r==0) ? s_kb_s0 :
           (r==1) ? s_kb_s1 :
           (r==2) ? s_kb_s2 :
           (r==3) ? s_kb_s3 :
                    s_kb_s4;
}

// Build symbol rows ensuring rows 0..3 end with exactly 10 keys (borrowing
// from later rows if needed). Row 4 may end up short. This keeps a clean grid.
static void BuildSymbolRowsNormalized(char outRows[5][16], int outCols[5]) {
    int r, i, j;

    // 1) Filter each raw row (preserve order)
    for (r = 0; r < 5; ++r) {
        outCols[r] = BuildVisibleRow(SymRawRow(r), outRows[r], 16);
    }

    // 2) Borrow forward so rows 0..3 reach 10 keys
    for (r = 0; r < 4; ++r) {
        while (outCols[r] < 10) {
            int takeFrom = -1;
            for (j = r + 1; j < 5; ++j) {
                if (outCols[j] > 0) { takeFrom = j; break; }
            }
            if (takeFrom < 0) break; // nothing left to borrow

            // Pop front from donor row
            char moved = outRows[takeFrom][0];
            for (i = 0; i < outCols[takeFrom]-1; ++i)
                outRows[takeFrom][i] = outRows[takeFrom][i+1];
            outCols[takeFrom]--;
            outRows[takeFrom][outCols[takeFrom]] = 0;

            // Append to current row
            if (outCols[r] < 15) {
                outRows[r][outCols[r]++] = moved;
                outRows[r][outCols[r]] = 0;
            }
        }
    }
}

// counts-only variant for navigation bounds
static void ComputeNormalizedSymbolCounts(int outCols[5]) {
    char tmp[5][16];
    BuildSymbolRowsNormalized(tmp, outCols);
}

// VC7.1-safe row length helper (no lambdas).
// For symbols: rows 0..4 are characters, row 5 is action row.
// For alpha: rows 0..3 are characters, row 4 is action row.
static int VisibleColsForRowHelper(bool symbols, int r) {
    if (!symbols) {
        if (r >= 4) return 2; // Backspace | Space
        const char* raw =
            (r==0) ? s_kb_a0 :
            (r==1) ? s_kb_a1 :
            (r==2) ? s_kb_a2 :
                     s_kb_a3;
        char vis[32];
        return BuildVisibleRow(raw, vis, sizeof(vis));
    } else {
        if (r == 5) return 2; // Backspace | Space (symbols mode)
        int cols[5];
        ComputeNormalizedSymbolCounts(cols);
        return (r >= 0 && r < 5) ? cols[r] : 0;
    }
}

// ------------------ ctor / open / close ------------------
OnScreenKeyboard::OnScreenKeyboard(){
    m_active = false;
    m_lower = false;     // false = UPPER, true = lower (caps state for alpha)
    m_symbols = false;   // false = ABC, true = Symbols
    m_waitRelease = false;

    m_parent[0] = m_old[0] = m_buf[0] = 0;
    m_cursor = 0; m_row = 0; m_col = 0;

    m_prevA = m_prevB = m_prevY = m_prevLT = m_prevRT = m_prevX = 0;
    m_prevButtons = 0;

    // Side-column UI focus (Done/Shift/Caps/Symbols)
    m_sideFocus = false;
    m_sideRow   = 0;
    m_shiftOnce = false; // one-shot shift toggle in alpha mode
}

void OnScreenKeyboard::Open(const char* parentDir, const char* initialName, bool startLowerCase){
    _snprintf(m_parent, sizeof(m_parent), "%s", parentDir ? parentDir : "");
    m_parent[sizeof(m_parent)-1] = 0;

    _snprintf(m_old, sizeof(m_old), "%s", initialName ? initialName : "");
    m_old[sizeof(m_old)-1] = 0;

    _snprintf(m_buf, sizeof(m_buf), "%s", initialName ? initialName : "");
    m_buf[sizeof(m_buf)-1] = 0;

    // Clamp to FATX limit
    int len0 = (int)strlen(m_buf);
    if (len0 > kFatxMaxName) {
        m_buf[kFatxMaxName] = 0;
        len0 = kFatxMaxName;
    }

    m_cursor = len0;
    m_row = 0; m_col = 0;
    m_lower = startLowerCase;
    m_symbols = false;

    // side column / shift state
    m_sideFocus = false;
    m_sideRow   = 0;
    m_shiftOnce = false;

    // Activate + reset edges; require a full release before accepting input
    m_active = true;
    m_prevA = m_prevB = m_prevY = m_prevLT = m_prevRT = 0;
    m_prevButtons = 0;
    m_waitRelease = true;
}

void OnScreenKeyboard::Close(){
    m_active = false;
}

// ------------------ helpers (class methods) ------------------
char OnScreenKeyboard::KbCharAt(int row, int col) const{
    if (m_symbols) {
        if (row < 0 || row > 4) return 0; // 5 symbol rows
        char rows[5][16];
        int  cols[5];
        BuildSymbolRowsNormalized(rows, cols);
        if (col >= 0 && col < cols[row]) {
            return rows[row][col];
        }
        return 0;
    } else {
        if (row < 0 || row > 3) return 0;
        const char* raw =
            (row==0) ? s_kb_a0 :
            (row==1) ? s_kb_a1 :
            (row==2) ? s_kb_a2 :
                       s_kb_a3;

        char vis[32];
        int cols = BuildVisibleRow(raw, vis, sizeof(vis));
        if (col < 0 || col >= cols) return 0;

        char ch = vis[col];

        // Effective case: caps ^ shiftOnce (alpha only)
        bool lowerEff = m_lower;
        if (!m_symbols && m_shiftOnce) lowerEff = !lowerEff;
        if (!m_symbols && lowerEff && ch >= 'A' && ch <= 'Z')
            ch = (char)(ch + ('a' - 'A'));
        return ch;
    }
}

// ANSI -> wide convenience; avoids repeating conversion boilerplate.
void OnScreenKeyboard::DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
    WCHAR wbuf[512];
    MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
    font.DrawText(x,y,color,wbuf,0,0.0f);
}

// Simple filled rectangle using shared gfx primitive.
void OnScreenKeyboard::DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(dev, x, y, w, h, c);  // from GfxPrims.h
}

// Text measurement helpers (ANSI inputs, wide for CXBFont).
void OnScreenKeyboard::MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH){
    WCHAR wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 256);
    font.GetTextExtent(wbuf, &outW, &outH);
}
FLOAT OnScreenKeyboard::MeasureTextW(CXBFont& font, const char* s){
    FLOAT w=0,h=0; MeasureTextWH(font, s, w, h); return w;
}

// ------------------ draw ------------------
void OnScreenKeyboard::Draw(CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT lineH){
    if (!m_active) return;

    D3DVIEWPORT8 vp; 
    dev->GetViewport(&vp);

    // --- layout constants used for both sizing and drawing ---
    const FLOAT headerH       = 32.0f;      // title band
    const FLOAT afterLinePad  = 10.0f;      // gap under divider
    const FLOAT labelToBoxPad = 12.0f;      // gap to input box
    const FLOAT boxH          = 30.0f;      // input box height
    const FLOAT gridTopGap    = 16.0f;      // gap before key grid
    const FLOAT gapY          = 4.0f;       // vertical gap between rows
    const FLOAT gapX          = 6.0f;       // horizontal gap between keys
    const FLOAT infoBandH     = (lineH > 22.0f ? lineH : 22.0f);
    const FLOAT cellH         = lineH + 6.0f;   // key cell height
    const FLOAT footerH       = 26.0f;          // room for footer hints

    const int   charRows      = m_symbols ? 5 : 4;

    // --- panel geometry (auto-height; grow to fit content, then clamp) ---
    const FLOAT panelW = MaxF(520.0f, vp.Width  * 0.55f);

    const FLOAT contentNeededH =
        headerH + 1.0f /*divider*/ + afterLinePad + infoBandH + labelToBoxPad + boxH +
        gridTopGap + (charRows * (cellH + gapY)) + cellH /*bottom row*/ + footerH;

    FLOAT panelH = MaxF(320.0f, vp.Height * 0.52f);
    if (panelH < contentNeededH) panelH = contentNeededH;
    const FLOAT maxH = vp.Height - 20.0f; // breathing room so frame fits
    if (panelH > maxH) panelH = maxH;

    const FLOAT x = KB_Snap((vp.Width  - panelW) * 0.55f);
    const FLOAT y = KB_Snap((vp.Height - panelH) * 0.5f);

    // frame
    DrawRect(dev, x-8, y-8, panelW+16, panelH+16, 0xA0101010);
    DrawRect(dev, x,   y,   panelW,    panelH,    0xE0222222);

    // --- header ---
    FLOAT titleW, titleH; MeasureTextWH(font, "Rename", titleW, titleH);
    const FLOAT titleY = KB_Snap(y + (headerH - titleH) * 0.5f);
    DrawAnsi(font, x + 12, titleY, 0xFFFFFFFF, "Rename");

    {
        int len = (int)strlen(m_buf);
        char cnt[32];
        _snprintf(cnt, sizeof(cnt), "%d/%d", len, kFatxMaxName);
        cnt[sizeof(cnt)-1] = 0;

        DWORD cntCol = 0xFFCCCCCC;
        if (len >= kFatxMaxName)        cntCol = 0xFFFF6060;
        else if (len >= kFatxMaxName-4) cntCol = 0xFFEED060;

        FLOAT cntW = MeasureTextW(font, cnt);
        DrawAnsi(font, KB_Snap(x + panelW - 12.0f - cntW), titleY, cntCol, cnt);
    }

    // Divider under header
    DrawRect(dev, x, y + headerH, panelW, 1.0f, 0x60FFFFFF);

    // ----- Path band ("In: <parent>") with safe-fit + end-stop marquee -----
    const FLOAT infoY      = KB_Snap(y + headerH + afterLinePad);
    const FLOAT infoBandW  = panelW - 24.0f;                      // x+12 .. x+panelW-12
    KB_DrawHeaderPath_FixedLabel(font, x + 12.0f, infoY, infoBandW, infoBandH,
                                 0xFFCCCCCC, m_parent);

    // Input box
    const FLOAT boxY = KB_Snap(infoY + infoBandH + labelToBoxPad);
    DrawRect(dev, x+12, boxY, panelW-24, boxH, 0xFF0E0E0E);

    // Current name text
    DrawAnsi(font, x+18, boxY+4, 0xFFFFFF00, m_buf);

    // Caret at current insertion point
    char tmp = m_buf[m_cursor]; m_buf[m_cursor]=0;
    FLOAT caretX = KB_Snap(x+18 + MeasureTextW(font, m_buf));
    m_buf[m_cursor]=tmp;
    DrawRect(dev, caretX, boxY+4, 2.0f, boxH-8.0f, 0x90FFFF00);

    // Grid layout (side column + key grid)
    const FLOAT padX     = 12.0f;
    const FLOAT contentW = panelW - 2.0f*padX;
    const FLOAT gridTop  = KB_Snap(boxY + boxH + gridTopGap);

    // Side column width uses ~2 of 12 columns
    const FLOAT colW12_full = contentW / 12.0f;
    const FLOAT sideW = MaxF(130.0f, colW12_full * 2.2f);
    const FLOAT keysW = contentW - sideW - gapX;

    // ---- side column (Done / Shift / Caps / Symbols) ----
    const char* sideLbl[4] = {
        "Done",
        m_shiftOnce ? "Shift*" : "Shift",
        m_lower ? "Caps (L3)" : "Caps (L3)*",
        m_symbols ? "ABC (R3)" : "Symbols (R3)"
    };
    for (int r=0;r<4;++r){
        const FLOAT sx = x + padX;
        const FLOAT sw = sideW;
        const FLOAT sy = KB_Snap(gridTop + r*(cellH + gapY));

        const bool disabled = (m_symbols && (r == 1 || r == 2));
        const bool sel      = (!disabled && m_sideFocus && m_sideRow == r);

        D3DCOLOR bg = sel ? 0x60FFFF00 : 0x30202020;
        DrawRect(dev, sx, sy, sw, cellH, bg);

        FLOAT tw, th; MeasureTextWH(font, sideLbl[r], tw, th);
        const FLOAT tx = KB_Snap(sx + (sw - tw) * 0.5f);
        const FLOAT ty = KB_Snap(sy + (cellH - th) * 0.5f);

        DWORD textCol = disabled ? 0xFF7A7A7A : 0xFFE0E0E0;
        DrawAnsi(font, tx, ty, textCol, sideLbl[r]);
    }

    // ---- character rows (alpha: 4 rows, symbols: 5 rows) ----
    const FLOAT keysX = x + padX + sideW + gapX;

    char symRows[5][16];
    int  symCols[5];
    if (m_symbols) BuildSymbolRowsNormalized(symRows, symCols);

    for (int row = 0; row < charRows; ++row) {
        const FLOAT rowY = KB_Snap(gridTop + row * (cellH + gapY));

        const char* visChars;
        int cols;

        if (m_symbols) {
            visChars = symRows[row];
            cols     = symCols[row];
        } else {
            const char* raw =
                (row==0) ? s_kb_a0 :
                (row==1) ? s_kb_a1 :
                (row==2) ? s_kb_a2 :
                           s_kb_a3;
            static char vis[32];
            cols = BuildVisibleRow(raw, vis, sizeof(vis));
            visChars = vis;
        }

        const FLOAT colW = (cols > 0) ? (keysW / (FLOAT)cols) : keysW;

        for (int col = 0; col < cols; ++col) {
            const FLOAT x0 = KB_Snap(keysX + col * colW);
            const FLOAT x1 = KB_Snap(keysX + (col + 1) * colW);
            const FLOAT drawX = x0 + gapX * 0.5f;
            const FLOAT drawW = (x1 - x0) - gapX;
            const bool  sel   = (!m_sideFocus && m_row == row && m_col == col);

            DrawRect(dev, drawX, rowY, drawW, cellH, sel ? 0x60FFFF00 : 0x30202020);

            // Visualize applied case on alpha rows
            char c = visChars[col];
            if (!m_symbols && c >= 'A' && c <= 'Z') {
                bool lowerEff = m_lower;
                if (m_shiftOnce) lowerEff = !lowerEff;
                if (lowerEff) c = (char)(c + ('a' - 'A'));
            }

            char s[2] = { c, 0 };
            FLOAT tw, th; MeasureTextWH(font, s, tw, th);
            DrawAnsi(font, KB_Snap(drawX + (drawW - tw) * 0.5f),
                           KB_Snap(rowY  + (cellH - th) * 0.5f), 0xFFE0E0E0, s);
        }
    }

    // ---- bottom row: Backspace | Space (use same grid math as other rows) ----
	const FLOAT bottomY = KB_Snap(gridTop + charRows * (cellH + gapY));
	{
		const int   colsB  = 2;
		const FLOAT colWB  = keysW / (FLOAT)colsB;

		// snapped cell edges (exactly like x0/x1 in normal rows)
		const FLOAT e0 = KB_Snap(keysX + 0 * colWB);
		const FLOAT e1 = KB_Snap(keysX + 1 * colWB);
		const FLOAT e2 = KB_Snap(keysX + 2 * colWB);

		// Backspace cell
		const FLOAT bx = e0 + gapX * 0.5f;
		const FLOAT bw = (e1 - e0) - gapX;
		const bool  selBack = (!m_sideFocus && m_row == charRows && m_col == 0);
		DrawRect(dev, bx, bottomY, bw, cellH, selBack ? 0x60FFFF00 : 0x30202020);

		FLOAT tw, th; MeasureTextWH(font, "Backspace", tw, th);
		DrawAnsi(font,
				KB_Snap(bx + (bw - tw) * 0.5f),
				KB_Snap(bottomY + (cellH - th) * 0.5f),
				0xFFE0E0E0, "Backspace (X)");

		// Space cell
		const FLOAT sx = e1 + gapX * 0.5f;
		const FLOAT sw = (e2 - e1) - gapX;
		const bool  selSpace = (!m_sideFocus && m_row == charRows && m_col == 1);
		DrawRect(dev, sx, bottomY, sw, cellH, selSpace ? 0x60FFFF00 : 0x30202020);

		MeasureTextWH(font, "Space", tw, th);
		DrawAnsi(font,
				KB_Snap(sx + (sw - tw) * 0.5f),
				KB_Snap(bottomY + (cellH - th) * 0.5f),
				0xFFE0E0E0, "Space (Y)");
	}




    // Footer hints (centered)
    const char* hints = "A: Select   B: Cancel   Start: Done   LT/RT Move Cursor";
    FLOAT hintsW = MeasureTextW(font, hints);
    FLOAT hintsX = KB_Snap(x + (panelW - hintsW) * 0.5f);
    DrawAnsi(font, hintsX, y + panelH - 25, 0xFFBBBBBB, hints);
}

// ------------------ input ------------------
OnScreenKeyboard::Result OnScreenKeyboard::OnPad(const XBGAMEPAD& pad){
    if (!m_active) return NONE;

    const DWORD btn = pad.wButtons;

    // Analog buttons
    unsigned char a  = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    unsigned char b  = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    unsigned char y  = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
    unsigned char x  = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    unsigned char lt = pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER];
    unsigned char rt = pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER];

    // Debounce immediately after opening so the opener press doesn’t leak in.
    if (m_waitRelease) {
        bool anyHeld = (a > 30) || (b > 30) || (y > 30) || (x > 30) ||
                       (lt > 30) || (rt > 30) ||
                       (btn & XINPUT_GAMEPAD_START) ||
                       (btn & XINPUT_GAMEPAD_LEFT_THUMB) ||
                       (btn & XINPUT_GAMEPAD_RIGHT_THUMB);
        m_prevA = a; m_prevB = b; m_prevY = y; m_prevX = x;
        m_prevLT = lt; m_prevRT = rt; m_prevButtons = btn;

        if (!anyHeld) m_waitRelease = false;
        return NONE;
    }

    // How many character rows are visible in current layout?
    const int charRows = m_symbols ? 5 : 4;
    const int bottomActionRow = charRows;

    // Edge + analog navigation (thumbsticks act as D-pad with thresholds).
    bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)    && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))    || (pad.sThumbLY >  16000);
    bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN))  || (pad.sThumbLY < -16000);
    bool left  = ((btn & XINPUT_GAMEPAD_DPAD_LEFT)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_LEFT))  || (pad.sThumbLX < -16000);
    bool right = ((btn & XINPUT_GAMEPAD_DPAD_RIGHT) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) || (pad.sThumbLX >  16000);

    // Navigation: either side column is focused, or the key grid is.
    if (m_sideFocus){
        if (up) {
            if (m_sideRow > 0) m_sideRow--;
            // Skip disabled Shift/Caps when in Symbols
            if (m_symbols && (m_sideRow == 1 || m_sideRow == 2)) m_sideRow = 0;
            Sleep(120);
        }
        if (down) {
            if (m_sideRow < 3) m_sideRow++;
            if (m_symbols && (m_sideRow == 1 || m_sideRow == 2)) m_sideRow = 3;
            Sleep(120);
        }
        if (right){ m_sideFocus = false; Sleep(120); }
    } else {
        if (up)   { if (m_row > 0) m_row--; Sleep(120); }
        if (down) { if (m_row < bottomActionRow) m_row++; Sleep(120); }

        int colsNow = VisibleColsForRowHelper(m_symbols, m_row);
        if (m_col >= colsNow) m_col = colsNow - 1;

        if (left)  {
            if (m_col > 0) { m_col--; Sleep(120); }
            else {
                // Wrap focus into side column
                m_sideFocus = true;
                if (m_symbols && (m_sideRow == 1 || m_sideRow == 2))
                    m_sideRow = 0; // land on “Done”
                Sleep(120);
            }
        }
        if (right) {
            if (m_col < colsNow - 1) { m_col++; Sleep(120); }
        }
    }

    // Button edges
    bool aTrig     = (a > 30 && m_prevA <= 30);
    bool bTrig     = (b > 30 && m_prevB <= 30);
    bool xTrig     = (x > 30 && m_prevX <= 30);
    bool yTrig     = (y > 30 && m_prevY <= 30);
    bool startTrig = ((btn & XINPUT_GAMEPAD_START) && !(m_prevButtons & XINPUT_GAMEPAD_START));
    bool l3Trig    = ((btn & XINPUT_GAMEPAD_LEFT_THUMB)  && !(m_prevButtons & XINPUT_GAMEPAD_LEFT_THUMB));
    bool r3Trig    = ((btn & XINPUT_GAMEPAD_RIGHT_THUMB) && !(m_prevButtons & XINPUT_GAMEPAD_RIGHT_THUMB));

    // Y inserts a space at cursor (shortcut)
    if (yTrig) {
        int len = (int)strlen(m_buf);
        const int cap = (int)sizeof(m_buf) - 1;
        if (len < cap && len < kFatxMaxName) {
            for (int i = len; i >= m_cursor; --i) m_buf[i+1] = m_buf[i];
            m_buf[m_cursor++] = ' ';
        }
        Sleep(120);
    }

    if (aTrig){
        if (m_sideFocus){
            // Make disabled rows inert in Symbols
            if ((m_sideRow == 1 || m_sideRow == 2) && m_symbols) {
                Sleep(120);
            } else if (m_sideRow == 0){
                // Done
                m_prevA=a; m_prevB=b; m_prevY=y; m_prevButtons=btn; m_prevLT=lt; m_prevRT=rt;
                return ACCEPTED;
            } else if (m_sideRow == 1){
                // Shift (one-shot) — alpha only
                if (!m_symbols) m_shiftOnce = !m_shiftOnce;
                Sleep(140);
            } else if (m_sideRow == 2){
                // Caps — alpha only
                if (!m_symbols) m_lower = !m_lower;
                Sleep(140);
            } else if (m_sideRow == 3){
                // ABC/Symbols toggle; re-clamp selection
                m_symbols = !m_symbols;
                if (m_symbols && (m_sideRow == 1 || m_sideRow == 2)) m_sideRow = 3;
                int newCols = VisibleColsForRowHelper(m_symbols, m_row);
                if (m_col >= newCols) m_col = newCols - 1;
                Sleep(140);
            }
        } else {
            // Grid: type a character OR trigger bottom actions
            if (m_row <= (charRows-1)){ // character rows
                int len = (int)strlen(m_buf);
                const int cap = (int)sizeof(m_buf) - 1;
                if (len < cap && len < kFatxMaxName){
                    char ch = KbCharAt(m_row, m_col);
                    if (ch) {
                        for (int i=len; i>=m_cursor; --i) m_buf[i+1] = m_buf[i];
                        m_buf[m_cursor++] = ch;
                        if (m_shiftOnce) m_shiftOnce = false; // one-shot consumed
                    }
                }
            } else { // bottom row: Backspace | Space
                if (m_col == 0){ // Backspace
                    if (m_cursor > 0){
                        int len = (int)strlen(m_buf);
                        for (int i=m_cursor-1; i<=len; ++i) m_buf[i] = m_buf[i+1];
                        m_cursor--;
                    }
                } else { // Space
                    int len = (int)strlen(m_buf);
                    const int cap = (int)sizeof(m_buf) - 1;
                    if (len < cap && len < kFatxMaxName){
                        for (int i=len; i>=m_cursor; --i) m_buf[i+1] = m_buf[i];
                        m_buf[m_cursor++] = ' ';
                    }
                }
            }
            Sleep(140);
        }
    }

    // Start = Done
    if (startTrig){
        m_prevA=a; m_prevB=b; m_prevY=y; m_prevX=x; m_prevButtons=btn; m_prevLT=lt; m_prevRT=rt;
        return ACCEPTED;
    }
    // B = Cancel
    if (bTrig){
        m_prevA=a; m_prevB=b; m_prevY=y; m_prevX=x; m_prevButtons=btn; m_prevLT=lt; m_prevRT=rt;
        return CANCELED;
    }

    // L3 toggles Caps (alpha only)
    if (l3Trig) {
        if (!m_symbols) m_lower = !m_lower;
        Sleep(140);
    }

    // R3 toggles Symbols; re-clamp selection and side focus sanity
    if (r3Trig) {
        m_symbols = !m_symbols;
        if (m_sideFocus && m_symbols && (m_sideRow == 1 || m_sideRow == 2))
            m_sideRow = 3;
        int newCols = VisibleColsForRowHelper(m_symbols, m_row);
        if (m_col >= newCols) m_col = (newCols > 0) ? (newCols - 1) : 0;
        Sleep(140);
    }

    // X = Backspace (anywhere)
    if (xTrig) {
        if (m_cursor > 0) {
            int len = (int)strlen(m_buf);
            for (int i = m_cursor - 1; i <= len; ++i)
                m_buf[i] = m_buf[i + 1];
            m_cursor--;
        }
        Sleep(120);
    }

    // Triggers move caret (LT left, RT right)
    bool ltTrig = (lt > 30 && m_prevLT <= 30);
    bool rtTrig = (rt > 30 && m_prevRT <= 30);
    if (ltTrig){ if (m_cursor > 0) m_cursor--; }
    if (rtTrig){ int len=(int)strlen(m_buf); if (m_cursor < len) m_cursor++; }

    // Clamp caret within current text
    {
        int len=(int)strlen(m_buf);
        if (m_cursor < 0)   m_cursor = 0;
        if (m_cursor > len) m_cursor = len;
    }

    // Save edges
    m_prevA = a; m_prevB = b; m_prevY = y; m_prevX = x;
    m_prevButtons = btn; m_prevLT = lt; m_prevRT = rt;

    return NONE;
}
