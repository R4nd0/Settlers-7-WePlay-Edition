#include "standalone_peer_policy.cpp"
#include <cstdio>

namespace {
struct LeaseFixture {
    std::wstring path=L"peer-lease-fixture-"+std::to_wstring(GetCurrentProcessId())+L".ini";
    bool owned=false;
    ~LeaseFixture() { if (owned) DeleteFileW(path.c_str()); }

    bool Write(const std::string& text) {
        Handle file;
        file.value=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,owned?TRUNCATE_EXISTING:CREATE_NEW,
            FILE_ATTRIBUTE_TEMPORARY,nullptr);
        if (file.value==INVALID_HANDLE_VALUE) return false;
        owned=true;
        const auto size=static_cast<DWORD>(text.size());
        DWORD written=0;
        return WriteFile(file.value,text.data(),size,&written,nullptr) && written==size;
    }
};

std::string Lease(const std::string& nonce,uint64_t expires) {
    return "format=weplay-wfp-peers-v1\nnonce="+nonce+"\nparent=1\nparent_created=42\nexpires_tick_ms="
        +std::to_string(expires)+"\nlocal=10.23.0.2\ncoordinator=10.23.0.1\nmode=group\n";
}
}

int main() {
    unsigned checks=0,failures=0;
    const auto check=[&](bool result,const char* name) {
        ++checks;
        if (!result) { ++failures; std::printf("FAIL: %s\n",name); }
    };
    LeaseFixture fixture;
    const std::string nonce(32,'a');
    const auto expires=GetTickCount64()+10000;
    if (!fixture.Write(Lease(nonce,expires))) return 2;
    Scope active;
    std::string text;
    if (Read(fixture.path,text)!=ReadResult::Valid || !Parse(text,active)) return 2;
    {
        Handle exclusive;
        exclusive.value=CreateFileW(fixture.path.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,0,nullptr);
        if (exclusive.value==INVALID_HANDLE_VALUE) return 2;
        check(Read(fixture.path,text)==ReadResult::Busy,"real sharing violation");
        check(Renew(fixture.path,active) && active.expires==expires && active.nonce==nonce,
            "transient sharing violation keeps the existing grant without extending it");
        Scope expired=active;
        expired.expires=0;
        check(!Renew(fixture.path,expired),"sharing violation cannot outlive the existing grant");
    }
    const auto refreshed=GetTickCount64()+11000;
    check(fixture.Write(Lease(nonce,refreshed)) && Renew(fixture.path,active) && active.expires==refreshed,
        "the same launcher can renew after the sharing violation");
    check(fixture.Write(Lease(std::string(32,'b'),refreshed)) && !Renew(fixture.path,active),
        "foreign launcher grant stays rejected");
    check(fixture.Write("invalid\n") && !Renew(fixture.path,active),"malformed grant stays rejected");
    const bool removed=DeleteFileW(fixture.path.c_str())!=FALSE;
    if (removed) fixture.owned=false;
    check(removed && !Renew(fixture.path,active),"removed grant stays rejected");
    std::printf("%s %u Windows peer lease checks\n",failures?"FAIL":"PASS",checks);
    return failures?1:0;
}
