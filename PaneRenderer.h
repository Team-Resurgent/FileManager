// PaneRenderer.h
#ifndef PANERENDERER_H
#define PANERENDERER_H

#include <xtl.h>
#include "XBFont.h"
#include "PaneModel.h"

struct PaneStyle {
    FLOAT listW, listY, lineH;
    FLOAT hdrY, hdrH, hdrW;
    FLOAT gutterW, paddingX, scrollBarW;
    int   visibleRows;
};

struct MarqueeState {
    int   row;          // scrolling row (-1 if none)
    FLOAT px;           // pixel offset
    DWORD nextTick;     // next advance time
    DWORD resetPause;   // pause-at-end timer
    MarqueeState() : row(-1), px(0.0f), nextTick(0), resetPause(0) {}
};

class PaneRenderer {
public:
    void DrawPane(
        CXBFont& font,
        LPDIRECT3DDEVICE8 dev,
        FLOAT baseX,
        const Pane& p,
        bool active,
        const PaneStyle& st,
        int paneIndex);                // <-- NEW

private:
    // --- NEW: marquee helpers ---
    void  DrawNameFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT maxW,
                                  DWORD color, const char* name,
                                  bool isSelected, int paneIndex, int rowIndex);
    void  RightEllipsizeToFit(CXBFont& font, const char* src, FLOAT maxW,
                              char* out, size_t cap);
    const char* SkipToPixelOffset(CXBFont& font, const char* s, FLOAT px, FLOAT& skippedW);

    // state for left(0) / right(1) pane
    MarqueeState m_marq[2];            // <-- NEW

private:
    // existing helpers
    static void  DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    static void  DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    static void  DrawRightAligned(CXBFont& font, const char* s, FLOAT rightX, FLOAT y, DWORD color);
    static FLOAT MeasureTextW(CXBFont& font, const char* s);
    static void  MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH);

    static FLOAT MaxF(FLOAT a, FLOAT b){ return (a>b)?a:b; }
    static FLOAT Snap (FLOAT v){ return (FLOAT)((int)(v + 0.5f)); }
    static FLOAT NameColX(FLOAT baseX,const PaneStyle& st){ return baseX + st.gutterW + st.paddingX; }

    static FLOAT ComputeSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st);
};

#endif // PANERENDERER_H
