#include "PaneRenderer.h"
#include "GfxPrims.h"
#include <wchar.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/*
============================================================================
 PaneRenderer (implementation)
  - Draws header/title, column headers, rows, selection highlight, scrollbar
  - Keeps Size column aligned across panes via per-frame shared width
  - Looping, character-step marquee with start/end pauses for long text
  - Substring-fitting instead of scissor; avoids per-pixel motion -> sharp text
  - Zero heap allocations in hot paths (ANSI -> WCHAR on stack; cached measures)
  - Tuning knobs:
      * kMarqInitPauseMs / kMarqEndPauseMs
      * kMarqStepMs / kMarqStepChars
      * kNearFitSlackPx / kRightGuardPx / kMeasureFudgePx
  - Depends on: XBFont (text), GfxPrims (rects), PaneModel (data)
============================================================================
*/


// --- local constants --------------------------------------------------------
static inline FLOAT Snap(FLOAT v){ return (FLOAT)((int)(v + 0.5f)); }

static const FLOAT kRightGuardPx   = 2.0f;  // keep a sliver on the right
static const FLOAT kMeasureFudgePx = 2.0f;  // measurement slack
static const FLOAT kNearFitSlackPx = 1.5f;  // near-fit no-scroll slack

// --- marquee speed tuning (easy to tweak in one place) ----------------------
static const DWORD kMarqInitPauseMs = 900;   // pause before first move
static const DWORD kMarqEndPauseMs  = 1200;  // pause when tail fully visible
static const DWORD kMarqStepMs      = 150;   // time between steps (bigger = slower)
static const int   kMarqStepChars   = 1;     // how many characters to advance per step

// --- shared size-column width backing --------------------------------------
FLOAT PaneRenderer::s_sharedSizeColW = 0.0f;
void  PaneRenderer::BeginFrameSharedCols(){ s_sharedSizeColW = 0.0f; }
void  PaneRenderer::UpdateSharedSizeColW(FLOAT w){ if (w > s_sharedSizeColW) s_sharedSizeColW = w; }
FLOAT PaneRenderer::GetSharedSizeColW(){ return s_sharedSizeColW; }

// --- ANSI/WIDE helpers ------------------------------------------------------
static void MbToW(const char* s, WCHAR* w, int capW){
    MultiByteToWideChar(CP_ACP,0,s?s:"",-1,w,capW);
}
static void MeasureAnsiWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH){
    WCHAR wbuf[512]; MbToW(s, wbuf, 512); font.GetTextExtent(wbuf, &outW, &outH);
}
void PaneRenderer::DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
    WCHAR wbuf[512]; MbToW(text?text:"", wbuf, 512); font.DrawText(x,y,color,wbuf,0,0.0f);
}
void PaneRenderer::DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(dev, x, y, w, h, c);
}
void PaneRenderer::MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH){
    WCHAR wbuf[256]; MbToW(s ? s : "", wbuf, 256); font.GetTextExtent(wbuf, &outW, &outH);
}
FLOAT PaneRenderer::MeasureTextW(CXBFont& font, const char* s){
    FLOAT w=0.0f,h=0.0f; MeasureTextWH(font,s,w,h); return w;
}
void PaneRenderer::DrawRightAligned(CXBFont& font, const char* s, FLOAT rightX, FLOAT y, DWORD color){
    WCHAR wbuf[512]; MbToW(s?s:"", wbuf, 512); FLOAT tw=0,th=0; font.GetTextExtent(wbuf,&tw,&th);
    font.DrawText(Snap(rightX - tw), y, color, wbuf, 0, 0.0f);
}

// Center a one-line ANSI string vertically in a band (slight downward bias)
static FLOAT CenterYForText(CXBFont& font, const char* s, FLOAT bandY, FLOAT bandH, FLOAT biasPx = 1.0f)
{
    WCHAR wbuf[512]; MbToW(s ? s : "", wbuf, 512);
    FLOAT tw=0, th=0; font.GetTextExtent(wbuf, &tw, &th);
    return Snap(bandY + (bandH - th) * 0.5f + biasPx);
}

