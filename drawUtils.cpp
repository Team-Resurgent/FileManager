#include "drawUtils.h"

struct TLVERT { 
    float x, y, z, rhw; 
    D3DCOLOR color; 
};

void DrawSolidRect(LPDIRECT3DDEVICE8 dev, float x, float y, float w, float h, D3DCOLOR c) {
    TLVERT v[4] = {
        { x,     y,     0.0f, 1.0f, c },
        { x + w, y,     0.0f, 1.0f, c },
        { x,     y + h, 0.0f, 1.0f, c },
        { x + w, y + h, 0.0f, 1.0f, c },
    };
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_DISABLE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_DISABLE);
    dev->SetVertexShader(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLVERT));
}
