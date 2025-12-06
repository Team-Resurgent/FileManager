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

extern ColorMap DefaultColors;

inline FLOAT Snap(FLOAT v) { return (FLOAT)((int)(v + 0.5f)); } // pixel-align

FLOAT DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap* colors, const char* text);
FLOAT DrawAnsiCentered(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap* colors, const char* text, FLOAT width, FLOAT height = NULL);
FLOAT DrawAnsiFromRight(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap* colors, const char* text, FLOAT height = NULL);

FLOAT GetAnsiW(CXBFont& font, const char* text);
void GetAnsiWH(CXBFont& font, const char* text, FLOAT* width, FLOAT* height);

