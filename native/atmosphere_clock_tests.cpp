#include "atmosphere_clock.h"
#include "atmosphere_fp.h"
#include <map>
#include <vector>
#include <cstdio>
#include <cstdlib>
struct Memory {
    std::map<uint32_t,uint8_t> bytes;
    bool read(uint32_t address,void* output,size_t size) {
        auto* out=static_cast<uint8_t*>(output);
        for(size_t i=0;i<size;++i){auto at=bytes.find(address+static_cast<uint32_t>(i));if(at==bytes.end())return false;out[i]=at->second;}
        return true;
    }
    template<class T> void put(uint32_t address,T value) {const auto* src=reinterpret_cast<const uint8_t*>(&value);for(size_t i=0;i<sizeof(value);++i)bytes[address+static_cast<uint32_t>(i)]=src[i];}
};
int checks=0;
void check(bool okay){++checks;if(!okay){std::fprintf(stderr,"Clock check failed: %d\n",checks);std::exit(1);}}
Memory fixture(const atmosphere::Profile& p,uint32_t base,double period=1200000.) {
    Memory m;auto at=[&](uint32_t va){return base+va-0x400000;};
    const uint32_t shift=p.vector_shift;
    m.put(at(p.environment_global),0x20000u);m.put(0x20040u+shift,0x30000u);m.put(0x20044u+shift,0x30470u);m.put(0x2005cu+shift,uint8_t(0));m.put(0x20054u+shift,1u);
    m.put(0x30000u,at(p.environment_vtable));m.put(0x30008u,0x40000u);m.put(0x30010u,15u);m.put(0x30018u,period);m.put(0x30028u,1u);
    const char name[]="WePlayMidsummer";for(uint32_t i=0;i<sizeof(name);++i)m.put(0x40000u+i,name[i]);
    m.put(at(p.timeline_global),0x50000u);m.put(0x50074u+shift,0x60000u);m.put(0x50078u+shift,0x60004u);m.put(0x60000u,0x70000u);
    m.put(0x70000u,at(p.timeline_vtable));m.put(0x70008u,period);m.put(0x70018u,0.);m.put(0x70020u,6100000.);m.put(0x70034u,30.f);
    m.put(0x70040u+shift,0x80000u);m.put(0x70044u+shift,0x80078u);
    for(uint32_t i=0;i<5;++i){m.put(0x80000u+i*24,i*90.f);m.put(0x80008u+i*24,i*period/4.);}
    return m;
}
int main(){
    {
        atmosphere::FpScope original;
        for(int rounding:{FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO}){
            check(fesetround(rounding)==0);
            feclearexcept(FE_ALL_EXCEPT);
            const auto csr=_mm_getcsr();
            {
                atmosphere::FpScope callback;check(callback.valid);
                fesetround(rounding==FE_DOWNWARD?FE_UPWARD:FE_DOWNWARD);
                feraiseexcept(FE_INEXACT);
                _mm_setcsr(_mm_getcsr() ^ 0x8040u); // flush/denorm mode too
            }
            check(fegetround()==rounding);
            check(fetestexcept(FE_ALL_EXCEPT)==0);
            check(_mm_getcsr()==csr);
        }
    }
    for(auto p:{atmosphere::Ubisoft,atmosphere::Steam,atmosphere::Gold})for(uint32_t base:{0x400000u,0x14000000u}){
        const uint32_t shift=p.vector_shift;
        auto m=fixture(p,base);atmosphere::Clock c;
        check(atmosphere::Read(m,base,p,c)==atmosphere::Status::ready);check(c.azimuth==30.f&&c.milliseconds==100000.f);
        // Actual sun angle, not process uptime: morning/noon/evening and pause.
        for(float angle:{0.f,30.f,90.f,180.f,270.f,360.f}){m.put(0x70034u,angle);check(atmosphere::Read(m,base,p,c)==atmosphere::Status::ready&&c.azimuth==angle);}
        auto before=c;check(atmosphere::Read(m,base,p,c)==atmosphere::Status::ready&&c.azimuth==before.azimuth&&c.milliseconds==before.milliseconds);
        auto expect=[&](Memory x){c.azimuth=123;check(atmosphere::Read(x,base,p,c)!=atmosphere::Status::ready&&c.azimuth==0);};
        auto x=m;x.put(0x2005cu+shift,uint8_t(2));expect(x);
        x=m;x.put(0x20054u+shift,0u);expect(x);
        x=m;x.put(0x20044u+shift,0x30471u);expect(x);
        x=m;x.put(0x30000u,0x12345678u);expect(x);
        x=m;x.put(0x40000u,'X');expect(x);
        x=m;x.put(0x30010u,16u);expect(x);
        x=m;x.put(0x30028u,2u);expect(x);
        x=m;x.put(0x70000u,0x12345678u);expect(x);
        x=m;x.put(0x70008u,5000.);expect(x);
        x=m;x.put(0x70034u,361.f);expect(x);
        x=m;x.put(0x70034u,0x7fc00000u);expect(x);
        x=m;x.put(0x70020u,0x7ff0000000000000ull);expect(x);
        x=m;x.put(0x70018u,7100000.);expect(x);
        x=m;x.put(0x70044u+shift,0x80060u);expect(x);
        x=m;x.put(0x80018u,89.f);expect(x);
        x=m;x.put(0x80020u,999.);expect(x);
        x=m;x.bytes.erase(0x70037u);expect(x);
        x=m;x.put(base+p.environment_global-0x400000,0xfffffff0u);expect(x);
        x=m;x.put(base+p.timeline_global-0x400000,0xfffffff0u);expect(x);
        x=m;x.put(0x60000u,0xfffffff0u);expect(x);
        x=m;x.put(0x70020u,600000100000.);check(atmosphere::Read(x,base,p,c)==atmosphere::Status::ready&&c.milliseconds==100000.f);
        x=fixture(p,base,12000.);check(atmosphere::Read(x,base,p,c)!=atmosphere::Status::ready);check(atmosphere::Read(x,base,p,c,true)==atmosphere::Status::ready);
        if(shift){auto wrong=p;wrong.vector_shift=0;check(atmosphere::Read(m,base,wrong,c)!=atmosphere::Status::ready);}
    }
    std::printf("PASS: %d bounded atmosphere-clock checks\n",checks);
}
