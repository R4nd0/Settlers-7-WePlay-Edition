// Elevated only for scoped WFP management; the game always runs unelevated.
#include "standalone_network_policy.h"
#include "standalone_image.h"
#include "lan_membership.h"
#include <algorithm>
#include <array>
#include <map>
#include <vector>
#include <string>
#include <limits>

namespace {
struct Handle { HANDLE value=nullptr; ~Handle() { if (value && value!=INVALID_HANDLE_VALUE) CloseHandle(value); } };
struct Engine { HANDLE value=nullptr; ~Engine() { if (value) FwpmEngineClose0(value); } };
struct Blob { FWP_BYTE_BLOB* value=nullptr; ~Blob() { if (value) FwpmFreeMemory0(reinterpret_cast<void**>(&value)); } };
struct Scope {
    std::string nonce;
    DWORD parent=0;
    uint64_t created=0,expires=0;
    uint32_t local=0,coordinator=0,peer=0;
    bool group=false;
};
bool Read(const std::wstring& path,std::string& output) {
    Handle file; file.value=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    std::vector<unsigned char> bytes;
    if (!StandaloneImage::ReadLockedFile(file.value,4096,bytes)) return false;
    output.assign(bytes.begin(),bytes.end());
    return true;
}
bool Parse(const std::string& text,Scope& scope) {
    std::map<std::string,std::string> fields;
    size_t at=0;
    while (at<text.size()) {
        const size_t end=text.find('\n',at),equal=text.find('=',at);
        if (end==std::string::npos || equal==std::string::npos || equal>=end) return false;
        for (size_t i=at;i<end;++i) if (text[i]<32 || text[i]>126) return false;
        if (!fields.emplace(text.substr(at,equal-at),text.substr(equal+1,end-equal-1)).second) return false;
        at=end+1;
    }
    const auto get=[&](const char* key)->const std::string& { static const std::string empty; const auto it=fields.find(key); return it==fields.end() ? empty : it->second; };
    uint64_t parent=0;
    if (get("format")!="weplay-wfp-peers-v1" || get("nonce").size()!=32
        || get("nonce").find_first_not_of("0123456789abcdef")!=std::string::npos
        || !lan_membership::Decimal(get("parent"),parent) || !parent || parent>MAXDWORD
        || !lan_membership::Decimal(get("parent_created"),scope.created)
        || !lan_membership::Decimal(get("expires_tick_ms"),scope.expires)
        || !ParsePrivateLanAddress(get("local").c_str(),scope.local)
        || !ParsePrivateLanAddress(get("coordinator").c_str(),scope.coordinator)) return false;
    if (get("mode")=="group" && fields.size()==8) scope.group=true;
    else if (get("mode")=="pair" && fields.size()==9 && ParsePrivateLanAddress(get("peer").c_str(),scope.peer)) scope.group=false;
    else return false;
    scope.nonce=get("nonce"); scope.parent=static_cast<DWORD>(parent);
    const uint64_t now=GetTickCount64();
    return scope.parent!=GetCurrentProcessId() && scope.expires>now && scope.expires-now<=30000;
}
bool SameOwner(HANDLE parent,uint64_t expected_created) {
    FILETIME created{},ended{},kernel{},user{};
    if (!GetProcessTimes(parent,&created,&ended,&kernel,&user)
        || (static_cast<uint64_t>(created.dwHighDateTime)<<32 | created.dwLowDateTime)!=expected_created) return false;
    Handle parent_token,self_token;
    if (!OpenProcessToken(parent,TOKEN_QUERY,&parent_token.value)
        || !OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&self_token.value)) return false;
    std::array<unsigned char,4096> left{},right{}; DWORD done=0;
    if (!GetTokenInformation(parent_token.value,TokenUser,left.data(),static_cast<DWORD>(left.size()),&done)
        || !GetTokenInformation(self_token.value,TokenUser,right.data(),static_cast<DWORD>(right.size()),&done)) return false;
    return EqualSid(reinterpret_cast<const TOKEN_USER*>(left.data())->User.Sid,
                    reinterpret_cast<const TOKEN_USER*>(right.data())->User.Sid)!=FALSE;
}
bool Addresses(const Scope& scope,const std::wstring& folder,std::vector<uint32_t>& addresses) {
    addresses={scope.local,scope.coordinator};
    if (scope.group) {
        std::string peers;
        lan_membership::Snapshot snapshot{};
        if (Read(folder+L"\\lan-peers.ini",peers)
            && lan_membership::Parse(peers.data(),peers.size(),scope.local,scope.coordinator,GetTickCount64(),snapshot)) {
            for (size_t i=0;i<snapshot.count;++i) addresses.push_back(snapshot.peers[i]);
        }
    } else addresses.push_back(scope.peer);
    std::sort(addresses.begin(),addresses.end());
    addresses.erase(std::unique(addresses.begin(),addresses.end()),addresses.end());
    return addresses.size()<=8;
}
DWORD Replace(HANDLE engine,FWP_BYTE_BLOB* app,const std::vector<uint32_t>& addresses,std::vector<UINT64>& ids) {
    DWORD result=FwpmTransactionBegin0(engine,0);
    if (result) return result;
    std::vector<UINT64> next;
    for (UINT64 id:ids) { result=FwpmFilterDeleteById0(engine,id); if (result) break; }
    const GUID* layers[]={&FWPM_LAYER_ALE_AUTH_CONNECT_V4,&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4};
    const UINT16 ports[]={3074,6200,9103,9111};
    for (uint32_t address:addresses) {
        if (result) break;
        for (const auto* layer:layers) for (UINT16 port:ports) {
            if (result) break;
            FWPM_FILTER0 filter{}; FWPM_FILTER_CONDITION0 conditions[4]{};
            filter.displayData.name=const_cast<wchar_t*>(L"WePlay Community leased game peer");
            filter.layerKey=*layer; filter.subLayerKey=StandalonePolicySublayer();
            UINT64 weight=(std::numeric_limits<UINT64>::max)();
            filter.weight.type=FWP_UINT64; filter.weight.uint64=&weight;
            // Soft permit: it cannot override a block from Windows Firewall or
            // another provider. Only this sublayer's lower-weight block yields.
            filter.action.type=FWP_ACTION_PERMIT;
            conditions[0].fieldKey=FWPM_CONDITION_ALE_APP_ID; conditions[0].matchType=FWP_MATCH_EQUAL;
            conditions[0].conditionValue.type=FWP_BYTE_BLOB_TYPE; conditions[0].conditionValue.byteBlob=app;
            conditions[1].fieldKey=FWPM_CONDITION_IP_REMOTE_ADDRESS; conditions[1].matchType=FWP_MATCH_EQUAL;
            conditions[1].conditionValue.type=FWP_UINT32; conditions[1].conditionValue.uint32=address;
            conditions[2].fieldKey=FWPM_CONDITION_IP_PROTOCOL; conditions[2].matchType=FWP_MATCH_EQUAL;
            conditions[2].conditionValue.type=FWP_UINT8; conditions[2].conditionValue.uint8=IPPROTO_UDP;
            conditions[3].fieldKey=FWPM_CONDITION_IP_REMOTE_PORT; conditions[3].matchType=FWP_MATCH_EQUAL;
            conditions[3].conditionValue.type=FWP_UINT16; conditions[3].conditionValue.uint16=port;
            filter.numFilterConditions=4; filter.filterCondition=conditions;
            UINT64 id=0; result=FwpmFilterAdd0(engine,&filter,nullptr,&id);
            if (!result) next.push_back(id);
        }
    }
    if (result) { FwpmTransactionAbort0(engine); return result; }
    result=FwpmTransactionCommit0(engine);
    if (!result) ids=std::move(next);
    return result;
}
}

