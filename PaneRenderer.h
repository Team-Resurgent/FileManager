#ifndef PANERENDERER_H
#define PANERENDERER_H

#include <xtl.h>
#include "XBFont.h"
#include "PaneModel.h"

// Style/config for drawing a pane (filled from FileBrowserApp layout)
struct PaneStyle {
    FLOAT listW, listY, lineH;
    FLOAT hdrY, hdrH, hdrW;
    FLOAT gutterW, paddingX, scrollBarW;
    int   visibleRows;
};

class PaneRenderer {
public:
    // Draw a single pane at baseX
    void DrawPane(
        CXBFont& font,
        LPDIRECT3DDEVICE8 dev,
        FLOAT baseX,
        const Pane& p,
        bool active,
        const PaneStyle& st);

private:
    // Local helpers (no global pollution)
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