// Right ellipsize (non-selected rows)
void PaneRenderer::RightEllipsizeToFit(CXBFont& font, const char* src, FLOAT maxW,
                                       char* out, size_t cap)
{
    if (!src){ out[0]=0; return; }
    const FLOAT fitW = (maxW > kRightGuardPx) ? (maxW - kRightGuardPx) : 0.0f;

    WCHAR wbuf[512]; MbToW(src, wbuf, 512);
    FLOAT tw=0,th=0; font.GetTextExtent(wbuf,&tw,&th);
    if (tw <= fitW + kMeasureFudgePx){ _snprintf(out,(int)cap,"%s",src); out[cap-1]=0; return; }

    size_t n = strlen(src), lo=0, hi=n; char buf[512];
    while (lo < hi){
        size_t mid=(lo+hi)/2;
        _snprintf(buf,sizeof(buf),"%.*s...",(int)mid,src);
        MbToW(buf,wbuf,512); font.GetTextExtent(wbuf,&tw,&th);
        if (tw <= fitW + kMeasureFudgePx) lo = mid+1; else hi = mid;
    }
    size_t take = (lo>0)?(lo-1):0;
    _snprintf(out,(int)cap,"%.*s...",(int)take,src);
    out[cap-1]=0;
}

// Compute size column width from data (clamped to sane bounds)
FLOAT PaneRenderer::ComputeSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st){
    const char* hdr = (p.mode == 0) ? "Free / Total" : "Size";
    FLOAT maxW = MeasureTextW(font, hdr);
    char buf[128];

    if (p.mode == 0){
        int limit = (int)p.items.size(); if (limit > 32) limit = 32;
        for (int i=0;i<limit;++i){
            const Item& it = p.items[i];
            if (it.isDir && !it.isUpEntry){
                ULONGLONG fb=0,tb=0; GetDriveFreeTotal(it.name, fb, tb);
                char f[64], t[64]; FormatSize(fb,f,sizeof(f)); FormatSize(tb,t,sizeof(t));
                _snprintf(buf,sizeof(buf),"%s / %s",f,t); buf[sizeof(buf)-1]=0;
                FLOAT w = MeasureTextW(font, buf); if (w > maxW) maxW = w;
            }
        }
    } else {
        int limit = (int)p.items.size(); if (limit > 200) limit = 200;
        for (int i=0;i<limit;++i){
            const Item& it = p.items[i];
            if (!it.isDir && !it.isUpEntry){
                FormatSize(it.size, buf, sizeof(buf));
                FLOAT w = MeasureTextW(font, buf); if (w > maxW) maxW = w;
            }
        }
    }

    maxW += 12.0f;
    const FLOAT minW = PaneRenderer::MaxF(90.0f, st.lineH * 3.5f);
    const FLOAT maxWClamp = st.listW * 0.45f;
    if (maxW < minW) maxW = minW;
    if (maxW > maxWClamp) maxW = maxWClamp;
    return maxW;
}

void PaneRenderer::PrimeSharedSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st){
    const FLOAT localW = ComputeSizeColW(font, p, st);
    UpdateSharedSizeColW(localW);
}

