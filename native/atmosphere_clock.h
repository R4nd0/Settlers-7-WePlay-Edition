// SPDX-License-Identifier: MIT
// WePlay Community. Read-only, bounded view of the actual sun timeline.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>

namespace atmosphere {
struct Profile {
    uint32_t image_size, environment_global, timeline_global, environment_vtable, timeline_vtable;
};
constexpr Profile Ubisoft{0x10e7000,0x1379b5c,0x1379b00,0x1138b24,0x1138b08};
// Independently checked against the Steam registration, blend function and RTTI.
constexpr Profile Steam{0x1106000,0x1376c4c,0x1376bf0,0x1137044,0x1137030};
struct Clock { float azimuth=0, milliseconds=0; };
enum class Status { ready, unavailable, wrong_environment, invalid_layout, invalid_timeline };

template<class Reader> Status Read(Reader& memory,uint32_t base,const Profile& profile,Clock& output,bool lab=false) {
    output={};
    if(base<0x10000 || base>UINT32_MAX-profile.image_size)return Status::invalid_layout;
    const auto address=[&](uint32_t va){return base+va-0x400000u;};
    const auto read=[&](uint32_t at,auto& value){return at>=0x10000 && at<=UINT32_MAX-sizeof(value) && memory.read(at,&value,sizeof(value));};
    uint32_t manager=0,begin=0,end=0,active=0;uint8_t slot=0;
    if(!read(address(profile.environment_global),manager) || manager<0x10000 || manager>UINT32_MAX-0x60
       ||!read(manager+0x40,begin)||!read(manager+0x44,end)||!read(manager+0x5c,slot))return Status::unavailable;
    if(slot>1 || begin<0x10000 || end<begin || (end-begin)%0x470 || (end-begin)/0x470>256
       ||!read(manager+0x54+slot*4,active)||!active||active>(end-begin)/0x470)return Status::invalid_layout;
    const auto environment=begin+(active-1)*0x470;
    uint32_t vtable=0,name=0,length=0,timeline_id=0;double period=0;
    if(!read(environment,vtable)||vtable!=address(profile.environment_vtable)
       ||!read(environment+8,name)||!read(environment+0x10,length))return Status::invalid_layout;
    constexpr char ExpectedName[]="WePlayMidsummer";char actual[sizeof(ExpectedName)]{};
    if(length!=sizeof(ExpectedName)-1 || name<0x10000 || name>UINT32_MAX-sizeof(actual)
       ||!memory.read(name,actual,sizeof(actual))||std::memcmp(actual,ExpectedName,sizeof(actual)))return Status::wrong_environment;
    if(!read(environment+0x18,period)||!std::isfinite(period)
       ||(period!=1200000. && !(lab && period==12000.)))return Status::invalid_timeline;
    uint32_t timelines=0,array=0,array_end=0,timeline=0;
    if(!read(address(profile.timeline_global),timelines)||timelines<0x10000||timelines>UINT32_MAX-0x7c
       ||!read(timelines+0x74,array)||!read(timelines+0x78,array_end)
       ||!read(environment+0x28,timeline_id))return Status::invalid_layout;
    if(array<0x10000 || array_end<array || (array_end-array)%4 || (array_end-array)/4>65536
       ||!timeline_id||timeline_id>(array_end-array)/4
       ||!read(array+(timeline_id-1)*4,timeline)||timeline<0x10000||timeline>UINT32_MAX-0x48)return Status::invalid_layout;
    double duration=0,start=0,current=0;float azimuth=0;uint32_t keys=0,keys_end=0;
    if(!read(timeline,vtable)||vtable!=address(profile.timeline_vtable)
       ||!read(timeline+8,duration)||!read(timeline+0x18,start)||!read(timeline+0x20,current)
       ||!read(timeline+0x34,azimuth)||!read(timeline+0x40,keys)||!read(timeline+0x44,keys_end))return Status::invalid_timeline;
    if(duration!=period || !std::isfinite(start)||!std::isfinite(current)||!std::isfinite(azimuth)
       ||current<start||current-start>1e12||azimuth<0||azimuth>360
       ||keys<0x10000||keys>UINT32_MAX-120||keys_end!=keys+120)return Status::invalid_timeline;
    for(uint32_t i=0;i<5;++i) {
        float angle=0;double time=0;
        if(!read(keys+i*24,angle)||!read(keys+i*24+8,time)||angle!=i*90.f||time!=i*period/4.)return Status::invalid_timeline;
    }
    // Keep refraction time precise in long games. The wrap is in the clear
    // twilight interval, where the heat weight is zero.
    output.azimuth=azimuth;output.milliseconds=static_cast<float>(std::fmod(current-start,period));
    return Status::ready;
}
} // namespace atmosphere