DWORD ServeStandalonePeerPolicy(const wchar_t* executable) {
    std::wstring path; Blob app;
    DWORD result=GetStandaloneAppIdentity(executable,path,&app.value);
    if (result) return result;
    constexpr wchar_t suffix[]=L"\\Base\\Bin\\Settlers7R.exe";
    if (path.size()<=_countof(suffix)-1 || _wcsicmp(path.c_str()+path.size()-(_countof(suffix)-1),suffix)) return ERROR_NOT_SUPPORTED;
    const std::wstring folder=path.substr(0,path.size()-(_countof(suffix)-1))+L"\\LocalData";
    DWORD attributes=GetFileAttributesW(folder.c_str());
    if (attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return ERROR_INVALID_DATA;
    std::string text; Scope initial;
    const std::wstring scope_path=folder+L"\\wfp-peers.ini",ready_path=folder+L"\\wfp-peers.ready";
    if (!Read(scope_path,text) || !Parse(text,initial)) return ERROR_INVALID_DATA;
    Handle parent;
    parent.value=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,initial.parent);
    if (!parent.value || !SameOwner(parent.value,initial.created)) return ERROR_ACCESS_DENIED;
    if (VerifyStandaloneNetworkPolicy(executable)) {
        result=InstallStandaloneNetworkPolicy(executable);
        if (result) return result;
    }
    Engine engine; FWPM_SESSION0 session{}; session.flags=FWPM_SESSION_FLAG_DYNAMIC;
    result=FwpmEngineOpen0(nullptr,RPC_C_AUTHN_WINNT,nullptr,&session,&engine.value);
    if (result) return result;
    std::vector<uint32_t> active,next; std::vector<UINT64> ids;
    if (!Addresses(initial,folder,next)) return ERROR_INVALID_DATA;
    result=Replace(engine.value,app.value,next,ids);
    if (result) return result;
    active=next;
    {
        Handle ready; ready.value=CreateFileW(ready_path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
            FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if (ready.value==INVALID_HANDLE_VALUE) return GetLastError();
        const std::string value=initial.nonce+"\n"; DWORD done=0;
        if (!WriteFile(ready.value,value.data(),static_cast<DWORD>(value.size()),&done,nullptr) || done!=value.size()
            || !FlushFileBuffers(ready.value)) return ERROR_WRITE_FAULT;
    }
    for (;;) {
        if (WaitForSingleObject(parent.value,100)!=WAIT_TIMEOUT) return ERROR_SUCCESS;
        Scope scope;
        if (!Read(scope_path,text) || !Parse(text,scope) || scope.nonce!=initial.nonce || scope.parent!=initial.parent
            || scope.created!=initial.created || scope.local!=initial.local || scope.coordinator!=initial.coordinator
            || scope.group!=initial.group || scope.peer!=initial.peer) return ERROR_SUCCESS;
        if (!Addresses(scope,folder,next)) return ERROR_SUCCESS;
        if (next!=active) {
            result=Replace(engine.value,app.value,next,ids);
            if (result) return result;
            active=next;
        }
    }
}
