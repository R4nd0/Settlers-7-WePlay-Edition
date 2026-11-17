#include "crossplay_selected_saved_source.h"
#include "crossplay_saved_ready_codec.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL selected source line %u\n",__LINE__);return 1;}}while(0)
    namespace S=CrossplaySelectedSavedSource;
    std::wstring path;
    REQUIRE(S::Path(L"Z:\\Owned\\Profiles","s1000",path) && path==L"Z:\\Owned\\Profiles\\Savegames\\s1000.s7s");
    for(const auto* token:{"", "1000", "s", "s../x", "s1000.s7s", "s1000/x", "s1000:other", "s100000000000"}){
        path=L"stale";REQUIRE(!S::Path(L"Z:\\Owned\\Profiles",token,path) && path.empty());
    }
    for(const auto* profile:{L"",L"Profiles",L"Z:Profiles",L"\\\\server\\Profiles",L"Z:\\Owned\\..\\Profiles",L"Z:\\Owned\\Profiles\\",L"Z:\\Owned\\Profiles "}){
        path=L"stale";REQUIRE(!S::Path(profile,"s1000",path) && path.empty());
    }
    CrossplayProduction::Launch launch{};CrossplayBootstrap::Context bootstrap{};
    CrossplayNative::ParticipantEvent original;S::Value value;
    launch.mixed=true;launch.host=true;launch.channel.edition=Crossplay::Edition::Gold;
    launch.channel.content[0]=1;launch.profile=L"Z:\\Owned\\Profiles";
    bootstrap={"Maps/Skirmish/Owned",true,7,"",true};
    // An ordinary unbound host must reject BEFORE inspecting any native cache
    // or opening the advertised profile path. Never invent a source session.
    SetLastError(471);value.session=123;
    REQUIRE(!S::Capture(launch,bootstrap,original,GetCurrentThreadId(),123,{1,2},value));
    REQUIRE(GetLastError()==471 && !value.session && value.configuration.empty() && !value.selected.lease);
    REQUIRE(!S::Current(original,value));
    launch.host=false;REQUIRE(!S::Capture(launch,bootstrap,original,GetCurrentThreadId(),123,{1,2},value));
    launch.host=true;launch.mixed=false;REQUIRE(!S::Capture(launch,bootstrap,original,GetCurrentThreadId(),123,{1,2},value));
    launch.mixed=true;bootstrap.saved=false;REQUIRE(!S::Capture(launch,bootstrap,original,GetCurrentThreadId(),123,{1,2},value));
    CrossplayNativeCodec::PendingReadyToggle ready_pending;
    REQUIRE(!CrossplayNativeCodec::SavedReadyDetail::Enqueue(original,GetCurrentThreadId(),123,{1,2},"Maps/Skirmish/Owned","s1000",{},ready_pending));
    CrossplayReadyState::Snapshot ready_state;
    REQUIRE(CrossplayNativeCodec::SavedReadyDetail::Observe(original,GetCurrentThreadId(),"Maps/Skirmish/Owned","s1000",ready_pending,1,ready_state)==CrossplayNativeCodec::ReadyObservation::Invalid);
    CrossplayResumeSource::ArchiveIdentity ui_identity{};std::string ui_token;
    for(const auto& bad:{std::string(),std::string("s1000"),std::string("Z:/Outside/Profiles/Savegames/s1000.s7s"),
        std::string("Z:/Owned/Profiles/Savegames/../s1000.s7s"),std::string("Z:/Owned/Profiles/Savegames/s1000.s7s:other"),
        std::string("Z:/Owned/Profiles/Savegames/s1000.s7s")+std::string(1,'\0')+"other",std::string(32768,'x')}){
        REQUIRE(!S::SelectFile(launch,bad,ui_identity,ui_token) && ui_token.empty());
    }
    const CrossplaySessionConfig::Connections ids{71,72};const std::string map="Maps/Skirmish/Owned",token="s1000";
    std::vector<unsigned char> wire(13);CrossplayChat::Put(wire.data(),CrossplaySessionConfig::Class);
    CrossplayChat::Put(wire.data()+4,CrossplaySessionConfig::Type);CrossplayChat::Put(wire.data()+8,ids.host);wire[12]=1;
    for(const auto& text:{map,token}){
        const auto at=wire.size();wire.resize(at+8);memcpy(wire.data()+at,"Str_",4);
        CrossplayChat::Put(wire.data()+at+4,static_cast<uint32_t>(text.size()+1));wire.insert(wire.end(),text.begin(),text.end());wire.push_back(0);
    }
    const auto start=wire.size();wire.resize(start+4+8*11);CrossplayChat::Put(wire.data()+start,8);
    for(unsigned i=0;i<8;++i){
        auto* row=wire.data()+start+4+i*11;row[0]=i<4?1:0;row[10]=i<4?4:0;
        CrossplayChat::Put(row+1,i<4?i+1:UINT32_MAX);CrossplayChat::Put(row+6,i<2?(i?ids.guest:ids.host):0);
    }
    CrossplayAIPreparationSource::Snapshot preparation{};preparation.same=true;
    preparation.getter[3]={29,3,1,2,0,0};preparation.getter[4]={24,4,1,2,1,0};preparation.writer=preparation.getter;
    unsigned mask=0;std::array<CrossplaySavedAI::Row,2> ai{};
    REQUIRE(CrossplaySavedAI::Describe(wire,ids,map,token,preparation,mask,ai));REQUIRE(mask==24 && ai[0].profile==29 && ai[1].profile==24);
    unsigned char player[0x5c4]{};CrossplayChat::Put(player+0x18,29);CrossplayChat::Put(player+0x5c,3);
    CrossplayChat::Put(player+0x60,1);CrossplayChat::Put(player+0x64,2);
    REQUIRE(CrossplaySavedAI::Playing(player,sizeof(player),ai[0]));
    for(const auto offset:{0x18,0x5c,0x60,0x64,0x6c,0x70}){
        player[offset]^=1;REQUIRE(!CrossplaySavedAI::Playing(player,sizeof(player),ai[0]));player[offset]^=1;
    }
    REQUIRE(!CrossplaySavedAI::Playing(player,sizeof(player)-1,ai[0]));
    REQUIRE(!CrossplaySavedAI::Playing(player,sizeof(player),ai[1]));
    for(unsigned field=0;field<6;++field){
        auto bad=preparation;
        switch(field){
        case 0:bad.writer[3].profile=30;break;case 1:bad.writer[3].slot=4;break;
        case 2:bad.writer[3].controller=3;break;case 3:bad.writer[3].kind=1;break;
        case 4:bad.writer[3].difficulty=2;break;case 5:bad.writer[3].peaceful=1;break;
        }
        REQUIRE(!CrossplaySavedAI::Describe(wire,ids,map,token,bad,mask,ai));
    }
    auto wrong=preparation;wrong.same=false;REQUIRE(!CrossplaySavedAI::Describe(wire,ids,map,token,wrong,mask,ai));
    wrong=preparation;wrong.getter[3].profile=256;wrong.writer=wrong.getter;
    REQUIRE(!CrossplaySavedAI::Describe(wire,ids,map,token,wrong,mask,ai));
    CrossplaySavedPlayers::Snapshot archived;archived.bytes=41115;
    for(unsigned slot=0;slot<25;++slot){auto& row=archived.rows[slot];row.index=UINT32_MAX;row.kind=slot>=21?8:4;row.difficulty=1;}
    archived.rows[1]={11,1,3,1,1,0};archived.rows[2]={12,2,3,1,1,0};
    archived.rows[3]={29,3,1,2,0,0};archived.rows[4]={24,4,1,2,0,0};
    REQUIRE(CrossplaySavedAI::FromArchive(archived,mask,ai) && mask==24 && ai[0].profile==29 && ai[1].profile==24);
    auto invalid_archive=archived;invalid_archive.rows[3].controller=3;
    REQUIRE(!CrossplaySavedAI::FromArchive(invalid_archive,mask,ai) && !mask && !ai[0].slot);
    invalid_archive=archived;invalid_archive.rows[2].index=1;REQUIRE(!CrossplaySavedAI::FromArchive(invalid_archive,mask,ai));
    invalid_archive=archived;invalid_archive.rows[5]={29,5,1,2,0,0};REQUIRE(!CrossplaySavedAI::FromArchive(invalid_archive,mask,ai));
    invalid_archive=archived;invalid_archive.bytes=0;REQUIRE(!CrossplaySavedAI::FromArchive(invalid_archive,mask,ai));
    // Preparation diagnostics retain their narrow cache semantics, independently
    // of the file-derived authority used by the actual saved source above.
    // A source with one closed position derives its mask from that real saved
    // configuration; it never substitutes the fresh test's constant24 mask.
    auto one=wire;auto* closed=one.data()+start+4+3*11;closed[0]=0;closed[10]=0;CrossplayChat::Put(closed+1,UINT32_MAX);
    REQUIRE(CrossplaySavedAI::Describe(one,ids,map,token,preparation,mask,ai));REQUIRE(mask==8 && ai[1].slot==0);
    printf("PASS %u selected native source boundary checks: original cache/profile binding required, no source/file/Ready manufactured. No game/native source captured.\n",checks);
}