// --- LOOPING MARQUEE: filenames (selected row) ------------------------------
void PaneRenderer::DrawNameFittedOrMarquee(
    CXBFont& font, FLOAT x, FLOAT y, FLOAT maxW,
    DWORD color, const char* name,
    bool isSelected, int paneIndex, int rowIndex)
{
    const char* s = name ? name : "";
    const int   len = (int)strlen(s);
    const FLOAT fitW_now = floorf(((maxW > 0.0f) ? maxW : 0.0f) + 0.5f);

    // Measure once
    WCHAR wfull[512]; MbToW(s, wfull, 512);
    FLOAT fullW=0, fullH=0; font.GetTextExtent(wfull, &fullW, &fullH);

    MarqueeState& M = m_marq[paneIndex];

    // Non-selected: draw/ellipsize; clear any marquee state for this row
    if (!isSelected){
        if (fullW <= fitW_now + kMeasureFudgePx){
            font.DrawText(Snap(x), Snap(y), color, wfull, 0, 0.0f);
        } else {
            char tmp[512];
            RightEllipsizeToFit(font, s, fitW_now + kRightGuardPx, tmp, sizeof(tmp));
            WCHAR wtmp[512]; MbToW(tmp, wtmp, 512);
            font.DrawText(Snap(x), Snap(y), color, wtmp, 0, 0.0f);
        }
        if (M.row == rowIndex){ M.row = -1; M.px = 0.0f; M.fitWLock = 0.0f; M.nextTick = 0; M.resetPause = 0; }
        return;
    }

    // Selected and (near-)fits: draw and clear marquee
    if (fullW <= fitW_now + kMeasureFudgePx + kNearFitSlackPx){
        font.DrawText(Snap(x), Snap(y), color, wfull, 0, 0.0f);
        if (M.row == rowIndex){ M.row = -1; M.px = 0.0f; M.fitWLock = 0.0f; M.nextTick = 0; M.resetPause = 0; }
        return;
    }

    // Selected + clipped: character-step marquee
    const DWORD now = GetTickCount();
    const DWORD kInitPauseMs = kMarqInitPauseMs;
    const DWORD kStepMs      = kMarqStepMs;
    const DWORD kEndPauseMs  = kMarqEndPauseMs;

    if (M.row != rowIndex){
        M.row       = rowIndex;
        M.px        = 0.0f;               // start index (we store as float)
        M.fitWLock  = fitW_now;           // lock visible width for stable end calc
        M.nextTick  = now + kInitPauseMs; // initial pause
        M.resetPause= 0;
    }
    if (M.fitWLock <= 0.0f) M.fitWLock = fitW_now;
    const FLOAT fitW = floorf(M.fitWLock + 0.5f);

    // Earliest start index where the entire tail fits
    int lo = 0, hi = len;
    while (lo < hi){
        int mid = (lo + hi) / 2;
        WCHAR wtmp[512]; MbToW(s + mid, wtmp, 512);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kMeasureFudgePx) hi = mid; else lo = mid + 1;
    }
    const int lastStart = (lo > len) ? len : lo;

    // Current start index
    int startIdx = (int)M.px;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > lastStart) startIdx = lastStart;

    // Longest substring from s+startIdx that fits into fitW
    const char* startPtr = s + startIdx;
    const int remaining  = len - startIdx;
    int lo2 = 0, hi2 = remaining;
    while (lo2 < hi2){
        int mid = (lo2 + hi2 + 1) / 2;
        char tmp[512]; _snprintf(tmp, sizeof(tmp), "%.*s", mid, startPtr);
        WCHAR wtmp[512]; MbToW(tmp, wtmp, 512);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kMeasureFudgePx) lo2 = mid; else hi2 = mid - 1;
    }
    char vis[512]; _snprintf(vis, sizeof(vis), "%.*s", lo2, startPtr);
    WCHAR wvis[512]; MbToW(vis, wvis, 512);

    font.DrawText(Snap(x), Snap(y), color, wvis, 0, 0.0f);

    // Step/pause/reset
    if (now >= M.nextTick){
        if (M.resetPause){
            M.px        = 0.0f;                 // hard reset to first char
            M.resetPause= 0;
            M.nextTick  = now + kInitPauseMs;   // pause at start again
        } else {
            if (startIdx >= lastStart){
                M.px        = (FLOAT)lastStart; // hold
                M.resetPause= now + kEndPauseMs;
                M.nextTick  = M.resetPause;
            } else {
                M.px       = (FLOAT)(startIdx + kMarqStepChars);
                M.nextTick = now + kStepMs;
            }
        }
    }
}

