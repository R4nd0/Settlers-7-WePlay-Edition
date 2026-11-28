#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>

namespace On12NativeTemporal {
struct Control {
    // flags=1 is an explicit, verified scene scope supplied by the original
    // API-level shader observer. The frontend must disable it for UI/post FX.
    DWORD size=40,version=2;std::uint64_t device12=0;
    DWORD width=0,height=0;float jitter_x=0,jitter_y=0;DWORD sequence=0,flags=0;
};
struct Query {
    DWORD size=96,version=2;std::uint64_t device12=0;
    DWORD owner=0,sequence=0,enabled=0,supported=1;
    float jitter_x=0,jitter_y=0;DWORD width=0,height=0;
    std::uint64_t eligible_resolves=0,shifted_resolves=0,configurations=0;
    std::uint64_t resolves=0;DWORD resolver_thread=0,metadata_scene=0,control_owner=0,reserved=0;
};
static_assert(sizeof(Control)==40&&sizeof(Query)==96,"Native temporal ABI");
class State {
public:
    ~State();
    bool Register(void* device12,void* cookie,void(*dirty)(void*));
    void ObserveOwner();
    bool Enabled()const{return enabled_.load(std::memory_order_acquire);}
    bool Adjust(bool scene,float width,float height,float& x_scale,float& y_scale);
private:
    friend bool Configure(const Control&);
    friend bool Snapshot(Query&);
    bool Set(const Control&);
    void Get(Query&)const;
    std::uint64_t key_=0;
    void* cookie_=nullptr;void(*dirty_)(void*)=nullptr;
    std::atomic<DWORD> last_owner_{0},configured_owner_{0};
    std::atomic_bool enabled_{false};
    std::atomic<DWORD> width_{0},height_{0};
    std::atomic<float> jitter_x_{0},jitter_y_{0};
    Control control_{};
    std::atomic<std::uint64_t> eligible_{0},shifted_{0};std::uint64_t configurations_=0;
    std::atomic<std::uint64_t> resolves_{0};std::atomic<DWORD> resolver_thread_{0},metadata_scene_{0};
};
bool Configure(const Control&);
bool Snapshot(Query&);
}
