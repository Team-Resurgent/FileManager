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

void PaneRenderer::DrawPane(
    CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT baseX,
    const Pane& p, bool active, const PaneStyle& st)
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
        DrawAnsi(font, NameColX(baseX, st), y, nameCol, nameBuf);

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