// --- LOOPING MARQUEE: header path ------------------------------------------
void PaneRenderer::DrawHeaderFittedOrMarquee(
    CXBFont& font, FLOAT x, FLOAT y, FLOAT maxW,
    DWORD color, const char* text, int paneIndex)
{
    const char* s = text ? text : "";
    const int   len = (int)strlen(s);
    const FLOAT fitW_now = floorf(((maxW > kRightGuardPx) ? (maxW - kRightGuardPx) : 0.0f) + 0.5f);

    // Reset header marquee if text changed
    static char s_prevHdr[2][512] = { {0}, {0} };
    if (_stricmp(s_prevHdr[paneIndex], s) != 0){
        _snprintf(s_prevHdr[paneIndex], sizeof(s_prevHdr[paneIndex]), "%s", s);
        s_prevHdr[paneIndex][sizeof(s_prevHdr[paneIndex])-1] = 0;
        m_hdrMarq[paneIndex] = MarqueeState();
        m_hdrMarq[paneIndex].nextTick = GetTickCount() + kMarqInitPauseMs;
    }

    // Fits (with slack)? Draw and bail.
    WCHAR wfull[512]; MbToW(s, wfull, 512);
    FLOAT fullW=0, fullH=0; font.GetTextExtent(wfull, &fullW, &fullH);
    if (fullW <= fitW_now + kMeasureFudgePx + kNearFitSlackPx){
        font.DrawText(Snap(x), Snap(y), color, wfull, 0, 0.0f);
        m_hdrMarq[paneIndex] = MarqueeState();
        return;
    }

    // Character-step marquee
    const DWORD now = GetTickCount();
    const DWORD kInitPauseMs = kMarqInitPauseMs;
    const DWORD kStepMs      = kMarqStepMs;
    const DWORD kEndPauseMs  = kMarqEndPauseMs;

    MarqueeState& M = m_hdrMarq[paneIndex];
    if (M.fitWLock <= 0.0f) M.fitWLock = fitW_now;
    const FLOAT fitW = floorf(M.fitWLock + 0.5f);

    // Earliest start where whole tail fits
    int lo = 0, hi = len;
    while (lo < hi){
        int mid = (lo + hi) / 2;
        WCHAR wtmp[512]; MbToW(s + mid, wtmp, 512);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kMeasureFudgePx) hi = mid; else lo = mid + 1;
    }
    const int lastStart = (lo > len) ? len : lo;

    int startIdx = (int)M.px;
    if (startIdx < 0) startIdx = 0;
    if (startIdx > lastStart) startIdx = lastStart;

    // Longest substring from s+startIdx that fits
    const char* startPtr = s + startIdx;
    const int remaining  = len - startIdx;
    int lo2 = 0, hi2 = remaining;
    while (lo2 < hi2){
        int mid = (lo2 + hi2 + 1) / 2;
        char tmp[512]; _snprintf(tmp, sizeof(tmp), "%.*s", mid, startPtr);
        WCHAR wtmp[512]; MbToW(tmp, wtmp, 512);
        FLOAT tw=0, th=0; font.GetTextExtent(wtmp, &tw, &th);
        if (tw <= fitW + kMeasureFudgePx) lo2 = mid; else hi2 = mid - 1;
    }
    char vis[512]; _snprintf(vis, sizeof(vis), "%.*s", lo2, startPtr);
    WCHAR wvis[512]; MbToW(vis, wvis, 512);

    font.DrawText(Snap(x), Snap(y), color, wvis, 0, 0.0f);

    // Step/pause/reset
    if (now >= M.nextTick){
        if (M.resetPause){
            M.px        = 0.0f;
            M.resetPause= 0;
            M.nextTick  = now + kInitPauseMs;
        } else {
            if (startIdx >= lastStart){
                M.px        = (FLOAT)lastStart;
                M.resetPause= now + kEndPauseMs;
                M.nextTick  = M.resetPause;
            } else {
                M.px       = (FLOAT)(startIdx + kMarqStepChars);
                M.nextTick = now + kStepMs;
            }
        }
    }
}

