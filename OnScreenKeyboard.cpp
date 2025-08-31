#include "OnScreenKeyboard.h"
#include "GfxPrims.h"
#include <stdio.h>
#include <string.h>

// ------------------ Local keyboard layouts ------------------
// Alpha/number layer (your original)
namespace {
    static const char s_kb_a0[] = "1234567890";     // 10
    static const char s_kb_a1[] = "QWERTYUIOP";     // 10
    static const char s_kb_a2[] = "ASDFGHJKL-";     // 10
    static const char s_kb_a3[] = "ZXCVBNM_@.";     // 10

    // Symbols layer: 5 raw rows (rows 0..3 target 10 keys; last row may be short)
    static const char s_kb_s0[] = "1234567890";       // 10
    static const char s_kb_s1[] = ",;:'\"!?¡¿%";      // 10
    static const char s_kb_s2[] = "[]{}\\`$£«»";      // 10
    static const char s_kb_s3[] = "<>()^~¥|=&";       // 10
	static const char s_kb_s4[] = "#*/+-@_.€©";       // 10  (€ © may be unsupported in some fonts)
}

// ------------------ FATX params ------------------
static const int kFatxMaxName = 42; // includes extension

// ------------------------------------------------------------
// Glyph filtering control (set to 0 if your font supports more)
// ------------------------------------------------------------
#ifndef OSK_FILTER_MISSING_GLYPHS
#define OSK_FILTER_MISSING_GLYPHS 1
#endif

// Only hide glyphs that your current XPR font can't render.
static bool IsGlyphSupported(char c) {
#if OSK_FILTER_MISSING_GLYPHS
    unsigned char uc = (unsigned char)c;  // CP-1252
    if (uc == 0x80 /*€*/ || uc == 0xA9 /*©*/) return false;
#endif
    return true;
}

// Build a per-row character list that only includes supported glyphs.
// Returns count placed in 'out' (NUL-terminated).
static int BuildVisibleRow(const char* raw, char* out, int cap) {
    int n = 0;
    for (const char* p = raw; *p && n < cap - 1; ++p) {
        if (IsGlyphSupported(*p)) out[n++] = *p;
    }
    out[n] = 0;
    return n;
}

static const char* SymRawRow(int r) {
    return (r==0) ? s_kb_s0 :
           (r==1) ? s_kb_s1 :
           (r==2) ? s_kb_s2 :
           (r==3) ? s_kb_s3 :
                    s_kb_s4;
}

// Filter rows and rebalance so rows 0..3 have exactly 10 each;
// only the last row (index 4) is allowed to be short.
static void BuildSymbolRowsNormalized(char outRows[5][16], int outCols[5]) {
    int r, i, j;

    // 1) Filter each raw row (preserve order)
    for (r = 0; r < 5; ++r) {
        outCols[r] = BuildVisibleRow(SymRawRow(r), outRows[r], 16);
    }

    // 2) Pull characters forward so rows 0..3 each end with exactly 10
    for (r = 0; r < 4; ++r) {
        while (outCols[r] < 10) {
            int takeFrom = -1;
            for (j = r + 1; j < 5; ++j) {
                if (outCols[j] > 0) { takeFrom = j; break; }
            }
            if (takeFrom < 0) break; // nothing left to borrow

            char moved = outRows[takeFrom][0];
            for (i = 0; i < outCols[takeFrom]-1; ++i)
                outRows[takeFrom][i] = outRows[takeFrom][i+1];
            outCols[takeFrom]--;
            outRows[takeFrom][outCols[takeFrom]] = 0;

            if (outCols[r] < 15) {
                outRows[r][outCols[r]++] = moved;
                outRows[r][outCols[r]] = 0;
            }
        }
    }
}

// counts-only version for navigation
static void ComputeNormalizedSymbolCounts(int outCols[5]) {
    char tmp[5][16];
    BuildSymbolRowsNormalized(tmp, outCols);
}

// VC7.1-safe helper (replaces lambda): how many visible keys are in row r?
// For symbols: rows 0..4 are character rows. Bottom action row index = 5.
static int VisibleColsForRowHelper(bool symbols, int r) {
    if (!symbols) {
        if (r >= 4) return 2; // action row (Backspace | Space)
        const char* raw =
            (r==0) ? s_kb_a0 :
            (r==1) ? s_kb_a1 :
            (r==2) ? s_kb_a2 :
                     s_kb_a3;
        char vis[32];
        return BuildVisibleRow(raw, vis, sizeof(vis));
    } else {
        if (r == 5) return 2; // action row when in symbols
        int cols[5];
        ComputeNormalizedSymbolCounts(cols);
        return (r >= 0 && r < 5) ? cols[r] : 0;
    }
}

