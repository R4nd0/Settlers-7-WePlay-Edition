#pragma once
#include "crossplay_history_preparation.h"

// Rebind the admitted History participant to its own preparation row through
// original methods. This supplies neither Ready nor permission to start.
namespace CrossplayNative {
namespace HistoryClaimDetail {
struct Pin {
    uintptr_t manager_pointer=0,manager_type=0,castle_type=0;
    uintptr_t prepare=0,claim=0,set_id=0,set_name=0;
};
inline bool Layout(Crossplay::Edition edition,Pin& pin){
    pin={};
    if(edition==Crossplay::Edition::UbisoftHistory)
        pin={0x13a5218,0x113ae54,0x11372d0,0x5869a0,0x586a10,0x546440,0x546450};
    else if(edition==Crossplay::Edition::SteamHistory)
        pin={0x13a2300,0x113939c,0x11357f8,0x586460,0x5864d0,0x545a90,0x545aa0};
    else return false;
    return true;
}
inline bool Methods(Crossplay::Edition edition){
    Pin pin;if(!Layout(edition,pin))return false;
    const unsigned char prepare[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x8b,0x49,0x04,0x69,0xd0,0xa8,0x05,0x00,0x00,0xc7,0x44,0x0a,0x60,0x02,0x00,0x00,0x00,0x5d,0xc2,0x04,0x00};
    const unsigned char claim[]={0x55,0x8b,0xec,0x0f,0xb6,0x45,0x08,0x69,0xd0,0xa8,0x05,0x00,0x00,0x8b,0x41,0x04,0x03,0xc2,0x83,0x78,0x64,0x01};
    const unsigned char id[]={0x55,0x8b,0xec,0x8b,0x45,0x08,0x89,0x41,0x18,0x5d,0xc2,0x04,0x00};
    const unsigned char name[]={0x55,0x8b,0xec,0x51,0x8b,0x45,0x08,0x83,0xc1,0x1c,0xff,0x70,0x08,0xff,0x30,0xe8};
    const struct Region {uintptr_t address;const unsigned char* bytes;size_t size;} regions[]={
        {pin.prepare,prepare,sizeof(prepare)},{pin.claim,claim,sizeof(claim)},
        {pin.set_id,id,sizeof(id)},{pin.set_name,name,sizeof(name)}};
    for(const auto& region:regions)
        if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(region.address),region.size)
            || memcmp(reinterpret_cast<void*>(region.address),region.bytes,region.size))return false;
    return true;
}
inline bool ParticipantForClaim(const LobbySnapshot& snapshot,uint32_t own_connection,uint32_t source_principal,
                                Participant& result){
    result={};
    if(!snapshot.instance || snapshot.host || snapshot.local_slot!=2 || snapshot.members.size()!=2 || !own_connection)return false;
    const Participant* local=nullptr;const Participant* host=nullptr;
    for(const auto& member:snapshot.members){
        const auto& p=member.player;
        if(member.status!=2 || !p.connection || p.connection==UINT32_MAX || p.principal==UINT32_MAX
            || p.name.empty() || p.name.size()>128 || p.name.find('\0')!=std::string::npos)return false;
        if(p.local && !p.host && p.slot==2 && p.connection==own_connection && p.principal==source_principal)local=&p;
        else if(!p.local && p.host && p.slot==1 && p.connection!=own_connection)host=&p;
        else return false;
    }
    if(!local || !host || (local->principal?local->principal:2)==(host->principal?host->principal:1))return false;
    result=*local;return true;
}
struct Interval {uintptr_t session=0,manager=0,configuration=0,rows=0;uint8_t mode=0;};
inline bool Same(const Interval& a,const Interval& b){
    return a.session && a.session==b.session && a.manager && a.manager==b.manager
        && a.configuration && a.configuration==b.configuration && a.rows && a.rows==b.rows && a.mode==b.mode;
}
inline bool Capture(const Pin& pin,const LobbySnapshot& snapshot,Interval& output){
    using CrossplayNativeCodec::Detail::Readable;output={};
    if(!Readable(reinterpret_cast<void*>(pin.manager_pointer),4))return false;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(pin.manager_pointer);
    if(!Readable(manager,0x190) || *reinterpret_cast<const uintptr_t*>(manager)!=pin.manager_type || manager[4]>1)return false;
    const auto mode=manager[4];
    auto* configuration=*reinterpret_cast<unsigned char* const*>(manager+((mode-1)&1)*0xc4+0x98);
    if(!Readable(configuration,8))return false;
    auto* rows=*reinterpret_cast<unsigned char* const*>(configuration+4);
    if(!Readable(rows,0x8dc0) || *reinterpret_cast<const uintptr_t*>(configuration)!=reinterpret_cast<uintptr_t>(rows)
        || rows[0x8db8]!=4)return false;
    output={snapshot.instance,reinterpret_cast<uintptr_t>(manager),reinterpret_cast<uintptr_t>(configuration),reinterpret_cast<uintptr_t>(rows),mode};
    return true;
}
}
class HistoryGuestClaim {
    DWORD owner=0;
    Crossplay::Edition edition{};
    HistoryClaimDetail::Interval interval;
    CrossplayCastleSetup::Value expected_castle;
    bool queued=false;
public:
    bool Queued()const{return queued;}
    bool Begin(Crossplay::Edition value,ParticipantEvent& binding,LobbyAdmission& admission,
               uint32_t source_principal,const CrossplayCastleSetup::Value& source_castle,uint32_t extra){
        using CrossplayNativeCodec::Detail::Readable;
        using namespace HistoryClaimDetail;
        Pin pin;LobbySnapshot snapshot;Participant own;
        if(queued || owner || !Layout(value,pin) || admission.GetState()!=AdmissionState::RosterBound || !admission.Poll()
            || !binding.CaptureLobby(snapshot) || !ParticipantForClaim(snapshot,admission.OwnConnection(),source_principal,own)
            || !Methods(value))return false;
        // Bind a fresh original event factory before the first original mutation.
        ParticipantEvent confirmed;
        if(!confirmed.Bind(value) || !Capture(pin,snapshot,interval))return false;
        auto* configuration=reinterpret_cast<unsigned char*>(interval.configuration);
        auto* rows=reinterpret_cast<unsigned char*>(interval.rows);
        const auto* first=rows+0x5a8;auto* second=rows+2*0x5a8;
        if(*reinterpret_cast<const uint32_t*>(first+0x64)!=1 || *reinterpret_cast<const uint32_t*>(first+0x60)!=3
            || *reinterpret_cast<const uint32_t*>(first+0x5c)!=1 || *reinterpret_cast<const uint32_t*>(second+0x64)!=1
            || *reinterpret_cast<const uint32_t*>(second+0x60)!=1 || *reinterpret_cast<const uint32_t*>(second+0x55c)!=pin.castle_type)return false;
        expected_castle=source_castle;owner=GetCurrentThreadId();edition=value;
        reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(pin.prepare)(configuration,2);
        if(*reinterpret_cast<const uint32_t*>(second+0x60)!=2
            || !reinterpret_cast<bool (__thiscall*)(void*,uint32_t,uint32_t)>(pin.claim)(configuration,2,2))return false;
        const uint32_t id=own.principal?own.principal:own.slot;
        reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(pin.set_id)(second,id);
        struct String {const char* data;uint32_t capacity,length,extra;};
        const String name{own.name.c_str(),static_cast<uint32_t>(own.name.size()+1),static_cast<uint32_t>(own.name.size()),0};
        reinterpret_cast<void (__thiscall*)(void*,const String*)>(pin.set_name)(second,&name);
        const auto* copied=reinterpret_cast<const String*>(second+0x1c);
        if(*reinterpret_cast<const uint32_t*>(second+0x18)!=id || copied->data==name.data || copied->length!=name.length
            || copied->capacity<name.length+1 || !Readable(copied->data,name.length+1) || memcmp(copied->data,name.data,name.length+1))return false;
        LobbySnapshot after;Participant observed;Interval current;
        if(!binding.CaptureLobby(after) || !ParticipantForClaim(after,admission.OwnConnection(),source_principal,observed)
            || observed.principal!=own.principal || observed.name!=own.name || !Capture(pin,after,current) || !Same(interval,current)
            || !confirmed.ConstructConfirmedSetup(own,source_castle,extra) || !confirmed.EnqueueLocal() || !confirmed.Reset())return false;
        queued=true;return true;
    }
    bool Applied(ParticipantEvent& binding,LobbyAdmission& admission)const{
        HistoryClaimDetail::Pin pin;LobbySnapshot snapshot;HistoryClaimDetail::Interval current;
        if(!queued || owner!=GetCurrentThreadId() || !HistoryClaimDetail::Layout(edition,pin)
            || !binding.CaptureLobby(snapshot) || !HistoryClaimDetail::Capture(pin,snapshot,current) || !HistoryClaimDetail::Same(interval,current)
            || !HistoryGuestPresentationReady(edition,binding,admission))return false;
        CrossplayCastleSetup::Value actual;std::vector<unsigned char> expected_bytes,actual_bytes;
        return binding.CaptureCastleSetup(reinterpret_cast<const unsigned char*>(interval.rows)+2*0x5a8+0x55c,actual)
            && CrossplayCastleSetup::Encode(expected_castle,expected_bytes) && CrossplayCastleSetup::Encode(actual,actual_bytes)
            && actual_bytes==expected_bytes;
    }
};
}
