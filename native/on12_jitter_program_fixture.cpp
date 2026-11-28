// CPU shader compilation and transformation; no D3D device is created.
#include "on12_jitter_program.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>

using Microsoft::WRL::ComPtr;
using On12Graphics::JitterProgram;
namespace {
unsigned checks=0;bool Check(bool value,unsigned line){++checks;if(!value)std::fprintf(stderr,"jitter program check failed at %u\n",line);return value;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
ComPtr<ID3DBlob> Compile(const char* text){ComPtr<ID3DBlob> code,error;const HRESULT hr=D3DCompile(text,std::strlen(text),"authored-jitter-fixture",nullptr,nullptr,"VS","vs_3_0",D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(hr)&&error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));return code;}
bool Write(const wchar_t* name,const void* bytes,size_t count){FILE* file=nullptr;if(_wfopen_s(&file,name,L"wb")||!file)return false;const bool good=std::fwrite(bytes,1,count,file)==count;std::fclose(file);return good;}
}
int main(){
    const char* source="float4x4 g_VSMainViewProjectionMatrix:register(c12); struct I{float4 p:POSITION0;float4 c:COLOR0;};struct O{float4 p:POSITION0;float4 c:COLOR0;};O VS(I i){O o;o.p=mul(g_VSMainViewProjectionMatrix,i.p);o.c=i.c;return o;}";
    auto original=Compile(source);REQUIRE(original);std::vector<DWORD> before(original->GetBufferSize()/4);std::memcpy(before.data(),original->GetBufferPointer(),original->GetBufferSize());
    JitterProgram result;REQUIRE(On12Graphics::MakeJitterProgram(original->GetBufferPointer(),original->GetBufferSize(),result));
    REQUIRE(result.constant<256&&!(result.constant>=12&&result.constant<16)&&result.temporary<32&&result.position<12);
    REQUIRE(result.words.size()==before.size()+8&&!std::memcmp(before.data(),original->GetBufferPointer(),original->GetBufferSize()));
    ComPtr<ID3DBlob> assembly;REQUIRE(SUCCEEDED(D3DDisassemble(result.words.data(),result.words.size()*4,0,nullptr,&assembly)));
    REQUIRE(Write(L"jitter-original.private.bin",before.data(),before.size()*4)&&Write(L"jitter-patched.private.bin",result.words.data(),result.words.size()*4));
    const char* bones="float4x4 g_VSMainViewProjectionMatrix:register(c12);float4 g_Bones[224]:register(c32);float4 VS(float4 p:POSITION0,float4 c:COLOR0):POSITION0{return mul(g_VSMainViewProjectionMatrix,p+g_Bones[(int)c.x]);}";
    auto skin=Compile(bones);REQUIRE(skin);JitterProgram skinned;REQUIRE(On12Graphics::MakeJitterProgram(skin->GetBufferPointer(),skin->GetBufferSize(),skinned));
    REQUIRE(skinned.constant<32&&!(skinned.constant>=12&&skinned.constant<16)); // No collision with dynamic c32..c255 bone indexing.
    const char* billboard="float4x4 g_View2ClipTM:register(c64);float4x4 g_Object2ViewTM:register(c68);float4 VS(float4 p:POSITION0):POSITION0{return mul(g_View2ClipTM,mul(g_Object2ViewTM,p));}";
    auto card=Compile(billboard);REQUIRE(card);JitterProgram card_result;REQUIRE(On12Graphics::MakeJitterProgram(card->GetBufferPointer(),card->GetBufferSize(),card_result));
    const char* ui="float4x4 g_UiProjection:register(c64);float4 VS(float4 p:POSITION0):POSITION0{return mul(g_UiProjection,p);}";
    auto overlay=Compile(ui);REQUIRE(overlay);REQUIRE(!On12Graphics::MakeJitterProgram(overlay->GetBufferPointer(),overlay->GetBufferSize(),result)&&result.words.empty());
    REQUIRE(!On12Graphics::MakeJitterProgram(nullptr,0,result));
    REQUIRE(!On12Graphics::MakeJitterProgram(before.data(),before.size()*4-4,result));
    auto wrong=before;wrong[0]=D3DPS_VERSION(3,0);REQUIRE(!On12Graphics::MakeJitterProgram(wrong.data(),wrong.size()*4,result));
    wrong=before;wrong.insert(wrong.end()-1,D3DSIO_RET);REQUIRE(!On12Graphics::MakeJitterProgram(wrong.data(),wrong.size()*4,result));
    wrong=before;wrong.push_back(D3DSIO_END);REQUIRE(!On12Graphics::MakeJitterProgram(wrong.data(),wrong.size()*4,result));
    for(size_t count=0;count<before.size();++count)REQUIRE(!On12Graphics::MakeJitterProgram(before.data(),count*4,result));
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"bone_palette_preserved\":true}\n",checks);return 0;
}
