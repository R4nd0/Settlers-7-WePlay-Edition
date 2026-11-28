#define CINTERFACE
#include <d3d9.h>
#include <cstddef>
#include <cstdio>
#define SLOT(name,index) static_assert(offsetof(IDirect3DDevice9Vtbl,name)==index*sizeof(void*),"D3D9 " #name " ABI")
SLOT(Present,17);SLOT(Reset,16);SLOT(StretchRect,34);SLOT(ColorFill,35);
SLOT(SetRenderTarget,37);SLOT(GetRenderTarget,38);SLOT(SetDepthStencilSurface,39);SLOT(GetDepthStencilSurface,40);
SLOT(Clear,43);SLOT(SetViewport,47);SLOT(GetViewport,48);SLOT(GetTexture,64);SLOT(SetTexture,65);
SLOT(BeginStateBlock,60);SLOT(EndStateBlock,61);
SLOT(SetScissorRect,75);SLOT(GetScissorRect,76);SLOT(DrawPrimitive,81);SLOT(DrawIndexedPrimitive,82);
SLOT(DrawPrimitiveUP,83);SLOT(DrawIndexedPrimitiveUP,84);SLOT(SetVertexShader,92);SLOT(SetPixelShader,107);
SLOT(SetVertexShaderConstantF,94);SLOT(GetVertexShaderConstantF,95);
SLOT(GetVertexShaderConstantI,97);SLOT(GetVertexShaderConstantB,99);SLOT(GetStreamSource,101);SLOT(GetStreamSourceFreq,103);SLOT(GetIndices,105);
static_assert(offsetof(IDirect3DStateBlock9Vtbl,Capture)==4*sizeof(void*),"StateBlock Capture ABI");
static_assert(offsetof(IDirect3DStateBlock9Vtbl,Apply)==5*sizeof(void*),"StateBlock Apply ABI");
int main(){std::puts("{\"passed\":true,\"sdk_abi_checks\":32,\"synthetic_only\":true}");}
