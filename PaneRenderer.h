#pragma once

#include "TextUtils.h"
#include "FSUtil.h"

struct Pane {
    std::vector<Item> items;
    char  curPath[512];
    int   mode; // 0 = drives, 1 = dir
    int   sel;
    int   scroll;
    Pane() : curPath(), mode(0), sel(0), scroll(0) {}
};

struct PaneStyle {
    FLOAT listW, listY, lineH;           // list width, top Y, per-row height
    FLOAT hdrY, hdrH, hdrW;              // header band position/size
    FLOAT gutterW, paddingX, scrollBarW; // icon gutter, left text pad, scroll width
    int   visibleRows;                   // rows visible in the list area
};

struct MarqueeState {
    int   row;        // scrolling row (-1 if none) — rows only (header ignores)
    FLOAT px;         // start index for visible substring (cast to int when used)
    DWORD nextTick;   // next advance time (GetTickCount-based)
    DWORD resetPause; // pause-at-end timer (0 when not pausing)
    FLOAT fitWLock;   // locked visible width while marquee is active
    MarqueeState() : row(-1), px(0.0f), nextTick(0), resetPause(0), fitWLock(0.0f) {}
};

class PaneRenderer {
public:
    FLOAT s_sharedSizeColW;
    PaneRenderer() { s_sharedSizeColW = 0.0f; }

    inline void ResetSharedSizeColW() { s_sharedSizeColW = 0.0f; }

    void PrimeSharedSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st);
    void DrawPane(CXBFont& font, LPDIRECT3DDEVICE8 dev, FLOAT baseX, const Pane& p, bool active, const PaneStyle& st, int paneIndex);    

private:
    void DrawNameFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT h, FLOAT maxW, DWORD color, const char* name, bool isSelected, int paneIndex, int rowIndex);
    void DrawHeaderFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT h, FLOAT maxW, DWORD color, const char* text, int paneIndex);

    static FLOAT NameColX(FLOAT baseX,const PaneStyle& st){ return baseX + st.gutterW + st.paddingX; }
    
    MarqueeState m_marq[2];    // rows
    MarqueeState m_hdrMarq[2]; // headers    
};