// ------------------ ctor / open / close ------------------
OnScreenKeyboard::OnScreenKeyboard(){
    m_active = false;
    m_lower = false;
    m_symbols = false;
    m_waitRelease = false;

    m_parent[0] = m_old[0] = m_buf[0] = 0;
    m_cursor = 0; m_row = 0; m_col = 0;

    m_prevA = m_prevB = m_prevY = m_prevLT = m_prevRT = m_prevX = 0;
    m_prevButtons = 0;

    // side-column UI
    m_sideFocus = false;
    m_sideRow   = 0;
    m_shiftOnce = false;
}

void OnScreenKeyboard::Open(const char* parentDir, const char* initialName, bool startLowerCase){
    _snprintf(m_parent, sizeof(m_parent), "%s", parentDir ? parentDir : "");
    m_parent[sizeof(m_parent)-1] = 0;

    _snprintf(m_old, sizeof(m_old), "%s", initialName ? initialName : "");
    m_old[sizeof(m_old)-1] = 0;

    _snprintf(m_buf, sizeof(m_buf), "%s", initialName ? initialName : "");
    m_buf[sizeof(m_buf)-1] = 0;

    // Enforce FATX max (truncate safely if needed)
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

        bool lowerEff = m_lower;
        if (!m_symbols && m_shiftOnce) lowerEff = !lowerEff;
        if (!m_symbols && lowerEff && ch >= 'A' && ch <= 'Z')
            ch = (char)(ch + ('a' - 'A'));
        return ch;
    }
}

void OnScreenKeyboard::DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
    WCHAR wbuf[512];
    MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
    font.DrawText(x,y,color,wbuf,0,0.0f);
}

