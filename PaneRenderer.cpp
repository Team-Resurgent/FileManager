#include "PaneRenderer.h"

#include "drawUtils.h"
#include "configuration.h"

static const FLOAT kRightGuardPx   = 2.0f; // keep a sliver on the right
static const FLOAT kMeasureFudgePx = 2.0f; // measurement slack
static const FLOAT kNearFitSlackPx = 1.5f; // near-fit no-scroll slack

static const DWORD kMarqInitPauseMs = MARQUEE_INITIAL_PAUSE;
static const DWORD kMarqEndPauseMs  = MARQUEE_END_PAUSE;
static const DWORD kMarqStepMs      = MARQUEE_STEP;
static const int   kMarqStepChars   = MARQUEE_STEP_CHARS;

void PaneRenderer::PrimeSharedSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st) {
    const char* hdr = (p.mode == 0) ? "Free / Total" : "Size";
    FLOAT maxW = GetAnsiW(font, hdr);
    char buf[128];

    if (p.mode == 0){ // mode == 0
        int limit = ((int)p.items.size() > 32) ? 32 : (int)p.items.size();

        for (int i=0;i<limit;++i){
            const Item& it = p.items[i];
            if (it.isDir && !it.isUpEntry){
                ULONGLONG fb, tb; 
                GetDriveFreeTotal(it.name, fb, tb);
                char f[64], t[64]; 
                FormatSize(fb, f, sizeof(f)); 
                FormatSize(tb, t, sizeof(t));
                _snprintf(buf, sizeof(buf), "%s / %s", f, t); 
                buf[sizeof(buf) - 1] = 0;
                maxW = max(GetAnsiW(font, buf), maxW);
            }
        }
    }
    else { // mode == 1
        int limit = (p.items.size() > 200) ? 200 : p.items.size();

        for (int i=0;i<limit;++i){
            const Item& it = p.items[i];
            if (!it.isDir && !it.isUpEntry){
                FormatSize(it.size, buf, sizeof(buf));
                maxW = max(GetAnsiW(font, buf), maxW);
            }
        }
    }

    maxW += 12.0f; // Why?

    const FLOAT minW = max(90.0f, st.lineH * 3.5f);
    const FLOAT maxWClamp = st.listW * 0.45f;

    maxW = max(minW, maxW);
    maxW = min(maxWClamp, maxW);
    s_sharedSizeColW = max(s_sharedSizeColW, maxW);
}

void PaneRenderer::DrawNameFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT h, FLOAT maxW, DWORD color, const char* name, bool isSelected, int paneIndex, int rowIndex) {
    const char* s = name ? name : ""; // Safety
    const int len = strlen(s);
    const FLOAT fitW_now = floorf(((maxW > 0.0f) ? maxW : 0.0f) + 0.5f);

    FLOAT fullW = GetAnsiW(font, s);

    MarqueeState& M = m_marq[paneIndex];

    if (!isSelected){ // Not selected
        char buf[512];
        EllipsizeAnsiToFit(font, s, fitW_now + kMeasureFudgePx, buf, sizeof(buf));
        DrawAnsiCentered(font, x, y, color, NULL, buf, NULL, h);

        if (M.row == rowIndex) { M.row = -1; M.px = 0.0f; M.fitWLock = 0.0f; M.nextTick = 0; M.resetPause = 0; }
        return;
    }

    // Selected and (near-)fits - draw and clear marquee
    if (fullW <= fitW_now + kMeasureFudgePx + kNearFitSlackPx){
        DrawAnsiCentered(font, x, y, color, NULL, s, NULL, h);

        if (M.row == rowIndex) { M.row = -1; M.px = 0.0f; M.fitWLock = 0.0f; M.nextTick = 0; M.resetPause = 0; }
        return;
    }

    // Selected and clipped - character-step marquee
    const DWORD now = GetTickCount();

    if (M.row != rowIndex){
        M.row       = rowIndex;
        M.px        = 0.0f;                   // start index
        M.fitWLock  = fitW_now;               // lock visible width for stable end calculation
        M.nextTick  = now + kMarqInitPauseMs; // initial pause
        M.resetPause= 0;
    }

    if (M.fitWLock <= 0.0f) M.fitWLock = fitW_now;
    const FLOAT fitW = floorf(M.fitWLock + 0.5f);

    // Earliest start index where the entire tail fits
    size_t hi = len, lo = 0;
    while (lo < hi){
        int mid = (lo + hi) / 2;
        FLOAT tw = GetAnsiW(font, s + mid);
        if (tw <= fitW + kMeasureFudgePx) hi = mid; 
        else lo = mid + 1;
    }
    const int lastStart = min(lo, len);

    // Current start index
    int startIdx = (int)M.px;
    startIdx = max(0, min(startIdx, lastStart));

    // Longest substring from s+startIdx that fits into fitW
    const char* startPtr = s + startIdx;
    const int remaining  = len - startIdx;
    int lo2 = 0, hi2 = remaining;

    while (lo2 < hi2){
        int mid = (lo2 + hi2 + 1) / 2;
        char tmp[512]; 
        _snprintf(tmp, sizeof(tmp), "%.*s", mid, startPtr);
        FLOAT tw = GetAnsiW(font, tmp);
        if (tw <= fitW + kMeasureFudgePx) lo2 = mid; 
        else hi2 = mid - 1;
    }

    char vis[512]; 
    _snprintf(vis, sizeof(vis), "%.*s", lo2, startPtr);
    DrawAnsiCentered(font, x, y, color, NULL, vis, NULL, h);

    // Step/pause/reset
    if (now >= M.nextTick) {
        if (M.resetPause) {
            M.px = 0.0f; // hard reset to first char
            M.resetPause = 0;
            M.nextTick = now + kMarqInitPauseMs; // pause at start again
        } 
        else {
            if (startIdx >= lastStart){
                M.px = (FLOAT)lastStart; // hold
                M.resetPause = now + kMarqEndPauseMs;
                M.nextTick = M.resetPause;
            } 
            else {
                M.px = (FLOAT)(startIdx + kMarqStepChars);
                M.nextTick = now + kMarqStepMs;
            }
        }
    }
}

