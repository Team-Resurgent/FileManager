#include "TextUtils.h"
#include "Configuration.h"

ColorMap DefaultColorMap(true);

void ColorMap::PopulateWithDefaults() {
    m_colors[0x80] = A_BUTTON_COLOR; // A Button
    m_colors[0x81] = B_BUTTON_COLOR; // B Button
    m_colors[0x82] = X_BUTTON_COLOR; // X Button
    m_colors[0x83] = Y_BUTTON_COLOR; // Y Button
}

void DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap& colors, const char* text) {
    int l = strlen(text);
    for (int i = 0; i < l; i++) {
        char ch = text[i];
        DWORD c = (colors.m_colors[(unsigned char)ch] != 0) ? colors.m_colors[(unsigned char)ch] : color;

        WCHAR wbuf[2];
        MultiByteToWideChar(CP_ACP, 0, &ch, 1, wbuf, 2);
        wbuf[1] = '\0';
        font.DrawText(x, y, c, wbuf, 0, 0.0f);

        x += font.GetTextWidth(wbuf);
    }
}

void DrawAnsiCentered(CXBFont& font, FLOAT x, FLOAT w, FLOAT y, FLOAT h, DWORD color, ColorMap& colors, const char* text) {
    WCHAR* wbuf = (WCHAR*)malloc(sizeof(WCHAR) * (strlen(text) + 1));
    MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf, strlen(text) + 1);
    FLOAT tw, th;
    font.GetTextExtent(wbuf, &tw, &th);
    free(wbuf);

    if (w != NULL) x = Snap(x + (w - tw) * 0.5f);
    if (h != NULL) y = Snap(y + (h - th) * 0.5f);

    DrawAnsi(font, x, y, color, colors, text);
}