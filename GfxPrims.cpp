#include "GfxPrims.h"

void DrawSolidRect(LPDIRECT3DDEVICE8 dev, float x, float y, float w, float h, D3DCOLOR c){
    TLVERT v[4] = {
        { x,     y,     0.0f, 1.0f, c },
        { x + w, y,     0.0f, 1.0f, c },
        { x,     y + h, 0.0f, 1.0f, c },
        { x + w, y + h, 0.0f, 1.0f, c },
    };
    dev->SetTexture(0, NULL);
    dev->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_DISABLE);
    dev->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_DISABLE);
    dev->SetVertexShader(FVF_TLVERT);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(TLVERT));
}