/*
===============================================================================
 DrawPane
===============================================================================
*/
void PaneRenderer::DrawPane(
    CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT baseX,
    const Pane& p, bool active, const PaneStyle& st, int paneIndex)
{
    // ----- header band -----
    DrawRect(dev, baseX, st.hdrY, st.hdrH ? st.hdrW : st.listW, st.hdrH, active ? 0xFF3A3A3A : 0x802A2A2A);

    // Header text
    char hdr[600];
    if (p.mode == 0) _snprintf(hdr, sizeof(hdr), "%s", "Detected Drives");
    else             _snprintf(hdr, sizeof(hdr), "%s",  p.curPath);
    hdr[sizeof(hdr)-1] = 0;

    // Center vertically
    const FLOAT titleY     = CenterYForText(font, hdr, st.hdrY, st.hdrH, 1.0f);
    const FLOAT headerLeft = baseX + 6.0f;
    const FLOAT headerMaxW = st.listW - 12.0f;

    // Draw header (fit or marquee)
    FLOAT tW=0, tH=0; MeasureAnsiWH(font, hdr, tW, tH);
    const FLOAT fitHdrW = (headerMaxW > kRightGuardPx) ? (headerMaxW - kRightGuardPx) : 0.0f;
    if (tW <= fitHdrW + kMeasureFudgePx + kNearFitSlackPx) {
        const FLOAT cx = Snap(headerLeft + (fitHdrW - tW) * 0.5f);
        DrawAnsi(font, cx, titleY, 0xFFFFFFFF, hdr);
        m_hdrMarq[paneIndex] = MarqueeState();
    } else {
        DrawHeaderFittedOrMarquee(font, headerLeft, titleY, headerMaxW, 0xFFFFFFFF, hdr, paneIndex);
    }

    // ----- size column metrics (shared) -----
    const FLOAT sizeColW  = GetSharedSizeColW();
    const FLOAT sizeRight = baseX + st.listW - (st.scrollBarW + st.paddingX);
    const FLOAT sizeColX  = sizeRight - sizeColW;

    // ----- column headers -----
    const FLOAT colHdrY = st.hdrY + st.hdrH + PaneRenderer::MaxF(6.0f, st.lineH * 0.15f);
    const FLOAT colHdrH = PaneRenderer::MaxF(22.0f, st.lineH);
    DrawRect(dev, baseX, colHdrY, st.listW, colHdrH, 0x60333333);

    FLOAT nameW, nameH; MeasureAnsiWH(font, "Name", nameW, nameH);
    const char* sizeHdr = (p.mode == 0) ? "Free / Total" : "Size";
    FLOAT sizeW, sizeH; MeasureAnsiWH(font, sizeHdr, sizeW, sizeH);

    const FLOAT nameY = colHdrY + (colHdrH - nameH) * 0.5f;
    const FLOAT sizeY = colHdrY + (colHdrH - sizeH) * 0.5f;

    DrawAnsi(font, NameColX(baseX, st), nameY, 0xFFDDDDDD, "Name");
    DrawRightAligned(font, sizeHdr, sizeRight, sizeY, 0xFFDDDDDD);

    // underline + vertical divider
    DrawRect(dev, baseX,    colHdrY + colHdrH,  st.listW, 1.0f, 0x80444444);
    DrawRect(dev, sizeColX, colHdrY + 2.0f,     1.0f,     colHdrH - 4.0f, 0x40444444);

    // ----- list background + nudge (push first row down slightly) ------------
    const FLOAT listBgTop = colHdrY + colHdrH;
    const FLOAT kFirstRowNudge = PaneRenderer::MaxF(2.0f, st.lineH * 0.30f); // ~3px at 480p, scales up
    const FLOAT listTop   = Snap(listBgTop + kFirstRowNudge);                 // rows start here
    const FLOAT listH     = st.lineH * st.visibleRows;

    // Fill background including the small gap introduced by the nudge
    DrawRect(dev, baseX, listBgTop, st.listW, kFirstRowNudge + listH, 0x30101010);

    // alternating stripes (start at first row, not at underline)
    int end = p.scroll + st.visibleRows; if (end > (int)p.items.size()) end = (int)p.items.size();
    int rowIndex = 0;
    for (int i = p.scroll; i < end; ++i, ++rowIndex) {
        D3DCOLOR stripe = (rowIndex & 1) ? 0x201E1E1E : 0x10000000;
        DrawRect(dev, baseX, listTop + rowIndex*st.lineH, st.listW, st.lineH, stripe);
    }

    // selection highlight
    if (!p.items.empty() && p.sel >= p.scroll && p.sel < end) {
        int selRow = p.sel - p.scroll;
        DrawRect(dev, baseX, listTop + selRow*st.lineH, st.listW, st.lineH, active?0x60FFFF00:0x30FFFF00);
    }

    // ----- rows -----
    FLOAT y = listTop;
    for (int i = p.scroll, r = 0; i < end; ++i, ++r) {
        const Item& it = p.items[i];
        DWORD nameCol = (i==p.sel)?0xFFFFFF00:0xFFE0E0E0;
        DWORD sizeCol = (i==p.sel)?0xFFFFFF00:0xFFB0B0B0;
        D3DCOLOR ico = it.isUpEntry ? 0xFFAAAAAA
                            : (it.marked ? 0xFFFF4040
                                         : (it.isDir ? 0xFF5EA4FF
                                                     : 0xFF89D07E));

        // icon gutter
        const FLOAT gutterX = baseX + 2.0f;
        const FLOAT gutterW = st.gutterW - 4.0f;
        const FLOAT gutterH = st.lineH - 6.0f;
        DrawRect(dev, gutterX, y + (st.lineH - gutterH) * 0.5f, gutterW, gutterH, ico);
        const char* glyph = it.isUpEntry ? ".." : (it.isDir?"+":"-");
        DrawAnsi(font, gutterX + 2.0f, y + 2.0f, 0xFFFFFFFF, glyph);

        // filename area (compute available width once)
        char nameBuf[300]; _snprintf(nameBuf, sizeof(nameBuf), "%s", it.name); nameBuf[sizeof(nameBuf)-1] = 0;
        const FLOAT nameXRaw     = NameColX(baseX, st);
        const FLOAT rightPad     = st.paddingX + st.scrollBarW;
        const FLOAT kNameSafePad = 2.0f;

        const FLOAT nameRightEdge = Snap((baseX + st.listW) - rightPad - GetSharedSizeColW() - kNameSafePad - kRightGuardPx);
        const FLOAT nameLeftEdge  = Snap(nameXRaw);
        FLOAT nameMaxW = nameRightEdge - nameLeftEdge;
        if (nameMaxW < 0.0f) nameMaxW = 0.0f;

        const bool isSel = active && (i == p.sel);
        DrawNameFittedOrMarquee(font, nameLeftEdge, y, nameMaxW,
                                nameCol, nameBuf, isSel, paneIndex, i - p.scroll);

        // size column
        char sz[96] = "";
        if (p.mode == 0 && it.isDir && !it.isUpEntry) {
            ULONGLONG fb=0, tb=0; GetDriveFreeTotal(it.name, fb, tb);
            char f[32], t[32]; FormatSize(fb, f, sizeof(f)); FormatSize(tb, t, sizeof(t));
            _snprintf(sz, sizeof(sz), "%s / %s", f, t);
        } else if (!it.isDir && !it.isUpEntry) {
            FormatSize(it.size, sz, sizeof(sz));
        }
        DrawRightAligned(font, sz, sizeRight, y, sizeCol);

        y += st.lineH;
    }

    // ----- scrollbar -----
    if ((int)p.items.size() > st.visibleRows) {
        const FLOAT trackX = baseX + st.listW - st.scrollBarW;
        const FLOAT trackY = listTop;                 // align with first row after nudge
        const FLOAT trackH = st.visibleRows * st.lineH;

        DrawRect(dev, trackX, trackY, st.scrollBarW, trackH, 0x40282828);

        int total = (int)p.items.size();
        FLOAT thumbH = (FLOAT)st.visibleRows/(FLOAT)total * trackH; if (thumbH < 10.0f) thumbH = 10.0f;
        FLOAT maxScroll = (FLOAT)(total - st.visibleRows);
        FLOAT t = (maxScroll > 0.0f) ? ((FLOAT)p.scroll / maxScroll) : 0.0f;
        FLOAT thumbY = trackY + t * (trackH - thumbH);
        DrawRect(dev, trackX, thumbY, st.scrollBarW, thumbH, 0x80C0C0C0);
    }
}

