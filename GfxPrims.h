#pragma once
#include <xtl.h>

// Transformed/lit vertex for solid rects
struct TLVERT { float x, y, z, rhw; D3DCOLOR color; };
enum { FVF_TLVERT = D3DFVF_XYZRHW | D3DFVF_DIFFUSE };

// Draws a filled rectangle in screen space
void DrawSolidRect(LPDIRECT3DDEVICE8 dev, float x, float y, float w, float h, D3DCOLOR c);
