#define S7_GOLD_RESUME_POSTLOAD_TESTING
#define S7_GOLD_LOGISTICS_BINDING_TESTING
#include "crossplay_gold_resume_postload.h"
#include <cstdio>
namespace {
unsigned checks=0;void check(bool x){++checks;if(!x){printf("FAIL postload preservation check %u\n",checks);ExitProcess(1);}}
uint32_t logic[100]{},main_data[4]{},priority_data[1500]{},transfers[110]{},registry[32]{},task[32]{},global_logic=0,frame[16]{};
bool allow=true;unsigned failures=0;const char* last_error=nullptr;bool Authority(){return allow;}void Report(bool ok,const char* reason){if(!ok){++failures;last_error=reason;}}
void Setup(){
 using namespace CrossplayGoldResume;
 memset(saved.data(),0,sizeof(saved));memset(task,0,sizeof(task));memset(registry,0,sizeof(registry));memset(frame,0,sizeof(frame));invalid=attempted=validated=false;installed=true;owner=GetCurrentThreadId();saved_count=1;checkpoint_turn=300;
 allow_history_guest_reset=false;original_request_resets.fill(0);verified_original_resets=0;
 global_logic=reinterpret_cast<uintptr_t>(logic);logic[0]=0xf317e0;logic[0x14c/4]=reinterpret_cast<uintptr_t>(main_data);
 main_data[0]=0xf4bb0c;main_data[1]=reinterpret_cast<uintptr_t>(priority_data);main_data[2]=reinterpret_cast<uintptr_t>(transfers);priority_data[0]=0xf4beb4;
 priority_data[0x164c/4]=reinterpret_cast<uintptr_t>(registry);priority_data[0x1650/4]=reinterpret_cast<uintptr_t>(registry+30);
 GoldLogisticsBinding::logic_global=reinterpret_cast<uintptr_t>(&global_logic);
 transfers[0x1a8/4]=reinterpret_cast<uintptr_t>(transfers)+0x194;
 task[0]=0xf4c684;task[1]=1;task[2]=65548;task[4]=UINT32_MAX;task[6]=29;task[11]=26;task[12]=299;task[13]=1;
 registry[24]=task[2];registry[25]=reinterpret_cast<uintptr_t>(task);check(Read(reinterpret_cast<uintptr_t>(task),saved[12]));
 frame[6]=reinterpret_cast<uintptr_t>(task);frame[9]=0x902dce;
 CrossplayGoldResume::installed=true;CrossplayGoldResume::owner=GetCurrentThreadId();CrossplayGoldResume::authority=&Authority;CrossplayGoldResume::report=&Report;
 CrossplayGoldResume::first_pass=CrossplayGoldResume::second_pass=0;CrossplayGoldResume::test_decide=nullptr;allow=true;failures=0;last_error=nullptr;
}
alignas(16) unsigned char original_fp[512]{},before_fp[512]{},after_fp[512]{};
uint32_t vt[8]{},object=0,entry_esp=0,after_esp=0,entry_flags=0,after_flags=0,result=0,expected_return=0,kept[6]{};
bool decision=false,recorded=false;uintptr_t bridge=0;
bool Decide(const uint32_t* p){
 recorded=p[0]==0x77777777&&p[1]==0x66666666&&p[4]==0x33333333&&p[5]==0x22222222&&p[6]==reinterpret_cast<uintptr_t>(&object)&&p[7]==0x11111111
  &&p[3]==reinterpret_cast<uintptr_t>(p)+32&&p[9]==expected_return&&p[10]==0xaaaa0001&&p[11]==0xbbbb0002&&p[12]==0xcccc0003&&p[13]==0xdddd0004&&p[14]==0xeeee0005;
 SetLastError(0xdeadbeef);__asm{
  fninit
  fldpi
  pxor xmm0,xmm0
  pxor xmm1,xmm1
  pxor xmm7,xmm7
 }
 return decision;
}
__declspec(naked) void Continue(){__asm{
 mov eax,0xfaceb00c
 ret
}}
unsigned test_df=0;
__declspec(naked) void RunBridge(){__asm{
   push ebp
   mov ebp,esp
   push ebx
   push esi
   push edi
   fxsave [original_fp]
   fninit
   fld1
   pcmpeqd xmm0,xmm0
   pcmpeqd xmm1,xmm1
   pcmpeqd xmm7,xmm7
   fxsave [before_fp]
   mov [entry_esp],esp
   mov eax,offset returned
   mov [expected_return],eax
   cmp test_df,0
   je clear_df
   std
   jmp df_ready
clear_df:
   cld
df_ready:
   xor eax,eax
   cmp eax,eax
   pushfd
   pop dword ptr [entry_flags]
   push 0xeeee0005
   push 0xdddd0004
   push 0xcccc0003
   push 0xbbbb0002
   push 0xaaaa0001
   mov eax,0x11111111
   lea ecx,[object]
   mov edx,0x22222222
   mov ebx,0x33333333
   mov esi,0x66666666
   mov edi,0x77777777
   call dword ptr [bridge]
returned:
   pushfd
   pop dword ptr [after_flags]
   mov [kept],ebx
   mov [kept+4],esi
   mov [kept+8],edi
   mov [kept+12],ecx
   mov [kept+16],edx
   mov [kept+20],eax
   mov [result],eax
   add esp,20
   mov [after_esp],esp
   fxsave [after_fp]
   fxrstor [original_fp]
   cld
   pop edi
   pop esi
   pop ebx
   mov esp,ebp
   pop ebp
   ret
}}

}
extern "C" __declspec(dllexport) uint32_t __cdecl CrossplayGoldResumeGuardOracle(uint32_t e){return e==1&&CrossplayGoldResume::Methods()?1u:0u;}
int main(){
 for(auto pc:{0x902dceu,0x902ea6u})for(uint32_t state:{0u,1u,2u,3u,4u,5u,6u,7u,UINT32_MAX}){
  Setup();task[1]=CrossplayGoldResume::saved[12].state=state;frame[9]=pc;uint32_t before[32]{};memcpy(before,task,sizeof(task));check(CrossplayGoldResume::Preserve(frame));check(memcmp(before,task,sizeof(task))==0&&failures==0);
  check(CrossplayGoldResume::first_pass+CrossplayGoldResume::second_pass==1);
 }
 for(unsigned offset:{1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u}){
  Setup();task[offset]^=1;uint32_t before[32]{};memcpy(before,task,sizeof(task));check(!CrossplayGoldResume::Preserve(frame));check(CrossplayGoldResume::invalid&&failures==1&&memcmp(before,task,sizeof(task))==0);
 }
 Setup();registry[24]^=65536;check(!CrossplayGoldResume::Preserve(frame));check(failures==1);
 Setup();registry[25]+=4;check(!CrossplayGoldResume::Preserve(frame));check(failures==1);
 Setup();allow=false;check(!CrossplayGoldResume::Preserve(frame));check(failures==1);check(!strcmp(last_error,"saved-request-postload-authority-excluded"));
 Setup();CrossplayGoldResume::saved[12]={};check(!CrossplayGoldResume::Preserve(frame));check(failures==1);check(!strcmp(last_error,"saved-request-postload-source-missing"));
 Setup();CrossplayGoldResume::invalid=true;check(!CrossplayGoldResume::Preserve(frame));check(failures==1);
 Setup();task[1]=2;check(!CrossplayGoldResume::Preserve(frame));check(failures==1);check(!strcmp(last_error,"saved-request-postload-state-changed"));
 Setup();task[4]^=1;check(!CrossplayGoldResume::Preserve(frame));check(!strcmp(last_error,"saved-request-postload-cache-changed"));
 Setup();registry[25]+=4;check(!CrossplayGoldResume::Preserve(frame));check(!strcmp(last_error,"saved-request-postload-registry-excluded"));
 // Fresh/ordinary callbacks, later ticks, other classes and wrong owners follow
 // the untouched original function, without gaining saved-world permissions.
 Setup();frame[9]=0x949dfa;check(!CrossplayGoldResume::Preserve(frame));check(!failures&&!CrossplayGoldResume::invalid);
 Setup();CrossplayGoldResume::attempted=true;check(!CrossplayGoldResume::Preserve(frame));check(!failures);
 Setup();++CrossplayGoldResume::owner;check(!CrossplayGoldResume::Preserve(frame));check(!failures);
 Setup();task[0]=0xf4c74c;check(!CrossplayGoldResume::Preserve(frame));check(!failures);
 Setup();CrossplayGoldResume::installed=false;check(!CrossplayGoldResume::Preserve(frame));check(!failures);
 Setup();check(CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 Setup();CrossplayGoldResume::saved_count=0;CrossplayGoldResume::saved[12]={};
 check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 registry[24]=registry[25]=0;
 check(CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 priority_data[0x164c/4]=0;
 check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 priority_data[0x1650/4]=0;
 check(CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 CrossplayGoldResume::saved_count=1;
 check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 Setup();task[1]=CrossplayGoldResume::saved[12].state=6;task[6]=CrossplayGoldResume::saved[12].wait=13;
 CrossplayGoldResume::ObserveOriginalReset(reinterpret_cast<uintptr_t>(task));
 task[1]=1;task[6]=0;
 check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 CrossplayGoldResume::allow_history_guest_reset=true;
 check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 task[1]=6;task[6]=13;
 CrossplayGoldResume::ObserveOriginalReset(reinterpret_cast<uintptr_t>(task));
 check(task[1]==6&&task[6]==13);
 task[1]=1;task[6]=0;
 check(CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 check(CrossplayGoldResume::verified_original_resets==1&&task[1]==1&&!task[6]);
 task[5]^=1;
 check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 Setup();task[6]++;check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)));
 Setup();check(!CrossplayGoldResume::Validate(reinterpret_cast<uintptr_t>(main_data)+4));
 Setup();frame[10]=reinterpret_cast<uintptr_t>(main_data);uint32_t unchanged[32]{};memcpy(unchanged,task,sizeof(task));
 CrossplayGoldResume::BeforeScheduler(frame);check(CrossplayGoldResume::attempted&&CrossplayGoldResume::validated&&!failures&&memcmp(unchanged,task,sizeof(task))==0);
 Setup();frame[10]=reinterpret_cast<uintptr_t>(main_data);task[6]++;CrossplayGoldResume::BeforeScheduler(frame);check(CrossplayGoldResume::invalid&&!CrossplayGoldResume::validated&&failures==1);
 // Hook lifetime tests use only owned scratch bytes, never a game image.
 unsigned char code[3][6]{};
 const auto prepare_hooks=[&](){Setup();for(unsigned i=0;i<3;++i){auto& hook=CrossplayGoldResume::hooks[i];hook.at=reinterpret_cast<uintptr_t>(code[i]);hook.size=6;
   memset(hook.before,0x70+i,6);memset(hook.after,0x90+i,6);memcpy(code[i],hook.after,6);}};
 prepare_hooks();check(CrossplayGoldResume::OwnsHooks());++CrossplayGoldResume::owner;check(!CrossplayGoldResume::Release());
 prepare_hooks();code[1][2]^=1;unsigned char kept_code[3][6]{};memcpy(kept_code,code,sizeof(code));
 check(!CrossplayGoldResume::OwnsHooks()&&!CrossplayGoldResume::Release()&&memcmp(kept_code,code,sizeof(code))==0);
 prepare_hooks();check(CrossplayGoldResume::Release());
 check(!CrossplayGoldResume::installed&&!CrossplayGoldResume::saved_count&&!CrossplayGoldResume::owner&&!CrossplayGoldResume::authority);
 for(unsigned i=0;i<3;++i)for(unsigned j=0;j<6;++j)check(code[i][j]==0x70+i);
 check(!CrossplayGoldResume::Release());
 printf("PASS %u authenticated saved request postload policy checks; both exact callers, state-independent original History class policy, all source fields/registry/generation/authority/phase/class exclusions, zero task writes. Synthetic world.\n",checks);
 object=reinterpret_cast<uintptr_t>(vt);vt[7]=0x11223344;
 CrossplayGoldResume::test_decide=&Decide;CrossplayGoldResume::predicate_resume=reinterpret_cast<uintptr_t>(&Continue);bridge=reinterpret_cast<uintptr_t>(&CrossplayGoldResume::Predicate);
 for(unsigned route=0;route<2;++route)for(unsigned df=0;df<2;++df){decision=route!=0;recorded=false;SetLastError(0xabcdef12);
  test_df=df;RunBridge();
  check(recorded&&entry_esp==after_esp&&GetLastError()==0xabcdef12&&memcmp(before_fp,after_fp,288)==0);
  check(kept[0]==0x33333333&&kept[1]==0x66666666&&kept[2]==0x77777777&&kept[3]==reinterpret_cast<uintptr_t>(&object));
  check(route?(result==0x11111100&&kept[4]==0x22222222): (result==0xfaceb00c&&kept[4]==0x11223344));
  check(((entry_flags^after_flags)&~0x10u)==0);
 }
 puts("PASS compiled postload predicate bridge both routes/DF0+1: untouched original continuation or original History AL=false semantics, this/five arguments/stack/nonvolatile/volatile registers/flags/LastError/x87/SSE preserved as specified. No game started.");
}
