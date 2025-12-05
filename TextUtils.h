#pragma once

#include <XTL.h>
#include "XBFont.h"

class ColorMap {
public:
    DWORD m_colors[256] = { 0 };
    ColorMap(bool populateWithDefaults = false) { if (populateWithDefaults) PopulateWithDefaults(); };

private:
    void PopulateWithDefaults();
};

extern ColorMap DefaultColorMap;

inline FLOAT Snap(FLOAT v) { return (FLOAT)((int)(v + 0.5f)); } // pixel-align

void DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap& colors, const char* text);

// Pass NULL for w or h to center in one axis
void DrawAnsiCentered(CXBFont& font, FLOAT x, FLOAT w, FLOAT y, FLOAT h, DWORD color, ColorMap& colors, const char* text);

