#include "textUtils.h"

#include "configuration.h"

ColorMap DefaultColors(true);

void ColorMap::PopulateWithDefaults() {
    m_colors[0x80] = A_BUTTON_COLOR;     // A Button
    m_colors[0x81] = B_BUTTON_COLOR;     // B Button
    m_colors[0x82] = X_BUTTON_COLOR;     // X Button
    m_colors[0x83] = Y_BUTTON_COLOR;     // Y Button
    m_colors[0x84] = BACK_BUTTON_COLOR;  // Back Button
    m_colors[0x85] = START_BUTTON_COLOR; // Start Button
    m_colors[0x86] = BLACK_BUTTON_COLOR; // Black Button
    m_colors[0x87] = WHITE_BUTTON_COLOR; // White Button
    m_colors[0x88] = D_PAD_COLOR;        // D-Pad
    m_colors[0x89] = D_PAD_COLOR;        // D-Pad Left + Right
    m_colors[0x8A] = D_PAD_COLOR;        // D-Pad Up + Down
    m_colors[0x8B] = D_PAD_COLOR;        // D-Pad Left
    m_colors[0x8C] = D_PAD_COLOR;        // D-Pad Right
    m_colors[0x8D] = D_PAD_COLOR;        // D-Pad Up
    m_colors[0x8E] = D_PAD_COLOR;        // D-Pad Down
    m_colors[0x8F] = L_STICK_COLOR;      // Left Stick Depress
    m_colors[0x90] = R_STICK_COLOR;      // Right Stick Depress
    m_colors[0x91] = L_TRIGGER_COLOR;    // Left Trigger
    m_colors[0x92] = R_TRIGGER_COLOR;    // Right Trigger
    m_colors[0x93] = L_STICK_COLOR;      // Left Stick
    m_colors[0x94] = R_STICK_COLOR;      // Right Stick
    m_colors[0x95] = XBOX_LOGO_COLOR;    // X Logo
    //       0x96                        // Unused
    //       0x97                        // Unused
    m_colors[0x98] = MEMORY_UNIT_COLOR;  // Memory Unit
    m_colors[0x99] = ZIP_FILE_COLOR;     // Zip File
    m_colors[0x9A] = PARTITION_COLOR;    // Partition
    m_colors[0x9B] = HDD_COLOR;          // HDD
    m_colors[0x9C] = DISC_COLOR;         // Disc
    //       0x9D                        // Full Blank Space
    m_colors[0x9E] = FILE_COLOR;         // File
    m_colors[0x9F] = FOLDER_COLOR;       // Folder
}

FLOAT DrawAnsi(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap* colors, const char* text) {
    x = Snap(x);
    y = Snap(y);

    if (colors == NULL) {
        WCHAR* wbuf = (WCHAR*)malloc(sizeof(WCHAR) * (strlen(text) + 1));
        MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf, strlen(text) + 1);
        font.DrawText(x, y, color, wbuf);
        x += font.GetTextWidth(wbuf);
        free(wbuf);
    }
    else {
        int l = strlen(text);
        for (int i = 0; i < l; i++) {
            char ch = text[i];
            DWORD c = (colors->m_colors[(unsigned char)ch] != 0) ? colors->m_colors[(unsigned char)ch] : color;

            WCHAR wbuf[2];
            MultiByteToWideChar(CP_ACP, 0, &ch, 1, wbuf, 2);
            wbuf[1] = '\0';
            font.DrawText(x, y, c, wbuf, 0, 0.0f);
            x += font.GetTextWidth(wbuf);
        }
    }

    return x;
}

FLOAT DrawAnsiCentered(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap* colors, const char* text, FLOAT w, FLOAT h) {
    FLOAT tw, th;
    GetAnsiWH(font, text, &tw, &th);
    
    if (w != NULL) x = x + (w - tw) * 0.5f;
    if (h != NULL) y = y + (h - th) * 0.5f;

    return DrawAnsi(font, x, y, color, colors, text);
}

FLOAT DrawAnsiFromRight(CXBFont& font, FLOAT x, FLOAT y, DWORD color, ColorMap* colors, const char* text, FLOAT h) {
    FLOAT tw = GetAnsiW(font, text);
    if (h != NULL) DrawAnsiCentered(font, x - tw, y, color, colors, text, NULL, h);
    else DrawAnsi(font, x - tw, y, color, colors, text);

    return x - tw;
}

FLOAT GetAnsiW(CXBFont& font, const char* text) {
    WCHAR* wbuf = (WCHAR*)malloc(sizeof(WCHAR) * (strlen(text) + 1));
    MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf, strlen(text) + 1);

    FLOAT tw = font.GetTextWidth(wbuf);
    free(wbuf);

    return tw;
}

void GetAnsiWH(CXBFont& font, const char* text, FLOAT* w, FLOAT* h) {
    WCHAR* wbuf = (WCHAR*)malloc(sizeof(WCHAR) * (strlen(text) + 1));
    MultiByteToWideChar(CP_ACP, 0, text, -1, wbuf, strlen(text) + 1);

    font.GetTextExtent(wbuf, w, h);
    free(wbuf);
}

void EllipsizeAnsiToFit(CXBFont& font, const char* src, FLOAT mW, char* dst, size_t s, EllipsizeMode mode) {
    if (!dst || s < 4 || src == NULL || src[0] == '\0') {
        if (dst && s > 0) dst[0] = '\0';
        return;
    }

    FLOAT tW = GetAnsiW(font, src);
    if (tW <= mW) {
        strncpy(dst, src, s);
        dst[s - 1] = '\0';
        return;
    }

    const char* ellipses = "...";
    mW -= GetAnsiW(font, ellipses);

    switch (mode) {
    case ELLIPSIZE_LEFT:
    {
        strcpy(dst, ellipses);
        const char* p = src + strlen(src);
        for (;;) {
            if (p == src) break;
            if (strlen(p) >= s - 4) break;
            --p;
            if (GetAnsiW(font, p) > mW) {
                ++p;
                break;
            }
        }
        strcat(dst, p);
        break;
    }

    case ELLIPSIZE_RIGHT:
    {
        memset(dst, 0, sizeof(char) * s);
        int l = strlen(src);
        for (int i = 0; i < l; i++) {
            if (i >= s - 4) break;
            dst[i] = src[i];
            if (GetAnsiW(font, dst) > mW) {
                dst[i] = '\0';
                break;
            }
        }

        strcat(dst, ellipses);
        break;
    }

    case ELLIPSIZE_CENTER:
    {
        int l = strlen(src);         

        const char* ls = src + (l / 2);         const char* lp = src;
        const char* rs = (l % 2) ? ls + 1 : ls; const char* rp = src + l;        

        char* lb = (char*)malloc(sizeof(char) * s);
        memset(lb, 0, sizeof(char) * s);

        for (;;) {
            if (lp == ls || rp == rs) break;
            if ((lp - src) + strlen(rp) >= s - 4) break;
            ++lp; --rp;
            lb[lp - src - 1] = src[lp - src - 1];
            if (GetAnsiW(font, lb) + GetAnsiW(font, rp) > mW) {
                --lp; ++rp;
                lb[lp - src - 1] = '\0';
                break;
            }
        }
        strcpy(dst, lb);
        strcat(dst, ellipses);
        strcat(dst, rp);

        free(lb);

        break;
    }
    } // switch
}