void OnScreenKeyboard::DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(dev, x, y, w, h, c);  // from GfxPrims.h
}

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

    D3DVIEWPORT8 vp; dev->GetViewport(&vp);

    const FLOAT panelW = MaxF(520.0f, vp.Width * 0.55f);
    const FLOAT panelH = MaxF(320.0f, vp.Height*0.52f); // a bit taller to fit 5 rows
    const FLOAT x = Snap((vp.Width  - panelW)*0.55f);
    const FLOAT y = Snap((vp.Height - panelH)*0.5f);

    // frame
    DrawRect(dev, x-8, y-8, panelW+16, panelH+16, 0xA0101010);
    DrawRect(dev, x, y, panelW, panelH, 0xE0222222);

    // --- header layout (more vertical breathing room) ---
    const FLOAT headerH       = 32.0f;  // height of the title bar area
    const FLOAT afterLinePad  = 10.0f;  // gap under the divider
    const FLOAT labelToBoxPad = 12.0f;  // gap between "In:" and input box

    // Title centered in the header band
    FLOAT titleW, titleH; MeasureTextWH(font, "Rename", titleW, titleH);
    const FLOAT titleY = Snap(y + (headerH - titleH) * 0.5f);
    DrawAnsi(font, x + 12, titleY, 0xFFFFFFFF, "Rename");

    // Length indicator (right-aligned), vertically aligned with title
    {
        int len = (int)strlen(m_buf);
        char cnt[32];
        _snprintf(cnt, sizeof(cnt), "%d/%d", len, kFatxMaxName);
        cnt[sizeof(cnt)-1] = 0;

        DWORD cntCol = 0xFFCCCCCC;
        if (len >= kFatxMaxName)        cntCol = 0xFFFF6060;
        else if (len >= kFatxMaxName-4) cntCol = 0xFFEED060;

        FLOAT cntW = MeasureTextW(font, cnt);
        DrawAnsi(font, Snap(x + panelW - 12.0f - cntW), titleY, cntCol, cnt);
    }

    // Divider under the header band
    DrawRect(dev, x, y + headerH, panelW, 1.0f, 0x60FFFFFF);

    // "In: <path>" placed with padding under the divider
    char hdr[640];
    _snprintf(hdr, sizeof(hdr), "In: %s", m_parent); hdr[sizeof(hdr)-1]=0;
    FLOAT infoW, infoH; MeasureTextWH(font, hdr, infoW, infoH);
    const FLOAT infoY = Snap(y + headerH + afterLinePad);
    DrawAnsi(font, x + 12, infoY, 0xFFCCCCCC, hdr);

    // Input box positioned using measured label height + padding
    const FLOAT boxY = Snap(infoY + infoH + labelToBoxPad);
    const FLOAT boxH = 30.0f;
    DrawRect(dev, x+12, boxY, panelW-24, boxH, 0xFF0E0E0E);

    // current name
    DrawAnsi(font, x+18, boxY+4, 0xFFFFFF00, m_buf);

    // caret
    char tmp = m_buf[m_cursor]; m_buf[m_cursor]=0;
    FLOAT caretX = Snap(x+18 + MeasureTextW(font, m_buf));
    m_buf[m_cursor]=tmp;
    DrawRect(dev, caretX, boxY+4, 2.0f, boxH-8.0f, 0x90FFFF00);

    // layout numbers
    const FLOAT padX     = 12.0f;
    const FLOAT contentW = panelW - 2.0f*padX;
    const FLOAT cellH    = lineH + 6.0f;
    const FLOAT gridTop  = Snap(boxY + boxH + 16.0f);
    const FLOAT gapX     = 6.0f;
    const FLOAT gapY     = 4.0f;

    // Split content into side column + character grid
    const FLOAT colW12_full = contentW / 12.0f;
    const FLOAT sideW = MaxF(130.0f, colW12_full * 2.2f);
    const FLOAT keysW = contentW - sideW - gapX;

    // ---- side column (4 buttons stacked) ----
    const char* sideLbl[4] = {
        "Done",
        m_shiftOnce ? "Shift*" : "Shift",
		m_lower ? "Caps (L3)" : "Caps (L3)*",
        m_symbols ? "ABC (R3)" : "Symbols (R3)"
    };
    for (int r=0;r<4;++r){
		const FLOAT sx = x + padX;
		const FLOAT sw = sideW;
		const FLOAT sy = Snap(gridTop + r*(cellH + gapY));

		const bool disabled = (m_symbols && (r == 1 || r == 2)); // Shift/Caps disabled in Symbols
		const bool sel      = (!disabled && m_sideFocus && m_sideRow == r);

		D3DCOLOR bg = sel ? 0x60FFFF00 : 0x30202020;
		DrawRect(dev, sx, sy, sw, cellH, bg);

		FLOAT tw, th; MeasureTextWH(font, sideLbl[r], tw, th);
		const FLOAT tx = Snap(sx + (sw - tw) * 0.5f);
		const FLOAT ty = Snap(sy + (cellH - th) * 0.5f);

		DWORD textCol = disabled ? 0xFF7A7A7A : 0xFFE0E0E0;
		DrawAnsi(font, tx, ty, textCol, sideLbl[r]);
	}

    // ---- character rows ----
    const FLOAT keysX = x + padX + sideW + gapX;

    char symRows[5][16];
    int  symCols[5];
    if (m_symbols) {
        BuildSymbolRowsNormalized(symRows, symCols);
    }

    const int charRows = m_symbols ? 5 : 4; // symbols show 5 rows, alpha shows 4

    for (int row = 0; row < charRows; ++row) {
        const FLOAT rowY = Snap(gridTop + row * (cellH + gapY));

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
            const FLOAT x0 = Snap(keysX + col * colW);
            const FLOAT x1 = Snap(keysX + (col + 1) * colW);
            const FLOAT drawX = x0 + gapX * 0.5f;
            const FLOAT drawW = (x1 - x0) - gapX;
            const bool  sel   = (!m_sideFocus && m_row == row && m_col == col);

            DrawRect(dev, drawX, rowY, drawW, cellH, sel ? 0x60FFFF00 : 0x30202020);

			char c = visChars[col];
			if (!m_symbols && c >= 'A' && c <= 'Z') {
				bool lowerEff = m_lower;
				if (m_shiftOnce) lowerEff = !lowerEff; // one-shot inverts caps
				if (lowerEff) c = (char)(c + ('a' - 'A')); // show lowercase
			}

            char s[2] = { c, 0 };
            FLOAT tw, th; MeasureTextWH(font, s, tw, th);
            DrawAnsi(font, Snap(drawX + (drawW - tw) * 0.5f),
                           Snap(rowY  + (cellH - th) * 0.5f), 0xFFE0E0E0, s);
        }
    }

    // ---- bottom row: Backspace | Space ----
    const FLOAT bottomY = Snap(gridTop + charRows*(cellH + gapY));
    {
        const FLOAT bw = keysW * 0.33f;
        const FLOAT bx = keysX;
        const bool sel = (!m_sideFocus && m_row==(charRows) && m_col==0);
        DrawRect(dev, bx, bottomY, bw, cellH, sel ? 0x60FFFF00 : 0x30202020);

        FLOAT tw, th; MeasureTextWH(font, "Backspace", tw, th);
        DrawAnsi(font, Snap(bx + (bw - tw) * 0.5f), Snap(bottomY + (cellH - th) * 0.5f), 0xFFE0E0E0, "Backspace (X)");
    }
    {
        const FLOAT sw = keysW * 0.64f;
        const FLOAT sx2 = keysX + keysW - sw;
        const bool sel = (!m_sideFocus && m_row==(charRows) && m_col==1);
        DrawRect(dev, sx2, bottomY, sw, cellH, sel ? 0x60FFFF00 : 0x30202020);

        FLOAT tw, th; MeasureTextWH(font, "Space", tw, th);
        DrawAnsi(font, Snap(sx2 + (sw - tw) * 0.5f), Snap(bottomY + (cellH - th) * 0.5f), 0xFFE0E0E0, "Space (Y)");
    }

    // centered footer hints
    const char* hints = "A: Select   B: Cancel   Start: Done   LT/RT Move Cursor";
	FLOAT hintsW = MeasureTextW(font, hints);
	FLOAT hintsX = Snap(x + (panelW - hintsW) * 0.5f);
	DrawAnsi(font, hintsX, y + panelH - 25, 0xFFBBBBBB, hints);
	}


