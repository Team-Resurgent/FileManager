#ifndef PANERENDERER_H
#define PANERENDERER_H

#include <xtl.h>
#include "XBFont.h"
#include "PaneModel.h"

/*
===============================================================================
 PaneRenderer
  - Renders a single file-browser pane (header, list, selection, sizes, scroll).
  - No STL allocations in hot paths; uses ANSI->wide conversions for XDK fonts.
  - Marquee support for long filenames on the selected row.
===============================================================================
*/

/*
------------------------------------------------------------------------------
 PaneStyle
  - View metrics provided by the caller (layout is owned by app, not renderer).
  - All values are in screen-space pixels.
------------------------------------------------------------------------------
*/
struct PaneStyle {
    FLOAT listW, listY, lineH;               // list width, top Y, per-row height
    FLOAT hdrY, hdrH, hdrW;                  // header band position/size
    FLOAT gutterW, paddingX, scrollBarW;     // icon gutter, left text pad, scroll width
    int   visibleRows;                       // rows visible in the list area
};

/*
------------------------------------------------------------------------------
 MarqueeState
  - Per-pane state for scrolling a clipped filename on the selected row.
  - row = -1 means no active marquee; px is the current leftward skip in pixels.
------------------------------------------------------------------------------
*/
struct MarqueeState {
    int   row;          // scrolling row (-1 if none)
    FLOAT px;           // pixel offset
    DWORD nextTick;     // next advance time (GetTickCount-based)
    DWORD resetPause;   // pause-at-end timer (0 when not pausing)
    MarqueeState() : row(-1), px(0.0f), nextTick(0), resetPause(0) {}
};

class PaneRenderer {
public:
    /*
    ----------------------------------------------------------------------------
     DrawPane
      - Master entry: draws header, column headers, stripes, selection,
        filename and size columns, and scrollbar.
      - 'active' toggles emphasis (for example, brighter header and selection).
      - paneIndex selects marquee state (0 = left pane, 1 = right pane).
    ----------------------------------------------------------------------------
    */
    void DrawPane(
        CXBFont& font,
        LPDIRECT3DDEVICE8 dev,
        FLOAT baseX,
        const Pane& p,
        bool active,
        const PaneStyle& st,
        int paneIndex);                // new

private:
    // Marquee helpers (selected row only; avoid allocations)
    void  DrawNameFittedOrMarquee(CXBFont& font, FLOAT x, FLOAT y, FLOAT maxW,
                                  DWORD color, const char* name,
                                  bool isSelected, int paneIndex, int rowIndex);
    void  RightEllipsizeToFit(CXBFont& font, const char* src, FLOAT maxW,
                              char* out, size_t cap); // binary search for fit + "..."
    const char* SkipToPixelOffset(CXBFont& font, const char* s, FLOAT px, FLOAT& skippedW);

    // State for left (0) and right (1) panes. Kept here so DrawPane remains
    // stateless with respect to the model while marquee scroll stays smooth.
    MarqueeState m_marq[2];            // new

private:
    // Existing helpers (ANSI -> wide text, primitives, metrics)
    static void  DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, const char* text);
    static void  DrawRect(LPDIRECT3DDEVICE8 dev, float x,float y,float w,float h,D3DCOLOR c);
    static void  DrawRightAligned(CXBFont& font, const char* s, FLOAT rightX, FLOAT y, DWORD color);
    static FLOAT MeasureTextW(CXBFont& font, const char* s);
    static void  MeasureTextWH(CXBFont& font, const char* s, FLOAT& outW, FLOAT& outH);

    // Small math/layout helpers
    static FLOAT MaxF(FLOAT a, FLOAT b){ return (a>b)?a:b; }
    static FLOAT Snap (FLOAT v){ return (FLOAT)((int)(v + 0.5f)); } // pixel snapping
    static FLOAT NameColX(FLOAT baseX,const PaneStyle& st){ return baseX + st.gutterW + st.paddingX; }

    // Compute the size column width (data-driven, clamped to sane bounds).
    static FLOAT ComputeSizeColW(CXBFont& font, const Pane& p, const PaneStyle& st);
};

#endif // PANERENDERER_H