// LOOPING MARQUEE: header path
void PaneRenderer::DrawHeaderFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT h, FLOAT maxW, DWORD color, const char* text, int paneIndex) {
    const char* s = text ? text : ""; // Safety
    const int len = strlen(s);
    const FLOAT fitW_now = floorf(((maxW > kRightGuardPx) ? (maxW - kRightGuardPx) : 0.0f) + 0.5f);

    // Reset header marquee if text changed
    static char s_prevHdr[2][512] = { {0}, {0} };
    if (_stricmp(s_prevHdr[paneIndex], s) != 0) {
        _snprintf(s_prevHdr[paneIndex], sizeof(s_prevHdr[paneIndex]), "%s", s);
        s_prevHdr[paneIndex][sizeof(s_prevHdr[paneIndex])-1] = 0;
        m_hdrMarq[paneIndex] = MarqueeState();
        m_hdrMarq[paneIndex].nextTick = GetTickCount() + kMarqInitPauseMs;
    }

    // Fits (with slack)? Draw and bail.
    FLOAT tw = GetAnsiW(font, s);
    if (tw <= fitW_now + kMeasureFudgePx + kNearFitSlackPx){
        DrawAnsiCentered(font, x, y, color, &DefaultColors, s, NULL, h);
        m_hdrMarq[paneIndex] = MarqueeState();
        return;
    }

    // Character-step marquee
    const DWORD now = GetTickCount();

    MarqueeState& M = m_hdrMarq[paneIndex];
    if (M.fitWLock <= 0.0f) M.fitWLock = fitW_now;
    const FLOAT fitW = floorf(M.fitWLock + 0.5f);

    // Earliest start where whole tail fits
    int lo = 0, hi = len;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        FLOAT tw = GetAnsiW(font, s + mid);
        if (tw <= fitW + kMeasureFudgePx) hi = mid; 
        else lo = mid + 1;
    }

    const int lastStart = (lo > len) ? len : lo;

    int startIdx = (int)M.px;
    startIdx = max(0, min(startIdx, lastStart));

    // Longest substring from s+startIdx that fits
    const char* startPtr = s + startIdx;
    const int remaining = len - startIdx;
    int lo2 = 0, hi2 = remaining;

    while (lo2 < hi2) {
        int mid = (lo2 + hi2 + 1) / 2;
        char tmp[512]; 
        _snprintf(tmp, sizeof(tmp), "%.*s", mid, startPtr);
        FLOAT tw = GetAnsiW(font, tmp);
        if (tw <= fitW + kMeasureFudgePx) lo2 = mid; 
        else hi2 = mid - 1;
    }

    char vis[512];
    _snprintf(vis, sizeof(vis), "%.*s", lo2, startPtr);
    DrawAnsiCentered(font, x, y, color, &DefaultColors, vis, NULL, h);

    // Step/pause/reset
    if (now >= M.nextTick) {
        if (M.resetPause) {
            M.px = 0.0f;
            M.resetPause = 0;
            M.nextTick = now + kMarqInitPauseMs;
        }
        else {
            if (startIdx >= lastStart) {
                M.px = (FLOAT)lastStart;
                M.resetPause = now + kMarqEndPauseMs;
                M.nextTick = M.resetPause;
            }
            else {
                M.px = (FLOAT)(startIdx + kMarqStepChars);
                M.nextTick = now + kMarqStepMs;
            }
        }
    }
}

