#ifndef PANERENDERER_H
#define PANERENDERER_H

#include <xtl.h>
#include "XBFont.h"
#include "PaneModel.h"

/*
============================================================================
 PaneRenderer (header)
  - Public renderer API for a single file-browser pane (OG Xbox / XDK/VS2003)
  - Declares PaneStyle (layout metrics) and MarqueeState (scroll state)
  - Exposes DrawPane(...) and per-frame shared size-column helpers
  - Character-step marquee for long names/paths (crisp, no subpixel drift)
  - No scissor: clipping achieved by measuring and drawing the substring that fits
  - Usage:
      * PaneRenderer::BeginFrameSharedCols()
      * pane.PrimeSharedSizeColW(font, model, style)   // for each pane
      * pane.DrawPane(font, dev, baseX, model, active, style, paneIndex)
============================================================================
*/


//------------------------------------------------------------------------------
// View metrics supplied by the caller. All coordinates are screen-space pixels.
//------------------------------------------------------------------------------
struct PaneStyle {
    FLOAT listW, listY, lineH;               // list width, top Y, per-row height
    FLOAT hdrY, hdrH, hdrW;                  // header band position/size
    FLOAT gutterW, paddingX, scrollBarW;     // icon gutter, left text pad, scroll width
    int   visibleRows;                       // rows visible in the list area
};

//------------------------------------------------------------------------------
// Per-pane marquee state (selected row + header path).
// px stores the current start index (as a float but used as int).
//------------------------------------------------------------------------------
struct MarqueeState {
    int   row;          // scrolling row (-1 if none) — rows only (header ignores)
    FLOAT px;           // start index for visible substring (cast to int when used)
    DWORD nextTick;     // next advance time (GetTickCount-based)
    DWORD resetPause;   // pause-at-end timer (0 when not pausing)
    FLOAT fitWLock;     // locked visible width while marquee is active
    MarqueeState() : row(-1), px(0.0f), nextTick(0), resetPause(0), fitWLock(0.0f) {}
};

class PaneRenderer {
public:
    //--------------------------------------------------------------------------
    // DrawPane: master entry. Draws header, columns, rows, sizes, scrollbar.
    // 'active' toggles emphasis (e.g., brighter header/selection).
    // paneIndex 0 = left pane, 1 = right pane (indexes marquee state arrays).
    //--------------------------------------------------------------------------
    void DrawPane(
        CXBFont& font,
        LPDIRECT3DDEVICE8 dev,
        FLOAT baseX,
        const Pane& p,
        bool active,
        const PaneStyle& st,
        int paneIndex);

    //--------------------------------------------------------------------------
    // Shared size-column width (aligns the size column across both panes):
    //  - Call BeginFrameSharedCols() once at frame start
    //  - Call PrimeSharedSizeColW() for each pane before drawing either one
    //  - DrawPane() uses GetSharedSizeColW() to align columns
    //--------------------------------------------------------------------------
    static void  BeginFrameSharedCols();
    static void  UpdateSharedSizeColW(FLOAT w);
    static FLOAT GetSharedSizeColW();
    void         PrimeSharedSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st);

private:
    // Looping marquee (character-step) for selected row filenames
    void DrawNameFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT maxW,
                                 DWORD color, const char* name,
                                 bool isSelected, int paneIndex, int rowIndex);

    // Looping marquee (character-step) for header path
    void DrawHeaderFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT maxW,
                                   DWORD color, const char* text, int paneIndex);

    // Drawing / measure helpers
    static void  DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    static void  DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    static void  DrawRightAligned(CXBFont& font, const char* s, FLOAT rightX, FLOAT y, DWORD color);
    static FLOAT MeasureTextW(CXBFont& font, const char* s);
    static void  MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH);

    // Text fitting
    void         RightEllipsizeToFit(CXBFont& font, const char* src, FLOAT maxW,
                                     char* out, size_t cap);

    // Small math/layout helpers
    static FLOAT MaxF (FLOAT a, FLOAT b){ return (a>b)?a:b; }
    //static FLOAT Snap (FLOAT v){ return (FLOAT)((int)(v + 0.5f)); } // pixel snapping
    static FLOAT NameColX(FLOAT baseX,const PaneStyle& st){ return baseX + st.gutterW + st.paddingX; }
    static int   MinI (int a, int b){ return (a<b)?a:b; }

    // Data-driven size column width
    static FLOAT ComputeSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st);

    // Per-pane marquee state
    MarqueeState m_marq[2];      // rows
    MarqueeState m_hdrMarq[2];   // headers

    // Shared width backing field
    static FLOAT s_sharedSizeColW;
};

#endif // PANERENDERER_H
