#pragma once
#include "crossplay_session_config.h"

// A receipt of one exact original configuration consumption. It is neither a
// complete setup comparison nor a native Ready, start or NEO agreement.
namespace CrossplayConfigurationReceipt {
enum class Observation { Pending,Invalid,Applied };
struct Interval {uint32_t owner=0;uintptr_t session=0,cache=0;};
inline bool Same(const Interval& a,const Interval& b){
    return a.owner && a.session && a.cache && a.owner==b.owner && a.session==b.session && a.cache==b.cache;
}
class Receipt {
    enum class Stage { New,Armed,Consumed,Invalid } stage=Stage::New;
    Interval interval{};
    CrossplaySessionConfig::Connections ids{};
    std::vector<unsigned char> expected;
    uint64_t started=0;
public:
    bool Active()const{return stage==Stage::Armed || stage==Stage::Consumed;}
    bool HasConsumed()const{return stage==Stage::Consumed;}
    bool ConsumedIn(Interval own,uint64_t now)const{
        return HasConsumed() && Same(interval,own) && now>=started && now-started<=10000;
    }
    void Invalidate(){stage=Stage::Invalid;expected.clear();}
    bool Arm(Interval own,CrossplaySessionConfig::Connections connections,const std::string& bootstrap_map,
             const std::vector<unsigned char>& wire,uint64_t now){
        CrossplaySessionConfig::Layout layout;
        if(stage!=Stage::New || !Same(own,own) || !CrossplaySessionConfig::Inspect(wire,connections,layout)
            || layout.map!=bootstrap_map){Invalidate();return false;}
        interval=own;ids=connections;expected=wire;started=now;stage=Stage::Armed;return true;
    }
    bool Consumed(Interval own,const std::vector<unsigned char>& wire,uint64_t now){
        if(stage!=Stage::Armed || !Same(interval,own) || now<started || now-started>10000 || wire!=expected){
            Invalidate();return false;
        }
        stage=Stage::Consumed;return true;
    }
    Observation Observe(Interval own,const std::vector<unsigned char>& actual,uint64_t now){
        if(!Active() || !Same(interval,own) || now<started || now-started>10000){Invalidate();return Observation::Invalid;}
        CrossplaySessionConfig::Layout layout;
        if(!CrossplaySessionConfig::Inspect(actual,ids,layout)){Invalidate();return Observation::Invalid;}
        return stage==Stage::Consumed && actual==expected?Observation::Applied:Observation::Pending;
    }
    const std::vector<unsigned char>& Expected()const{return expected;}
};
}
