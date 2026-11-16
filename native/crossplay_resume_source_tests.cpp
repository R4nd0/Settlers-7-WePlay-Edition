#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_resume_source.h"
#include <cstdio>

namespace {
using Bytes=neo_save::Bytes;
void Put(Bytes& bytes,uint32_t value){const auto at=bytes.size();bytes.resize(at+4);neo_save::Put(bytes.data()+at,value);}
void Text(Bytes& bytes,const std::string& value){bytes.insert(bytes.end(),value.begin(),value.end());}
void String(Bytes& bytes,const std::string& value){Text(bytes,"Str_");Put(bytes,static_cast<uint32_t>(value.size()+1));Text(bytes,value);bytes.push_back(0);}
Bytes Encrypt(Bytes data,const std::array<uint32_t,4>& key,uint32_t delta,unsigned rounds){
    std::vector<uint32_t> v(data.size()/4);for(size_t i=0;i<v.size();++i)v[i]=neo_save::Word(data.data()+i*4);
    uint32_t sum=0;
    for(unsigned r=0;r<rounds;++r){
        sum+=delta;const unsigned e=(sum>>2)&3;
        for(size_t p=0;p<v.size();++p){
            const auto y=v[(p+v.size()-1)%v.size()],z=v[(p+1)%v.size()];
            v[p]+=(((y>>5 ^ z<<2)+(y<<4 ^ z>>3)) ^ ((sum^z)+(key[(p&3)^e]^y)));
        }
    }
    for(size_t i=0;i<v.size();++i)neo_save::Put(data.data()+i*4,v[i]);return data;
}
std::string Pickle(uint32_t turn){
    const std::string payload="NEO2\n"+std::to_string(turn+1)+"\n"+std::to_string(turn)+"\n1\n65537,12,-1\n";
    std::string value="return{{[\"version\"]=1,[\"payload\"]=\"";
    for(char c:payload){if(c=='\n')value+="\\n";else value+=c;}return value+="\",},}";
}
Bytes Archive(const std::string& pickle,uint32_t turn,unsigned copies=1,const Bytes* world=nullptr){
    Bytes cache;Text(cache,"bin1");Put(cache,1);Put(cache,0);Put(cache,1);Text(cache,"Coll");Put(cache,0);Put(cache,copies);
    for(unsigned i=0;i<copies;++i){Put(cache,1);String(cache,"weplay_neo_logistics_v1");String(cache,pickle);}Text(cache,"coll");
    const std::string xml="<root><LogicTurn>"+std::to_string(turn)+"</LogicTurn></root>";
    std::vector<std::string> names{"SaveGameCache.s7","SavegameInfo.xml"};
    std::vector<Bytes> members{cache,Bytes(xml.begin(),xml.end())};
    if(world){names.push_back("SaveGame.s7");members.push_back(*world);}
    Bytes data(80),directory(272);Put(directory,static_cast<uint32_t>(names.size()));
    for(size_t i=0;i<names.size();++i){
        const auto at=directory.size();directory.resize(at+64);const auto& raw=members[i];const auto n=names[i].size()+1;
        neo_save::Put(directory.data()+at+8,static_cast<uint32_t>(raw.size()));neo_save::Put(directory.data()+at+12,neo_save::Crc(raw.data(),raw.size()));
        neo_save::Put(directory.data()+at+24,static_cast<uint32_t>(data.size()));neo_save::Put(directory.data()+at+32,static_cast<uint32_t>(raw.size()));
        neo_save::Put(directory.data()+at+48,static_cast<uint32_t>(n));Text(directory,names[i]);directory.push_back(0);
        const auto padded=((n+3)&~size_t(3))+(n%4==0?4:0);directory.resize(at+64+padded);data.insert(data.end(),raw.begin(),raw.end());
    }
    neo_save::Put(directory.data()+4,272);neo_save::Put(directory.data()+8,static_cast<uint32_t>(directory.size()));Put(directory,0);
    const auto encrypted=Encrypt(directory,{892421091,3702197895u,3064602941u,189782668},1532281822,8);
    Bytes header(64);neo_save::Put(header.data(),static_cast<uint32_t>(data.size()));neo_save::Put(header.data()+8,static_cast<uint32_t>(encrypted.size()));
    neo_save::Put(header.data()+12,neo_save::Crc(encrypted.data(),encrypted.size()));header=Encrypt(header,{623092400,3578011951u,428883204,211416760},1231853475,7);
    memcpy(data.data(),"BAF\6",4);neo_save::Put(data.data()+4,7);neo_save::Put(data.data()+8,64);memcpy(data.data()+16,header.data(),64);
    data.insert(data.end(),encrypted.begin(),encrypted.end());return data;
}
CrossplayResumeSource::ArchiveIdentity Identity(const Bytes& bytes){
    CrossplayResumeSource::ArchiveIdentity result;result.bytes=bytes.size();
    if(!CrossplayResumeNeo::Hash(bytes.data(),bytes.size(),result.sha256))throw std::runtime_error("hash failed");return result;
}
CrossplayResumeTransfer::Checkpoint Checkpoint(const std::string& pickle,uint32_t turn){
    CrossplayResumeTransfer::Checkpoint cp;cp.content[0]=1;cp.file[0]=2;cp.setup[0]=3;cp.logic_turn=turn;cp.replay_turn=turn+1;cp.bytes=100;cp.features=19;cp.enabled=true;
    if(!CrossplayResumeNeo::Hash(reinterpret_cast<const unsigned char*>(pickle.data()),pickle.size(),cp.neo))throw std::runtime_error("hash failed");return cp;
}
bool Write(const std::wstring& path,const Bytes& bytes,DWORD disposition){
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,disposition,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool okay=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size();CloseHandle(file);return okay;
}
}
int wmain(){
    namespace S=CrossplayResumeSource;unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL resume source line %u\n",__LINE__);return 1;}}while(0)
    const auto first=Pickle(100),second=Pickle(700);const auto a=Archive(first,100),b=Archive(second,700);
    const auto ai=Identity(a),bi=Identity(b);const auto ac=Checkpoint(first,100),bc=Checkpoint(second,700);std::string serialized,payload;
    CHECK(S::Inspect(a.data(),a.size(),ai,ac,serialized,payload));CHECK(serialized==first);
    CHECK(S::Inspect(b.data(),b.size(),bi,bc,serialized,payload));CHECK(serialized==second && payload.find("701\n700\n")!=std::string::npos);
    CHECK(!S::Inspect(b.data(),b.size(),ai,bc,serialized,payload));CHECK(serialized.empty() && payload.empty());
    CHECK(!S::Inspect(b.data(),b.size(),bi,ac,serialized,payload));CHECK(serialized.empty() && payload.empty());
    auto wrong=bi;++wrong.bytes;CHECK(!S::Inspect(b.data(),b.size(),wrong,bc,serialized,payload));
    wrong=bi;wrong.sha256[0]^=1;CHECK(!S::Inspect(b.data(),b.size(),wrong,bc,serialized,payload));
    CHECK(!S::Inspect(nullptr,b.size(),bi,bc,serialized,payload));
    for(const auto& invalid:std::array<Bytes,5>{Archive(second,699),Archive(second,700,0),Archive(second,700,2),Archive("return error('never execute')",700),Archive(second.substr(1),700)}){
        auto cp=bc;
        if(invalid==Archive("return error('never execute')",700))cp=Checkpoint("return error('never execute')",700);
        CHECK(!S::Inspect(invalid.data(),invalid.size(),Identity(invalid),cp,serialized,payload));CHECK(serialized.empty() && payload.empty());
    }
    auto corrupt=b;corrupt[80]^=1;CHECK(!S::Inspect(corrupt.data(),corrupt.size(),Identity(corrupt),bc,serialized,payload));
    for(size_t size:{size_t(0),size_t(79),b.size()-1}){Bytes cut(b.begin(),b.begin()+size);CHECK(!S::Inspect(cut.data(),cut.size(),bi,bc,serialized,payload));}
    wchar_t cwd[32768]{};CHECK(GetCurrentDirectoryW(32768,cwd)>0);const auto path=std::wstring(cwd)+L"\\resume-source-"+std::to_wstring(GetCurrentProcessId())+L".private.s7s";
    CHECK(Write(path,a,CREATE_NEW));SetLastError(0x44332211);CHECK(S::Read(path,ai,ac,serialized,payload));CHECK(GetLastError()==0x44332211 && serialized==first);
    CHECK(Write(path,b,TRUNCATE_EXISTING));CHECK(S::Read(path,bi,bc,serialized,payload));CHECK(serialized==second);
    CHECK(!S::Read(path,ai,ac,serialized,payload));CHECK(serialized.empty() && payload.empty());
    const auto writer=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(writer!=INVALID_HANDLE_VALUE);
    SetLastError(0x55443322);CHECK(!S::Read(path,bi,bc,serialized,payload));CHECK(GetLastError()==0x55443322 && serialized.empty() && payload.empty());CHECK(CloseHandle(writer));
    CHECK(!S::Read(L"relative.s7s",bi,bc,serialized,payload));wrong=bi;wrong.bytes=S::MaximumArchiveBytes+1;CHECK(!S::Read(path,wrong,bc,serialized,payload));
    CHECK(DeleteFileW(path.c_str()));CHECK(!S::Read(path,bi,bc,serialized,payload));
    // Production capture derives every checkpoint field from the selected
    // archive. Two valid worlds at the same filename must never reuse state.
    Crossplay::Digest content{};content[0]=17;const std::string map="Maps\\Skirmish\\s04_m06_Monhaim_Valley";
    S::Selected selected;Bytes world(100,0x51);const auto full=Archive(second,700,1,&world);
    CHECK(S::Describe(full.data(),full.size(),content,map,selected));
    CHECK(selected.checkpoint.logic_turn==700 && selected.checkpoint.replay_turn==701
        && selected.checkpoint.bytes==world.size() && selected.checkpoint.features==19 && selected.checkpoint.enabled);
    CHECK(selected.checkpoint.neo==bc.neo && selected.serialized==second && selected.payload.find("701\n700\n")!=std::string::npos);
    Crossplay::Digest world_hash{};CHECK(CrossplayResumeNeo::Hash(world.data(),world.size(),world_hash));
    CHECK(selected.checkpoint.file==world_hash && selected.archive.sha256==Identity(full).sha256);
    const auto original=selected;
    world[0]^=1;const auto changed=Archive(second,700,1,&world);
    CHECK(S::Describe(changed.data(),changed.size(),content,map,selected));
    CHECK(selected.checkpoint.file!=original.checkpoint.file && selected.checkpoint.setup!=original.checkpoint.setup
        && selected.archive.sha256!=original.archive.sha256 && selected.checkpoint.neo==original.checkpoint.neo);
    CHECK(S::Describe(full.data(),full.size(),content,map+"Other",selected));
    CHECK(selected.checkpoint.setup!=original.checkpoint.setup && selected.checkpoint.file==original.checkpoint.file);
    CHECK(!S::Describe(b.data(),b.size(),content,map,selected) && selected.serialized.empty());
    for(const auto& invalid:{Archive(second,699,1,&world),Archive(second,700,0,&world),Archive(second,700,2,&world),Archive("return error('never execute')",700,1,&world)}){
        CHECK(!S::Describe(invalid.data(),invalid.size(),content,map,selected));CHECK(selected.payload.empty() && !selected.lease);
    }
    world.resize(79);const auto short_world=Archive(second,700,1,&world);
    CHECK(!S::Describe(short_world.data(),short_world.size(),content,map,selected));
    CHECK(!S::Describe(full.data(),full.size(),{},map,selected));
    CHECK(!S::Describe(full.data(),full.size(),content,"../Map",selected));
    CHECK(!S::Describe(full.data(),full.size(),content,std::string(256,'a'),selected));
    // Restore a sufficiently sized world after the short-world negative case.
    world.resize(100,0x51);const auto disabled=Archive("return{{[\"version\"]=1,[\"payload\"]=\"NEO0\\n\",},}",700,1,&world);
    CHECK(S::Describe(disabled.data(),disabled.size(),content,map,selected));
    CHECK(!selected.checkpoint.enabled && selected.payload=="NEO0\n" && selected.checkpoint.features==19
        && selected.checkpoint.logic_turn==700 && selected.checkpoint.replay_turn==701);
    CHECK(Write(path,full,CREATE_NEW));SetLastError(1907);CHECK(S::CaptureSelected(path,content,map,selected) && GetLastError()==1907);
    CHECK(selected.lease && selected.lease->value!=INVALID_HANDLE_VALUE);
    const auto blocked=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(blocked==INVALID_HANDLE_VALUE);CHECK(!DeleteFileW(path.c_str()));
    selected={};CHECK(Write(path,changed,TRUNCATE_EXISTING));
    CHECK(S::CaptureSelected(path,content,map,selected));CHECK(selected.checkpoint.file!=original.checkpoint.file);
    selected={};CHECK(DeleteFileW(path.c_str()));
    printf("PASS %u selected resume archive checks: two checkpoints, complete archive/NEO hashes, clocks, restricted metadata, no stale fixture, writer exclusion, error preservation.\n",checks);return 0;
}
