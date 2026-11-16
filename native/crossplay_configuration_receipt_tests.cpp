#include "crossplay_configuration_receipt.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){printf("FAIL configuration receipt check %u\n",checks);std::exit(1);}}
void Word(std::vector<unsigned char>& bytes,uint32_t value){const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);}
void Text(std::vector<unsigned char>& bytes,const std::string& value){
    bytes.insert(bytes.end(),{'S','t','r','_'});Word(bytes,value.empty()?0u:static_cast<uint32_t>(value.size()+1));
    if(!value.empty()){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}
}
std::vector<unsigned char> Wire(unsigned flags=0){
    std::vector<unsigned char> result;Word(result,CrossplaySessionConfig::Class);Word(result,CrossplaySessionConfig::Type);Word(result,1);result.push_back(1);
    Text(result,"Maps/ActualSelection");Text(result,"");Word(result,8);
    for(unsigned i=0;i<8;++i){
        result.push_back(i<2?static_cast<unsigned char>((flags>>i)&1):0);Word(result,i<2?i+1:UINT32_MAX);
        result.push_back(0);Word(result,i<2?(i?0u:1u):0u);result.push_back(i<2?3u:5u);
    }
    return result;
}
}
int main(){
    using namespace CrossplayConfigurationReceipt;
    const Interval own{7,0x10000,0x20000};const CrossplaySessionConfig::Connections ids{1,0};const auto wire=Wire();
    Receipt initial;Check(initial.Observe(own,wire,0)==Observation::Invalid);
    for(unsigned flags=0;flags<4;++flags){
        const auto selected=Wire(flags);Receipt receipt;
        Check(receipt.Arm(own,ids,"Maps/ActualSelection",selected,100));
        Check(receipt.Observe(own,selected,101)==Observation::Pending); // Cache already matches, but no consumption.
        Check(receipt.Consumed(own,selected,102));
        Check(receipt.Observe(own,selected,103)==Observation::Applied);
        Check(receipt.Observe(own,Wire(flags^1),104)==Observation::Pending); // Real readiness changed; do not mask it.
        Check(receipt.Observe(own,selected,105)==Observation::Applied);
        Check(!receipt.Consumed(own,selected,106)); // A repeated notification cannot advance an old transaction.
        Check(receipt.Observe(own,selected,107)==Observation::Invalid);
    }
    for(unsigned which=0;which<3;++which){
        auto stale=own;if(which==0)++stale.owner;if(which==1)++stale.session;if(which==2)++stale.cache;
        Receipt receipt;Check(receipt.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(!receipt.Consumed(stale,wire,101));Check(!receipt.Active());
        Receipt other;Check(other.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(other.Observe(stale,wire,101)==Observation::Invalid);
    }
    for(uint64_t now:{uint64_t(99),uint64_t(10101)}){
        Receipt receipt;Check(receipt.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(!receipt.Consumed(own,wire,now));
        Receipt other;Check(other.Arm(own,ids,"Maps/ActualSelection",wire,100));
        Check(other.Observe(own,wire,now)==Observation::Invalid);
    }
    Receipt exact;Check(exact.Arm(own,ids,"Maps/ActualSelection",wire,100));
    Check(exact.Consumed(own,wire,10100)&&exact.Observe(own,wire,10100)==Observation::Applied);
    for(auto invalid:std::vector<Interval>{{0,own.session,own.cache},{own.owner,0,own.cache},{own.owner,own.session,0}}){
        Receipt receipt;Check(!receipt.Arm(invalid,ids,"Maps/ActualSelection",wire,100));
    }
    Receipt wrong_map;Check(!wrong_map.Arm(own,ids,"Maps/OtherSelection",wire,100));
    Receipt wrong_ids;Check(!wrong_ids.Arm(own,{0,1},"Maps/ActualSelection",wire,100));
    Receipt repeated;Check(repeated.Arm(own,ids,"Maps/ActualSelection",wire,100));Check(!repeated.Arm(own,ids,"Maps/ActualSelection",wire,100));
    for(size_t at=0;at<wire.size();++at){
        auto altered=wire;altered[at]^=0x80;Receipt receipt;
        Check(receipt.Arm(own,ids,"Maps/ActualSelection",wire,100));Check(!receipt.Consumed(own,altered,101));
    }
    printf("PASS %u configuration consumption receipt checks; matching cache without original consumption remains pending; owner/session/cache/bytes/deadline bound.\n",checks);
}