void PaneRenderer::DrawPane(CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT baseX, const Pane& p, bool active, const PaneStyle& st, int paneIndex) {

    DrawSolidRect(dev, baseX, st.hdrY, st.hdrH ? st.hdrW : st.listW, st.hdrH, active ? 0xFF3A3A3A : 0x802A2A2A);

    // Header text
    char hdr[600];
    if (p.mode == 0) _snprintf(hdr, sizeof(hdr), "%s", "Detected Drives");
    else _snprintf(hdr, sizeof(hdr), "%s", p.curPath);
    hdr[sizeof(hdr) - 1] = 0;

    // Center vertically
    const FLOAT headerLeft = baseX + 6.0f;
    const FLOAT headerMaxW = st.listW - 12.0f;

    // Draw header (fit or marquee)
    FLOAT tW = GetAnsiW(font, hdr);
    const FLOAT fitHdrW = (headerMaxW > kRightGuardPx) ? (headerMaxW - kRightGuardPx) : 0.0f;
    if (tW <= fitHdrW + kMeasureFudgePx + kNearFitSlackPx) {
        const FLOAT cx = Snap(headerLeft + (fitHdrW - tW) * 0.5f);
        DrawAnsiCentered(font, cx, st.hdrY, 0xFFFFFFFF, &DefaultColors, hdr, NULL, st.hdrH);
        m_hdrMarq[paneIndex] = MarqueeState();
    } 
    else DrawHeaderFittedOrMarquee(font, headerLeft, st.hdrY, st.hdrH, headerMaxW, 0xFFFFFFFF, hdr, paneIndex);

    // size column metrics (shared)
    const FLOAT sizeRight = baseX + st.listW - (st.scrollBarW + st.paddingX);
    const FLOAT sizeColX  = sizeRight - s_sharedSizeColW;

    // column headers
    const FLOAT colHdrY = st.hdrY + st.hdrH + max(6.0f, st.lineH * 0.15f);
    const FLOAT colHdrH = max(22.0f, st.lineH);
    DrawSolidRect(dev, baseX, colHdrY, st.listW, colHdrH, 0x60333333);

    const char* sizeHdr = (p.mode == 0) ? "Free / Total" : "Size";

    DrawAnsiCentered(font, NameColX(baseX, st), colHdrY, HEADER_TEXT_COLOR, &DefaultColors, "Name", NULL, colHdrH);
    DrawAnsiFromRight(font, sizeRight, colHdrY, HEADER_TEXT_COLOR, &DefaultColors, sizeHdr, colHdrH);

    // underline + vertical divider
    DrawSolidRect(dev, baseX, colHdrY + colHdrH, st.listW, 1.0f, 0x80444444);
    DrawSolidRect(dev, sizeColX, colHdrY + 2.0f, 1.0f, colHdrH - 4.0f, 0x40444444);

    // list background + nudge
    const FLOAT listBgTop = colHdrY + colHdrH;
    const FLOAT kFirstRowNudge = max(2.0f, st.lineH * 0.30f); // ~3px at 480p, scales up
    const FLOAT listTop = Snap(listBgTop + kFirstRowNudge); // rows start here
    const FLOAT listH = st.lineH * st.visibleRows;

    // Fill background including the small gap introduced by the nudge
    DrawSolidRect(dev, baseX, listBgTop, st.listW, kFirstRowNudge + listH, 0x30101010);

    // alternating stripes (start at first row, not at underline)
    int end = p.scroll + st.visibleRows;
    if (end > p.items.size()) end = p.items.size();

    int rowIndex = 0;
    for (int i = p.scroll; i < end; ++i, ++rowIndex) {
        D3DCOLOR stripe = (rowIndex & 1) ? 0x201E1E1E : 0x10000000;
        DrawSolidRect(dev, baseX, listTop + rowIndex*st.lineH, st.listW, st.lineH, stripe);
    }

    // selection highlight
    if (!p.items.empty() && p.sel >= p.scroll && p.sel < end) {
        int selRow = p.sel - p.scroll;
        DrawSolidRect(dev, baseX, listTop + selRow*st.lineH, st.listW, st.lineH, active ? ACTIVE_HIGHLIGHTED_ROW_COLOR : HIGHLIGHTED_ROW_COLOR);
    }

    // rows
    FLOAT y = listTop;
    for (int i = p.scroll, r = 0; i < end; ++i, ++r) {
        const Item& it = p.items[i];
        DWORD nameCol = (i == p.sel) ? HIGHLIGHTED_TEXT_COLOR : 0xFFE0E0E0;
        DWORD sizeCol = (i == p.sel) ? HIGHLIGHTED_TEXT_COLOR : 0xFFB0B0B0;

        // icon gutter
        const FLOAT gutterX = baseX + 4.0f;
        const FLOAT gutterW = st.gutterW;
        const FLOAT gutterH = st.lineH;

        const char glyph[2] = { it.icon, 0 };
        DrawAnsiCentered(font, gutterX, y, MARKED_ITEM_COLOR, (it.marked ? NULL : &DefaultColors), glyph, gutterW, gutterH);

        // filename area (compute available width once)
        char nameBuf[300];
        _snprintf(nameBuf, sizeof(nameBuf), "%s", it.name);
        nameBuf[sizeof(nameBuf) - 1] = 0;
        const FLOAT nameXRaw = NameColX(baseX, st);
        const FLOAT rightPad = st.paddingX + st.scrollBarW;
        const FLOAT kNameSafePad = 2.0f;

        const FLOAT nameRightEdge = Snap((baseX + st.listW) - rightPad - s_sharedSizeColW - kNameSafePad - kRightGuardPx);
        const FLOAT nameLeftEdge  = Snap(nameXRaw);
        FLOAT nameMaxW = nameRightEdge - nameLeftEdge;
        nameMaxW = max(nameMaxW, 0.0f);

        const bool isSel = active && (i == p.sel);
        DrawNameFittedOrMarquee(font, nameLeftEdge, y, gutterH, nameMaxW, nameCol, nameBuf, isSel, paneIndex, i - p.scroll);

        // size column
        char sz[96] = "";
        if (p.mode == 0 && it.isDir && !it.isUpEntry) {
            ULONGLONG fb=0, tb=0;
            GetDriveFreeTotal(it.name, fb, tb);
            char f[32], t[32];
            FormatSize(fb, f, sizeof(f));
            FormatSize(tb, t, sizeof(t));
            _snprintf(sz, sizeof(sz), "%s / %s", f, t);
        } 
        else if (!it.isDir && !it.isUpEntry) FormatSize(it.size, sz, sizeof(sz));

        DrawAnsiFromRight(font, sizeRight, y, sizeCol, &DefaultColors, sz, gutterH);
        y += st.lineH;
    }

    // ----- scrollbar -----
    int total = p.items.size();
    if (total > st.visibleRows) {
        const FLOAT trackX = baseX + st.listW - st.scrollBarW;
        const FLOAT trackY = listTop; // align with first row after nudge
        const FLOAT trackH = st.visibleRows * st.lineH;

        DrawSolidRect(dev, trackX, trackY, st.scrollBarW, trackH, 0x40282828);

        FLOAT thumbH = (FLOAT)st.visibleRows/(FLOAT)total * trackH;
        thumbH = max(thumbH, 10.0f);

        FLOAT maxScroll = (FLOAT)(total - st.visibleRows);
        FLOAT t = (maxScroll > 0.0f) ? ((FLOAT)p.scroll / maxScroll) : 0.0f;
        FLOAT thumbY = trackY + t * (trackH - thumbH);
        DrawSolidRect(dev, trackX, thumbY, st.scrollBarW, thumbH, 0x80C0C0C0);
    }
}

