#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_choices.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){printf("FAIL History choices check %u\n",checks);std::exit(1);}}
CrossplayHistoryChoices::SmallString Inline(const std::string& text){
    CrossplayHistoryChoices::SmallString result{};result.capacity=15;result.length=static_cast<uint32_t>(text.size());
    if(text.size()<16)memcpy(result.storage,text.c_str(),text.size()+1);return result;
}
}
extern "C" __declspec(dllexport) int __cdecl ProductionHistoryChoicesGuardOracle(unsigned edition){
    return CrossplayHistoryChoices::Methods(static_cast<Crossplay::Edition>(edition))?1:0;
}
int main(){
    using namespace CrossplayHistoryChoices;
    Pin pin;Check(!Layout(Crossplay::Edition::Gold,pin)&&!pin.create);Check(!Methods(Crossplay::Edition::Gold));
    Check(!Layout(static_cast<Crossplay::Edition>(99),pin));
    for(auto edition:{Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory})Check(Layout(edition,pin)&&pin.create&&pin.backend&&pin.hash);
    for(unsigned i=0;i<8;++i){uint32_t flags=99;Check(Flags(i&1,(i>>1)&1,(i>>2)&1,flags)&&flags==i);}
    for(unsigned i=2;i<256;++i){uint32_t flags=99;Check(!Flags(i,0,0,flags)&&!flags);Check(!Flags(0,i,0,flags)&&!flags);Check(!Flags(0,0,i,flags)&&!flags);}
    std::string value;
    for(const std::string text:{"","MapForge","Maps/Template"}){
        auto input=Inline(text);const auto before=input;Check(Template(input,value)&&value==text);Check(!memcmp(&input,&before,sizeof(input)));
        memset(input.storage,0xcc,sizeof(input.storage));Check(value==text);
    }
    for(size_t length:{size_t(16),size_t(17),size_t(255)}){
        std::string text(length,'x');SmallString input{};const char* data=text.c_str();memcpy(input.storage,&data,sizeof(data));
        input.length=static_cast<uint32_t>(length);input.capacity=input.length;
        Check(Template(input,value)&&value==text);text.assign(length,'y');Check(value==std::string(length,'x'));
    }
    auto input=Inline("Name");input.capacity=14;Check(!Template(input,value)&&value.empty());
    input=Inline("Name");input.length=16;Check(!Template(input,value));
    input=Inline("Name");input.capacity=0x100001;Check(!Template(input,value));
    input=Inline("Name");input.storage[1]=0;Check(!Template(input,value));
    input=Inline("Name");input.storage[4]='x';Check(!Template(input,value));
    input={};input.capacity=16;input.length=1;Check(!Template(input,value));
    input=Inline("../Foreign");Check(!Template(input,value));
    input=Inline("C:/Foreign");Check(!Template(input,value));
    input=Inline("Name");input.storage[0]=0xff;Check(!Template(input,value));
    Value a{1,2,3,4,5,"Maps/Actual","",7},b=a;Check(Same(a,b));
    for(auto field:{&Value::cache,&Value::backend,&Value::handler,&Value::implementation,&Value::descriptor}){
        b=a;b.*field=0;Check(!Same(a,b));b=a;(b.*field)+=4;Check(!Same(a,b));
    }
    b=a;b.map="Maps/Foreign";Check(!Same(a,b));b=a;b.map_template="Other";Check(!Same(a,b));b=a;b.flags=0;Check(!Same(a,b));
    printf("PASS %u History choice guards/string ownership/flags checks. Native game methods not executed.\n",checks);
}
