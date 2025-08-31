#include "PaneRenderer.h"
#include "GfxPrims.h"
#include <wchar.h>
#include <stdio.h>  // _snprintf

// Measure ANSI text (CP_ACP) with CXBFont (wide only)
static void MeasureAnsiWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH)
{
    WCHAR wbuf[512];
    MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 512);
    font.GetTextExtent(wbuf, &outW, &outH);
}


void PaneRenderer::DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text){
    WCHAR wbuf[512];
    MultiByteToWideChar(CP_ACP,0,text,-1,wbuf,512);
    font.DrawText(x,y,color,wbuf,0,0.0f);
}
void PaneRenderer::DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c){
    DrawSolidRect(dev, x, y, w, h, c);
}
void PaneRenderer::MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH){
    WCHAR wbuf[256];
    MultiByteToWideChar(CP_ACP, 0, s, -1, wbuf, 256);
    font.GetTextExtent(wbuf, &outW, &outH);
}
FLOAT PaneRenderer::MeasureTextW(CXBFont& font, const char* s){
    FLOAT w=0.0f, h=0.0f; MeasureTextWH(font, s, w, h); return w;
}
void PaneRenderer::DrawRightAligned(CXBFont& font, const char* s, FLOAT rightX, FLOAT y, DWORD color){
    DrawAnsi(font, rightX - MeasureTextW(font, s), y, color, s);
}

FLOAT PaneRenderer::ComputeSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st){
    FLOAT maxW = MeasureTextW(font, "Size");
    int limit = (int)p.items.size(); if(limit>200) limit=200;
    char buf[64];
    for(int i=0;i<limit;++i){
        const Item& it = p.items[i];
        if(!it.isDir && !it.isUpEntry){
            FormatSize(it.size, buf, sizeof(buf));
            FLOAT w = MeasureTextW(font, buf); if(w>maxW) maxW=w;
        }
    }
    maxW += 16.0f;
    const FLOAT minW = MaxF(90.0f, st.lineH * 4.0f);
    const FLOAT maxWClamp = st.listW * 0.40f;
    if(maxW < minW) maxW = minW;
    if(maxW > maxWClamp) maxW = maxWClamp;
    return maxW;
}


static void MbToW(const char* s, WCHAR* w, int capW){
    MultiByteToWideChar(CP_ACP,0,s?s:"",-1,w,capW);
}

void PaneRenderer::RightEllipsizeToFit(CXBFont& font, const char* src, FLOAT maxW,
                                       char* out, size_t cap)
{
    if (!src){ out[0]=0; return; }
    WCHAR wbuf[512]; MbToW(src, wbuf, 512);
    FLOAT tw=0,th=0; font.GetTextExtent(wbuf,&tw,&th);
    if (tw <= maxW){ _snprintf(out,(int)cap,"%s",src); out[cap-1]=0; return; }

    size_t n = strlen(src), lo=0, hi=n;
    char buf[512];
    while (lo < hi){
        size_t mid=(lo+hi)/2;
        _snprintf(buf,sizeof(buf),"%.*s...",(int)mid,src);
        MbToW(buf,wbuf,512); font.GetTextExtent(wbuf,&tw,&th);
        if (tw <= maxW) lo = mid+1; else hi = mid;
    }
    size_t take = (lo>0)?(lo-1):0;
    _snprintf(out,(int)cap,"%.*s...",(int)take,src);
    out[cap-1]=0;
}

const char* PaneRenderer::SkipToPixelOffset(CXBFont& font, const char* s, FLOAT px, FLOAT& skippedW)
{
    skippedW = 0.0f; if (!s || px <= 0) return s?s:"";
    const char* p = s;
    WCHAR wtmp[256]; FLOAT tw=0,th=0;
    while (*p){
        int len = (int)(p - s + 1); if (len > 255) len = 255;
        char tmp[256]; _snprintf(tmp,sizeof(tmp),"%.*s",len,s);
        MbToW(tmp,wtmp,256); font.GetTextExtent(wtmp,&tw,&th);
        if (tw >= px) break;
        ++p;
    }
    skippedW = tw; return p;
}

