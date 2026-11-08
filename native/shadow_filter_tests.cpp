#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}
int main() {
 int checks=0,failures=0;
 auto check=[&](bool ok){++checks;if(!ok){++failures;std::printf("failure %d\n",checks);}};
 auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1106000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
 if(!memory)return 2;
 image_base=reinterpret_cast<uintptr_t>(memory);
 for(bool steam:{false,true}) {
  steam_history=steam;auto* at=PatchAddress(ShadowFilterPatch);
  const unsigned char before[]={0x83,0xc4,0x0c};
  const unsigned char after[]={0x50,0xff,0xb3,0xac,0x19,0,0,0x8b,0x83,0xb0,0x19,0,0,0xff,0xd0};
  const unsigned char index[]={0xf3,0x0f,0x10,0x84,0xb4,0xc8,0,0,0}; memcpy(at-0x52,index,sizeof(index)); memcpy(at-3,before,3);memcpy(at+5,after,sizeof(after));memcpy(at,OriginalBytes(ShadowFilterPatch),5);
  check(PrepareShadowFilter());check(WritePatch(ShadowFilterPatch,false));check(at[0]==0xe8);
  check(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(at)+5+*reinterpret_cast<int32_t*>(at+1))==reinterpret_cast<uintptr_t>(&SelectShadowFilterStep));
  check(WritePatch(ShadowFilterPatch,true));check(!memcmp(at,OriginalBytes(ShadowFilterPatch),5));
  for(int offset=-3;offset<20;++offset) {at[offset]^=1;check(!PrepareShadowFilter());at[offset]^=1;check(PrepareShadowFilter());}
  for(int offset=-0x52;offset< -0x52+9;++offset) {at[offset]^=1;check(!PrepareShadowFilter());at[offset]^=1;check(PrepareShadowFilter());}
  unsigned char manager[256]={};uintptr_t owner=reinterpret_cast<uintptr_t>(manager);shadow_filter_owner_slot=&owner;
  auto* fpBefore=static_cast<unsigned char*>(_aligned_malloc(512,16));auto* fpAfter=static_cast<unsigned char*>(_aligned_malloc(512,16));
  memset(fpBefore,0,512);memset(fpAfter,0,512);
  for(bool right_owner:{false,true}) for(uint32_t size:{512u,1024u,2048u,4096u,8192u,16384u}) for(int corrupt=-1;corrupt<4;++corrupt) for(uint32_t cascade:{0u,1u,2u,3u,4u,0xffffffffu}) {
   *reinterpret_cast<uint32_t*>(manager+0x78)=size;
   float* inverse=reinterpret_cast<float*>(manager+0x7c);
   for(int i=0;i<4;++i)inverse[i]=1.f/static_cast<float>(size);
   if(corrupt>=0)reinterpret_cast<uint32_t*>(inverse)[corrupt]^=1;
   owner=right_owner?reinterpret_cast<uintptr_t>(manager):0;
   unsigned char copy[256];memcpy(copy,manager,256);
   uint32_t stackout[17]={},regs[6]={},flagsBefore=0,flagsAfter=0;
   __asm {
    sub esp,040h
    mov edi,esp
    mov ecx,16
    mov eax,0x1234abcd
    rep stosd
    lea ebx,manager
    mov eax,0x11223344
    mov ecx,0x22334455
    mov edx,0x33445566
    mov esi,cascade
    mov edi,fpBefore
    fxsave [edi]
    mov edi,0x55667788
    pushfd
    pop flagsBefore
    call SelectShadowFilterStep
    pushfd
    pop flagsAfter
    push edi
    mov edi,fpAfter
    fxsave [edi]
    pop edi
    mov regs[0],eax
    mov regs[4],ecx
    mov regs[8],edx
    mov regs[12],esi
    mov regs[16],edi
    mov regs[20],ebx
    mov esi,esp
    lea edi,stackout
    mov ecx,17
    rep movsd
    add esp,044h
   }
   const auto expected=reinterpret_cast<uintptr_t>((right_owner&&corrupt<0&&cascade>=2&&cascade<=3&&(size==4096||size==8192))?ShadowFilterStep:inverse);
   check(flagsBefore==flagsAfter);
   check(regs[0]==expected&&regs[1]==0x22334455&&regs[2]==0x33445566&&regs[3]==cascade&&regs[4]==0x55667788&&regs[5]==reinterpret_cast<uintptr_t>(manager));
   bool stackok=stackout[0]==1;for(int i=1;i<17;++i)if(stackout[i]!=0x1234abcd)stackok=false;
   check(stackok);check(!memcmp(fpBefore,fpAfter,288));check(!memcmp(copy,manager,256));
  }
  _aligned_free(fpBefore);_aligned_free(fpAfter);
 }
 for(float value:ShadowFilterStep)check(value==1.f/2048.f);
 VirtualFree(memory,0,MEM_RELEASE);
 std::printf("%d shadow-step guards/ABI checks, %d failures\n",checks,failures);return failures?1:0;
}
