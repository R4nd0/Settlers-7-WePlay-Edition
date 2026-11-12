// Application-scoped persistent WFP blocks. No global firewall setting changes.
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <fwpmu.h>
#include <sddl.h>
#include <cstdio>
#include <string>
#include <vector>
#include <cstring>
#include "standalone_image.h"
#include "standalone_network_policy.h"

namespace {
const GUID provider = {0x48747030,0x748b,0x47cc,{0x8c,0xdd,0x49,0x7a,0x65,0x0a,0x6d,0x53}};
const GUID sublayer = {0x48747031,0x748b,0x47cc,{0x8c,0xdd,0x49,0x7a,0x65,0x0a,0x6d,0x53}};
const GUID* const layers[] = {&FWPM_LAYER_ALE_AUTH_CONNECT_V4,&FWPM_LAYER_ALE_AUTH_CONNECT_V6,
    &FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4,&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6};
constexpr wchar_t name[] = L"WePlay Community standalone isolation v1";
FWP_V4_ADDR_AND_MASK loopback4 = {0x7f000000,0xff000000};
FWP_V6_ADDR_AND_MASK loopback6 = {{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},128};
struct Engine { HANDLE value = nullptr; ~Engine() { if (value) FwpmEngineClose0(value); } };
struct Blob { FWP_BYTE_BLOB* value = nullptr; ~Blob() { if (value) FwpmFreeMemory0(reinterpret_cast<void**>(&value)); } };
struct Security { PSECURITY_DESCRIPTOR value = nullptr; ~Security() { if (value) LocalFree(value); } };
bool Same(const GUID& a,const GUID& b) { return !memcmp(&a,&b,sizeof(GUID)); }
bool Named(const FWPM_DISPLAY_DATA0& data) { return data.name && !wcscmp(data.name,name); }
DWORD Identity(const wchar_t* argument,std::wstring& path,Blob& app) {
    HANDLE file = CreateFileW(argument,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (file == INVALID_HANDLE_VALUE) return GetLastError();
    std::vector<unsigned char> bytes;
    wchar_t final_path[32768]{};
    DWORD length = GetFinalPathNameByHandleW(file,final_path,32768,FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    bool valid = StandaloneImage::ReadLockedFile(file,20*1024*1024,bytes);
    CloseHandle(file);
    if (!valid || !length || length >= 32768 || wcsncmp(final_path,L"\\\\?\\",4)) return ERROR_INVALID_DATA;
    path = final_path+4;
    const auto separator = path.find_last_of(L"\\/");
    if (separator == std::wstring::npos) return ERROR_INVALID_NAME;
    // Only a prepared copy explicitly marked by the installer can be scoped.
    std::array<unsigned char,32> digest{};
    if (!StandaloneImage::Digest(bytes.data(),bytes.size(),digest)) return ERROR_INVALID_DATA;
    std::string marker = "WePlay network scope v1\n";
    const char* hex = "0123456789abcdef";
    for (unsigned char value : digest) { marker += hex[value >> 4]; marker += hex[value & 15]; }
    marker += '\n';
    const auto marker_path = path.substr(0,separator+1)+L".weplay-isolation";
    file = CreateFileW(marker_path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if (file == INVALID_HANDLE_VALUE) return ERROR_INVALID_DATA;
    std::vector<unsigned char> stored;
    valid = StandaloneImage::ReadLockedFile(file,128,stored);
    CloseHandle(file);
    if (!valid || stored.size() != marker.size() || memcmp(stored.data(),marker.data(),marker.size())) return ERROR_INVALID_DATA;
    // Reject junctions/symlinks throughout the resolved root as well.
    for (size_t end = 3; end < path.size(); ++end) if (path[end] == L'\\') {
        DWORD attributes = GetFileAttributesW(path.substr(0,end).c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return ERROR_INVALID_DATA;
    }
    return FwpmGetAppIdFromFileName0(path.c_str(),&app.value);
}
bool Key(const FWP_BYTE_BLOB& app,const GUID& layer,GUID& key) {
    std::vector<unsigned char> bytes(app.data,app.data+app.size);
    const auto* suffix = reinterpret_cast<const unsigned char*>(&layer);
    bytes.insert(bytes.end(),suffix,suffix+sizeof(GUID));
    std::array<unsigned char,32> hash{};
    if (!StandaloneImage::Digest(bytes.data(),bytes.size(),hash)) return false;
    memcpy(&key,hash.data(),sizeof(key));
    return true;
}
void Definition(FWPM_FILTER0& filter,GUID key,const GUID& layer,FWP_BYTE_BLOB* app,FWPM_FILTER_CONDITION0 (&conditions)[2]) {
    filter.filterKey = key;
    filter.displayData.name = const_cast<wchar_t*>(name);
    filter.flags = FWPM_FILTER_FLAG_PERSISTENT;
    filter.providerKey = const_cast<GUID*>(&provider);
    filter.layerKey = layer; filter.subLayerKey = sublayer;
    filter.weight.type = FWP_UINT8; filter.weight.uint8 = 15;
    filter.action.type = FWP_ACTION_BLOCK;
    conditions[0].fieldKey = FWPM_CONDITION_ALE_APP_ID;
    conditions[0].matchType = FWP_MATCH_EQUAL;
    conditions[0].conditionValue.type = FWP_BYTE_BLOB_TYPE;
    conditions[0].conditionValue.byteBlob = app;
    conditions[1].fieldKey = FWPM_CONDITION_IP_REMOTE_ADDRESS;
    conditions[1].matchType = FWP_MATCH_NOT_EQUAL;
    if (Same(layer,FWPM_LAYER_ALE_AUTH_CONNECT_V4) || Same(layer,FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4)) {
        conditions[1].conditionValue.type = FWP_V4_ADDR_MASK;
        conditions[1].conditionValue.v4AddrMask = &loopback4;
    } else {
        conditions[1].conditionValue.type = FWP_V6_ADDR_MASK;
        conditions[1].conditionValue.v6AddrMask = &loopback6;
    }
    filter.numFilterConditions = 2; filter.filterCondition = conditions;
}
bool Equal(const FWPM_FILTER0& actual,const FWPM_FILTER0& expected) {
    if (!Named(actual.displayData) || (actual.flags & ~FWPM_FILTER_FLAG_INDEXED) != expected.flags || !actual.providerKey
        || !Same(*actual.providerKey,provider) || !Same(actual.layerKey,expected.layerKey)
        || !Same(actual.subLayerKey,sublayer) || actual.action.type != FWP_ACTION_BLOCK
        || actual.weight.type != FWP_UINT8 || actual.weight.uint8 != 15 || actual.numFilterConditions != 2) {
        std::fprintf(stderr,"Policy shape: flags=%lu weight-type=%d weight-byte=%u conditions=%lu\n",
            actual.flags,static_cast<int>(actual.weight.type),static_cast<unsigned>(actual.weight.uint8),actual.numFilterConditions);
        return false;
    }
    bool app = false, flags = false;
    for (size_t i = 0; i < 2; ++i) {
        const auto& condition = actual.filterCondition[i];
        if (Same(condition.fieldKey,FWPM_CONDITION_ALE_APP_ID)) {
            const auto* blob = condition.conditionValue.byteBlob;
            const auto* wanted = expected.filterCondition[0].conditionValue.byteBlob;
            if (app || condition.matchType != FWP_MATCH_EQUAL || condition.conditionValue.type != FWP_BYTE_BLOB_TYPE
                || !blob || blob->size != wanted->size || memcmp(blob->data,wanted->data,blob->size)) return false;
            app = true;
        } else if (Same(condition.fieldKey,FWPM_CONDITION_IP_REMOTE_ADDRESS)) {
            const auto& wanted = expected.filterCondition[1].conditionValue;
            if (flags || condition.matchType != FWP_MATCH_NOT_EQUAL || condition.conditionValue.type != wanted.type) {
                std::fprintf(stderr,"Policy address: match=%d type=%d expected=%d\n",static_cast<int>(condition.matchType),
                    static_cast<int>(condition.conditionValue.type),static_cast<int>(wanted.type)); return false;
            }
            if (wanted.type == FWP_V4_ADDR_MASK) {
                const auto* address = condition.conditionValue.v4AddrMask;
                if (!address || address->addr != loopback4.addr || address->mask != loopback4.mask) return false;
            } else {
                const auto* address = condition.conditionValue.v6AddrMask;
                if (!address || address->prefixLength != 128 || memcmp(address->addr,loopback6.addr,16)) return false;
            }
            flags = true;
        } else return false;
    }
    return app && flags;
}
DWORD Containers(HANDLE engine,PSECURITY_DESCRIPTOR security,bool install) {
    FWPM_PROVIDER0* found = nullptr;
    DWORD result = FwpmProviderGetByKey0(engine,&provider,&found);
    if (result == FWP_E_PROVIDER_NOT_FOUND && install) {
        FWPM_PROVIDER0 item{}; item.providerKey = provider; item.displayData.name = const_cast<wchar_t*>(name);
        item.flags = FWPM_PROVIDER_FLAG_PERSISTENT;
        result = FwpmProviderAdd0(engine,&item,security);
    } else if (!result) {
        const bool valid = Named(found->displayData) && found->flags == FWPM_PROVIDER_FLAG_PERSISTENT
            && (!found->serviceName || !*found->serviceName);
        if (!valid) std::fprintf(stderr,"Policy provider: name=%d flags=%lu service=%d\n",Named(found->displayData),
            found->flags,found->serviceName && *found->serviceName ? 1 : 0);
        FwpmFreeMemory0(reinterpret_cast<void**>(&found));
        if (!valid) return ERROR_INVALID_DATA;
    }
    if (result) return result;
    FWPM_SUBLAYER0* layer = nullptr;
    result = FwpmSubLayerGetByKey0(engine,&sublayer,&layer);
    if (result == FWP_E_SUBLAYER_NOT_FOUND && install) {
        FWPM_SUBLAYER0 item{}; item.subLayerKey = sublayer; item.displayData.name = const_cast<wchar_t*>(name);
        item.flags = FWPM_SUBLAYER_FLAG_PERSISTENT; item.providerKey = const_cast<GUID*>(&provider); item.weight = 0xfffe;
        result = FwpmSubLayerAdd0(engine,&item,security);
    } else if (!result) {
        const bool valid = Named(layer->displayData) && layer->flags == FWPM_SUBLAYER_FLAG_PERSISTENT
            && layer->providerKey && Same(*layer->providerKey,provider) && layer->weight == 0xfffe;
        if (!valid) std::fprintf(stderr,"Policy sublayer: name=%d flags=%u weight=%u\n",Named(layer->displayData),
            static_cast<unsigned>(layer->flags),static_cast<unsigned>(layer->weight));
        FwpmFreeMemory0(reinterpret_cast<void**>(&layer));
        if (!valid) return ERROR_INVALID_DATA;
    }
    return result;
}
DWORD Run(const wchar_t* action,const wchar_t* argument) {
    const bool install = !wcscmp(action,L"install"), remove = !wcscmp(action,L"remove");
    if (!install && !remove && wcscmp(action,L"check")) return ERROR_INVALID_PARAMETER;
    std::wstring path; Blob app;
    DWORD result = Identity(argument,path,app);
    if (result) return result;
    Engine engine;
    result = FwpmEngineOpen0(nullptr,RPC_C_AUTHN_WINNT,nullptr,nullptr,&engine.value);
    if (result) return result;
    Security security;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GR;;;WD)",
            SDDL_REVISION_1,&security.value,nullptr)) return GetLastError();
    bool transaction = false;
    if (install || remove) {
        result = FwpmTransactionBegin0(engine.value,0);
        if (result) return result;
        transaction = true;
    }
    result = Containers(engine.value,security.value,install);
    for (const auto* layer : layers) {
        if (result) break;
        GUID key{};
        if (!Key(*app.value,*layer,key)) { result = ERROR_INVALID_DATA; break; }
        FWPM_FILTER0 expected{}; FWPM_FILTER_CONDITION0 conditions[2]{};
        Definition(expected,key,*layer,app.value,conditions);
        FWPM_FILTER0* actual = nullptr;
        result = FwpmFilterGetByKey0(engine.value,&key,&actual);
        if (result == FWP_E_FILTER_NOT_FOUND && install) result = FwpmFilterAdd0(engine.value,&expected,security.value,nullptr);
        else if (result == FWP_E_FILTER_NOT_FOUND && remove) result = ERROR_SUCCESS;
        else if (!result) {
            const bool valid = Equal(*actual,expected);
            FwpmFreeMemory0(reinterpret_cast<void**>(&actual));
            if (!valid) result = ERROR_INVALID_DATA;
            else if (remove) result = FwpmFilterDeleteByKey0(engine.value,&key);
        }
    }
    if (transaction) {
        if (result) FwpmTransactionAbort0(engine.value);
        else result = FwpmTransactionCommit0(engine.value);
    }
    return result;
}
}
DWORD VerifyStandaloneNetworkPolicy(const wchar_t* executable) { return Run(L"check",executable); }
DWORD InstallStandaloneNetworkPolicy(const wchar_t* executable) { return Run(L"install",executable); }
DWORD GetStandaloneAppIdentity(const wchar_t* executable,std::wstring& path,FWP_BYTE_BLOB** app) {
    Blob blob;
    const DWORD result=Identity(executable,path,blob);
    if (!result) { *app=blob.value; blob.value=nullptr; }
    return result;
}
const GUID& StandalonePolicySublayer() { return sublayer; }
#ifndef S7_NETWORK_POLICY_LIBRARY
int wmain(int argc,wchar_t** argv) {
    DWORD result = argc == 3 ? (!wcscmp(argv[1],L"serve-lan") ? ServeStandalonePeerPolicy(argv[2]) : Run(argv[1],argv[2])) : ERROR_INVALID_PARAMETER;
    if (result) {
        std::fprintf(stderr,"Standalone Windows filtering failed: 0x%08lx\n",result);
        return result == ERROR_ACCESS_DENIED ? 5 : 2;
    }
    std::wprintf(L"PASS %s: four persistent per-application IPv4/IPv6 non-loopback blocks\n",argv[1]);
    return 0;
}
#endif