void PaneRenderer::DrawNameFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT maxW,
                                           DWORD color, const char* name,
                                           bool isSelected, int paneIndex, int rowIndex)
{
    WCHAR wfull[512]; MbToW(name, wfull, 512);
    FLOAT fullW=0, fullH=0; font.GetTextExtent(wfull,&fullW,&fullH);

    if (!isSelected || fullW <= maxW){
        char tmp[512];
        RightEllipsizeToFit(font, name, maxW, tmp, sizeof(tmp));
        WCHAR wtmp[512]; MbToW(tmp,wtmp,512);
        font.DrawText(x,y,color,wtmp,0,0.0f);
        if (m_marq[paneIndex].row == rowIndex){ m_marq[paneIndex].row=-1; m_marq[paneIndex].px=0.0f; }
        return;
    }

    // marquee
    const DWORD now = GetTickCount();
    const DWORD kInitPauseMs = 500;
    const DWORD kStepMs      = 30;
    const FLOAT kStepPx      = 2.0f;
    const DWORD kEndPauseMs  = 700;

    MarqueeState& M = m_marq[paneIndex];
    if (M.row != rowIndex){
        M.row = rowIndex; M.px = 0.0f; M.nextTick = now + kInitPauseMs; M.resetPause = 0;
    }
    if (now >= M.nextTick){
        if (M.resetPause){
            M.px = 0.0f; M.resetPause = 0; M.nextTick = now + kInitPauseMs;
        }else{
            M.px += kStepPx;
            FLOAT maxScroll = fullW - maxW;
            if (M.px >= maxScroll){ M.px = maxScroll; M.resetPause = now + kEndPauseMs; M.nextTick = M.resetPause; }
            else                   { M.nextTick = now + kStepMs; }
        }
    }

    FLOAT skipped=0.0f;
    const char* start = SkipToPixelOffset(font, name, M.px, skipped);

    char vis[512]; vis[0]=0; int n=0;
    WCHAR wtmp[512]; FLOAT tw=0,th=0;
    const char* p = start;
    while (*p && n < (int)sizeof(vis)-2){
        vis[n++]=*p; vis[n]=0;
        MbToW(vis,wtmp,512); font.GetTextExtent(wtmp,&tw,&th);
        if (tw > maxW){ vis[--n]=0; break; }
        ++p;
    }
    MbToW(vis,wtmp,512); font.DrawText(x,y,color,wtmp,0,0.0f);
}

