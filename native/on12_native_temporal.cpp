#ifndef S7_NATIVE_TEMPORAL_CPU_FIXTURE
#include "pch.h"
#endif
#include "on12_native_temporal.h"
#include "water_on12_identity.h"
#include <array>
#include <mutex>
#include <cmath>

namespace On12NativeTemporal {
namespace {std::array<State*,16> states{};std::mutex mutex;}
State::~State(){std::lock_guard<std::mutex> lock(mutex);enabled_=false;for(auto& state:states)if(state==this)state=nullptr;}
bool State::Register(void* key,void* cookie,void(*dirty)(void*)){
    if(!key||!cookie||!dirty||key_)return false;
    std::lock_guard<std::mutex> lock(mutex);for(auto& state:states)if(!state){key_=reinterpret_cast<std::uintptr_t>(key);cookie_=cookie;dirty_=dirty;state=this;return true;}return false;
}
void State::ObserveOwner(){const DWORD thread=GetCurrentThreadId();if(last_owner_.load(std::memory_order_relaxed)!=thread)last_owner_.store(thread,std::memory_order_relaxed);}
bool State::Set(const Control& c){
    if(c.size!=sizeof(c)||c.version!=2||c.device12!=key_||!c.sequence||c.sequence<=control_.sequence||c.flags>1
        ||GetCurrentThreadId()!=last_owner_.load(std::memory_order_relaxed)||!std::isfinite(c.jitter_x)||!std::isfinite(c.jitter_y))return false;
    if(c.flags){if(!c.width||!c.height||c.width>4096||c.height>4096||std::fabs(c.jitter_x)>0.5f||std::fabs(c.jitter_y)>0.5f)return false;}
    else if(c.width||c.height||c.jitter_x!=0||c.jitter_y!=0)return false;
    enabled_.store(false,std::memory_order_release);configured_owner_.store(GetCurrentThreadId(),std::memory_order_relaxed);
    control_=c;width_.store(c.width,std::memory_order_relaxed);height_.store(c.height,std::memory_order_relaxed);
    jitter_x_.store(c.jitter_x,std::memory_order_relaxed);jitter_y_.store(c.jitter_y,std::memory_order_relaxed);
    enabled_.store(c.flags!=0,std::memory_order_release);++configurations_;dirty_(cookie_);return true;
}
bool State::Adjust(bool scene,float width,float height,float& x,float& y){
    resolves_.fetch_add(1,std::memory_order_relaxed);resolver_thread_.store(GetCurrentThreadId(),std::memory_order_relaxed);metadata_scene_.store(scene?1u:0u,std::memory_order_relaxed);
    if(!enabled_.load(std::memory_order_acquire)||configured_owner_.load(std::memory_order_relaxed)!=GetCurrentThreadId())return false;
    eligible_.fetch_add(1,std::memory_order_relaxed);
    // Version 2 carries an explicit scene scope from the original API layer.
    // Driver-side CTAB recognition is diagnostic only; runtimes may omit it.
    if(width<=0||height<=0||width!=width_.load(std::memory_order_relaxed)||height!=height_.load(std::memory_order_relaxed))return false;
    const float jx=jitter_x_.load(std::memory_order_relaxed),jy=jitter_y_.load(std::memory_order_relaxed);
    if(!enabled_.load(std::memory_order_acquire)||configured_owner_.load(std::memory_order_relaxed)!=GetCurrentThreadId())return false;
    x+=2*jx*x;y+=2*jy*y;shifted_.fetch_add(1,std::memory_order_relaxed);return true;
}
void State::Get(Query& q)const{
    q.owner=last_owner_.load(std::memory_order_relaxed);q.sequence=control_.sequence;q.enabled=enabled_.load(std::memory_order_acquire)?1u:0u;
    q.jitter_x=control_.jitter_x;q.jitter_y=control_.jitter_y;q.width=control_.width;q.height=control_.height;
    q.eligible_resolves=eligible_.load(std::memory_order_relaxed);q.shifted_resolves=shifted_.load(std::memory_order_relaxed);q.configurations=configurations_;
    q.resolves=resolves_.load(std::memory_order_relaxed);q.resolver_thread=resolver_thread_.load(std::memory_order_relaxed);q.metadata_scene=metadata_scene_.load(std::memory_order_relaxed);q.control_owner=configured_owner_.load(std::memory_order_relaxed);q.supported=1;
}
bool Configure(const Control& c){
    std::lock_guard<std::mutex> lock(mutex);State* candidate=nullptr;
    for(auto* state:states)if(state&&state->key_==c.device12&&state->last_owner_.load(std::memory_order_relaxed)==GetCurrentThreadId()){
        if(candidate)return false;candidate=state;
    }return candidate&&candidate->Set(c);
}
bool Snapshot(Query& q){
    if(q.size!=sizeof(q)||q.version!=2)return false;
    std::lock_guard<std::mutex> lock(mutex);State* candidate=nullptr;
    for(auto* state:states)if(state&&state->key_==q.device12&&state->last_owner_.load(std::memory_order_relaxed)==GetCurrentThreadId()){
        if(candidate)return false;candidate=state;
    }if(!candidate)return false;candidate->Get(q);return true;
}
}
extern "C" DWORD WINAPI WePlayOn12TemporalControl(void* opaque){
    WaterOn12Identity::Guard guard;
    On12NativeTemporal::Control c;SIZE_T count=0;
    return opaque&&ReadProcessMemory(GetCurrentProcess(),opaque,&c,sizeof(c),&count)&&count==sizeof(c)&&On12NativeTemporal::Configure(c)?1u:0u;
}
extern "C" DWORD WINAPI WePlayOn12TemporalQuery(void* opaque){
    WaterOn12Identity::Guard guard;
    On12NativeTemporal::Query q;SIZE_T count=0;
    if(!opaque||!ReadProcessMemory(GetCurrentProcess(),opaque,&q,sizeof(q),&count)||count!=sizeof(q)||!On12NativeTemporal::Snapshot(q))return 0;
    return WriteProcessMemory(GetCurrentProcess(),opaque,&q,sizeof(q),&count)&&count==sizeof(q)?1u:0u;
}