// ------------------ input ------------------
OnScreenKeyboard::Result OnScreenKeyboard::OnPad(const XBGAMEPAD& pad){
    if (!m_active) return NONE;

    const DWORD btn = pad.wButtons;

    unsigned char a  = pad.bAnalogButtons[XINPUT_GAMEPAD_A];
    unsigned char b  = pad.bAnalogButtons[XINPUT_GAMEPAD_B];
    unsigned char y  = pad.bAnalogButtons[XINPUT_GAMEPAD_Y];
	unsigned char x  = pad.bAnalogButtons[XINPUT_GAMEPAD_X];
    unsigned char lt = pad.bAnalogButtons[XINPUT_GAMEPAD_LEFT_TRIGGER];
    unsigned char rt = pad.bAnalogButtons[XINPUT_GAMEPAD_RIGHT_TRIGGER];

    // Debounce after opening
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

    // how many character rows are visible?
    const int charRows = m_symbols ? 5 : 4;
    const int bottomActionRow = charRows;

    // edge + analog navigation
    bool up    = ((btn & XINPUT_GAMEPAD_DPAD_UP)    && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_UP))    || (pad.sThumbLY >  16000);
    bool down  = ((btn & XINPUT_GAMEPAD_DPAD_DOWN)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_DOWN))  || (pad.sThumbLY < -16000);
    bool left  = ((btn & XINPUT_GAMEPAD_DPAD_LEFT)  && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_LEFT))  || (pad.sThumbLX < -16000);
    bool right = ((btn & XINPUT_GAMEPAD_DPAD_RIGHT) && !(m_prevButtons & XINPUT_GAMEPAD_DPAD_RIGHT)) || (pad.sThumbLX >  16000);

    if (m_sideFocus){
        if (up) {
            if (m_sideRow > 0) m_sideRow--;
            // Skip disabled rows (1=Shift, 2=Caps) when in Symbols
            if (m_symbols && (m_sideRow == 1 || m_sideRow == 2)) m_sideRow = 0;
            Sleep(120);
        }
        if (down) {
            if (m_sideRow < 3) m_sideRow++;
            // Skip disabled rows (1=Shift, 2=Caps) when in Symbols
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
                m_sideFocus = true;
                // Entering side column while in Symbols lands on an enabled item
                if (m_symbols && (m_sideRow == 1 || m_sideRow == 2))
                    m_sideRow = 0; // choose "Done"
                Sleep(120);
            }
        }
        if (right) {
            if (m_col < colsNow - 1) { m_col++; Sleep(120); }
        }
    }

    // edges
    bool aTrig     = (a > 30 && m_prevA <= 30);
    bool bTrig     = (b > 30 && m_prevB <= 30);
	bool xTrig     = (x > 30 && m_prevX <= 30);
    bool yTrig     = (y > 30 && m_prevY <= 30);
    bool startTrig = ((btn & XINPUT_GAMEPAD_START) && !(m_prevButtons & XINPUT_GAMEPAD_START));
	bool l3Trig    = ((btn & XINPUT_GAMEPAD_LEFT_THUMB) && !(m_prevButtons & XINPUT_GAMEPAD_LEFT_THUMB)); // L3 toggle symbols
	bool r3Trig    = ((btn & XINPUT_GAMEPAD_RIGHT_THUMB) && !(m_prevButtons & XINPUT_GAMEPAD_RIGHT_THUMB)); // R3 = Symbols


	// --- Y inserts Space ---
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
                // ABC/Symbols toggle
                m_symbols = !m_symbols;
                // If we just turned Symbols on while focused on a disabled row, jump to an enabled one
                if (m_symbols && (m_sideRow == 1 || m_sideRow == 2)) m_sideRow = 3;
                // Re-clamp grid selection columns
                int newCols = VisibleColsForRowHelper(m_symbols, m_row);
                if (m_col >= newCols) m_col = newCols - 1;
                Sleep(140);
            }
        } else {
            if (m_row <= (charRows-1)){ // character rows
                int len = (int)strlen(m_buf);
                const int cap = (int)sizeof(m_buf) - 1;

                if (len < cap && len < kFatxMaxName){
                    char ch = KbCharAt(m_row, m_col);
                    if (ch) {
                        int i;
                        for (i=len; i>=m_cursor; --i) m_buf[i+1] = m_buf[i];
                        m_buf[m_cursor++] = ch;
                        if (m_shiftOnce) m_shiftOnce = false; // one-shot
                    }
                }
            } else { // bottom row: Backspace | Space
                if (m_col == 0){ // Backspace
                    if (m_cursor > 0){
                        int len = (int)strlen(m_buf);
                        int i;
                        for (i=m_cursor-1; i<=len; ++i) m_buf[i] = m_buf[i+1];
                        m_cursor--;
                    }
                } else { // Space
                    int len = (int)strlen(m_buf);
                    const int cap = (int)sizeof(m_buf) - 1;

                    if (len < cap && len < kFatxMaxName){
                        int i;
                        for (i=len; i>=m_cursor; --i) m_buf[i+1] = m_buf[i];
                        m_buf[m_cursor++] = ' ';
                    }
                }
            }
            Sleep(140);
        }
    }

    if (startTrig){
        m_prevA=a; m_prevB=b; m_prevY=y; m_prevX=x; m_prevButtons=btn; m_prevLT=lt; m_prevRT=rt;
        return ACCEPTED;
    }
    if (bTrig){
        m_prevA=a; m_prevB=b; m_prevY=y; m_prevX=x; m_prevButtons=btn; m_prevLT=lt; m_prevRT=rt;
        return CANCELED;
    }

	// --- L3 toggles Caps (alpha only) ---
	if (l3Trig) {
		if (!m_symbols) m_lower = !m_lower;
		Sleep(140);
	}

	if (r3Trig) {
		m_symbols = !m_symbols;

		// If entering Symbols while focused on a disabled side item, bump to enabled
		if (m_sideFocus && m_symbols && (m_sideRow == 1 || m_sideRow == 2))
			m_sideRow = 3;

		// Re-clamp grid selection columns for the current row count
		int newCols = VisibleColsForRowHelper(m_symbols, m_row);
		if (m_col >= newCols) m_col = (newCols > 0) ? (newCols - 1) : 0;

		Sleep(140);
	}

	// X performs Backspace (anywhere)
	if (xTrig) {
		if (m_cursor > 0) {
			int len = (int)strlen(m_buf);
			for (int i = m_cursor - 1; i <= len; ++i)
				m_buf[i] = m_buf[i + 1];
			m_cursor--;
		}
		Sleep(120);
	}

    bool ltTrig = (lt > 30 && m_prevLT <= 30);
    bool rtTrig = (rt > 30 && m_prevRT <= 30);
    if (ltTrig){ if (m_cursor > 0) m_cursor--; }
    if (rtTrig){ int len=(int)strlen(m_buf); if (m_cursor < len) m_cursor++; }

    // clamp caret
    {
        int len=(int)strlen(m_buf);
        if (m_cursor < 0)   m_cursor = 0;
        if (m_cursor > len) m_cursor = len;
    }

    // save prev
    m_prevA = a; m_prevB = b; m_prevY = y; m_prevX = x;
    m_prevButtons = btn; m_prevLT = lt; m_prevRT = rt;

    return NONE;
}