void PaneRenderer::DrawPane(
    CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT baseX,
    const Pane& p, bool active, const PaneStyle& st, int paneIndex)
{
    // ----- header (title band with vertical centering) -----
    const FLOAT hx        = baseX - 15.0f;   // same offset as FileBrowserApp::HdrX
    const FLOAT headerGap = 8.0f;            // space under the header band
    DrawRect(dev, hx, st.hdrY, st.hdrW, st.hdrH, active ? 0xFF3A3A3A : 0x802A2A2A);

    char hdr[600];
    if (p.mode == 0) _snprintf(hdr, sizeof(hdr), "%s", "Detected Drives");
    else             _snprintf(hdr, sizeof(hdr), "%s",  p.curPath);
    hdr[sizeof(hdr)-1] = 0;

    FLOAT tW, tH; MeasureAnsiWH(font, hdr, tW, tH);
    const FLOAT titleY = st.hdrY + (st.hdrH - tH) * 0.5f;
    DrawAnsi(font, hx + 5.0f, titleY, 0xFFFFFFFF, hdr);

    // compute Size column width/X
    const FLOAT sizeColW  = ComputeSizeColW(font, p, st);
    const FLOAT sizeColX  = baseX + st.listW - (st.scrollBarW + st.paddingX + sizeColW);
    const FLOAT sizeRight = sizeColX + sizeColW;

    // ----- column header band -----
    const FLOAT colHdrY = st.hdrY + st.hdrH + headerGap;
    const FLOAT colHdrH = 24.0f;
    DrawRect(dev, baseX-10.0f, colHdrY, st.listW+20.0f, colHdrH, 0x60333333);

	FLOAT nameW, nameH; MeasureAnsiWH(font, "Name", nameW, nameH);
	FLOAT sizeW, sizeH; MeasureAnsiWH(font, "Size", sizeW, sizeH);
    const FLOAT nameY = colHdrY + (colHdrH - nameH) * 0.5f;
    const FLOAT sizeY = colHdrY + (colHdrH - sizeH) * 0.5f;

    DrawAnsi(font, NameColX(baseX, st), nameY, 0xFFDDDDDD, "Name");
    DrawRightAligned(font, "Size", sizeRight, sizeY, 0xFFDDDDDD);

    // underline + vertical separator
    DrawRect(dev, baseX-10.0f, colHdrY + colHdrH, st.listW+20.0f, 1.0f, 0x80444444);
    DrawRect(dev, sizeColX - 8.0f, colHdrY + 1.0f, 1.0f, colHdrH - 2.0f, 0x40444444);

    // list bg
    DrawRect(dev, baseX-10.0f, st.listY-6.0f, st.listW+20.0f, st.lineH*st.visibleRows+12.0f, 0x30101010);

    // stripes + selection
    int end = p.scroll + st.visibleRows; if (end > (int)p.items.size()) end = (int)p.items.size();
    int rowIndex = 0;
    for (int i = p.scroll; i < end; ++i, ++rowIndex) {
        D3DCOLOR stripe = (rowIndex & 1) ? 0x201E1E1E : 0x10000000;
        DrawRect(dev, baseX, st.listY + rowIndex*st.lineH - 2.0f, st.listW-8.0f, st.lineH, stripe);
    }
    if (!p.items.empty() && p.sel >= p.scroll && p.sel < end) {
        int selRow = p.sel - p.scroll;
        DrawRect(dev, baseX, st.listY + selRow*st.lineH - 2.0f, st.listW-8.0f, st.lineH, active?0x60FFFF00:0x30FFFF00);
    }

    // rows
    FLOAT y = st.listY;
    for (int i = p.scroll, r = 0; i < end; ++i, ++r) {
        const Item& it = p.items[i];
        DWORD nameCol = (i==p.sel)?0xFFFFFF00:0xFFE0E0E0;
        DWORD sizeCol = (i==p.sel)?0xFFFFFF00:0xFFB0B0B0;
        D3DCOLOR ico = it.isUpEntry ? 0xFFAAAAAA
                            : (it.marked ? 0xFFFF4040               // RED when marked
                                         : (it.isDir ? 0xFF5EA4FF   // folder blue
                                                     : 0xFF89D07E));// file green
        DrawRect(dev, baseX+2.0f, y+6.0f, st.gutterW-8.0f, st.lineH-12.0f, ico);

        const char* glyph = it.isUpEntry ? ".." : (it.isDir?"+":"-");
        DrawAnsi(font, baseX+4.0f, y+4.0f, 0xFFFFFFFF, glyph);

        char nameBuf[300]; _snprintf(nameBuf, sizeof(nameBuf), "%s", it.name); nameBuf[sizeof(nameBuf)-1] = 0;
        const FLOAT nameX    = NameColX(baseX, st);
		// room for size column + padding + scrollbar on the right
		const FLOAT rightPad = st.paddingX + st.scrollBarW + 2.0f;
		const FLOAT nameMaxW = (baseX + st.listW) - rightPad - nameX - ComputeSizeColW(font, p, st);
		// or: const FLOAT nameMaxW = (sizeColX - 8.0f) - nameX;  // also fine

		const bool isSel = active && (i == p.sel);
		DrawNameFittedOrMarquee(font, nameX, y, nameMaxW,
								nameCol, nameBuf, isSel, paneIndex, i - p.scroll);

        char sz[64]=""; if(!it.isDir && !it.isUpEntry){ FormatSize(it.size, sz, sizeof(sz)); }
        DrawRightAligned(font, sz, sizeRight, y, sizeCol);

        y += st.lineH;
    }

    // scrollbar
    if ((int)p.items.size() > st.visibleRows) {
        FLOAT trackX = baseX + st.listW - st.scrollBarW - 4.0f;
        FLOAT trackY = st.listY - 2.0f;
        FLOAT trackH = st.visibleRows * st.lineH;
        DrawRect(dev, trackX, trackY, st.scrollBarW, trackH, 0x40282828);
        int total = (int)p.items.size();
        FLOAT thumbH = (FLOAT)st.visibleRows/(FLOAT)total * trackH; if (thumbH < 10.0f) thumbH = 10.0f;
        FLOAT maxScroll = (FLOAT)(total - st.visibleRows);
        FLOAT t = (maxScroll > 0.0f) ? ((FLOAT)p.scroll / maxScroll) : 0.0f;
        FLOAT thumbY = trackY + t * (trackH - thumbH);
        DrawRect(dev, trackX, thumbY, st.scrollBarW, thumbH, 0x80C0C0C0);
    }
}

