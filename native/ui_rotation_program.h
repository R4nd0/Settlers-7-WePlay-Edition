#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <cstring>

// Recipient-owned SM3 program transformation. Runtime admission separately pins
// the complete original VS/PS identities. No original bytecode is bundled.
namespace UiRotationProgram {
inline unsigned Type(std::uint32_t word){return ((word>>28)&7u)|((word>>8)&24u);}
inline std::uint32_t Dst(unsigned type,unsigned index,unsigned mask){
    return 0x80000000u|((type<<28)&0x70000000u)|((type<<8)&0x1800u)|index|(mask<<16);
}
inline std::uint32_t Src(unsigned type,unsigned index,unsigned swizzle=0xe4u,bool negative=false){
    return Dst(type,index,0)|(swizzle<<16)|(negative?0x01000000u:0u);
}
inline void Instruction(std::vector<std::uint32_t>& out,unsigned op,std::initializer_list<std::uint32_t> args){
    out.push_back((static_cast<std::uint32_t>(args.size())<<24)|op);out.insert(out.end(),args.begin(),args.end());
}
inline bool Patch(const std::vector<std::uint32_t>& source,std::vector<std::uint32_t>& result){
    if(source.size()!=121||source.front()!=0xfffe0300u||source.back()!=0xffffu)return false;
    std::vector<std::uint32_t> out{source.front()};out.reserve(source.size()+47);
    size_t at=1;unsigned changed=0;bool inserted=false,ended=false;std::uint32_t scale=0;
    while(at<source.size()){
        const unsigned op=source[at]&0xffffu;
        if(op==0xffffu){if(at+1!=source.size())return false;out.push_back(source[at]);ended=true;break;}
        const size_t length=1+(op==0xfffeu?(source[at]>>16)&0x7fffu:(source[at]>>24)&15u);
        if(length<=1||length>source.size()-at)return false;
        std::vector<std::uint32_t> row(source.begin()+at,source.begin()+at+length);
        if(op==81u&&length==6u&&(row[1]&0x7ffu)==0u)scale=row[2];
        if(op==31u&&!inserted){
            // 32767/10 source pixels per signed-normalized center component.
            // The integer wrap period is65536/10, not2*(32767/10).
            if(scale!=0x454ccb33u)return false;
            Instruction(out,81,{Dst(2,254,15),scale,0x40000100u,0,0});inserted=true;
        }
        if(op==1u&&length==3u&&row[1]==Dst(0,0,3)&&Type(row[2])==1u&&(row[2]&0x7ffu)==2u){
            Instruction(out,4,{Dst(0,3,1),Src(1,2,0,true),Src(2,0,0),Src(1,0,0)});
            Instruction(out,12,{Dst(0,3,1),Src(2,254,0),Src(0,3,0)});
            // Identity rotations and non-wrapped pivots retain their original
            // center representation, including its exact cancellation rounding.
            Instruction(out,2,{Dst(0,3,2),Src(1,2,0xff,true),Src(2,1,0)});
            Instruction(out,11,{Dst(0,3,2),Src(0,3,0x55),Src(1,2,0xaa)|0x0b000000u});
            Instruction(out,12,{Dst(0,3,2),Src(2,254,0xaa),Src(0,3,0x55)});
            Instruction(out,5,{Dst(0,3,1),Src(0,3,0),Src(0,3,0x55)});
            Instruction(out,12,{Dst(0,3,4),Src(1,2,0),Src(2,254,0xaa)});
            Instruction(out,5,{Dst(0,3,1),Src(0,3,0),Src(0,3,0xaa)});
            Instruction(out,4,{Dst(0,2,1),Src(0,3,0),Src(2,254,0x55),Src(1,2,0)});
            Instruction(out,1,{Dst(0,2,2),Src(1,2,0x55)});
            row[2]&=~0x70001800u;++changed;
        }
        if(op==4u&&length==5u&&row[1]==Dst(0,0,3)&&Type(row[2])==1u&&(row[2]&0x7ffu)==2u){
            row[2]&=~0x70001800u;++changed;
        }
        out.insert(out.end(),row.begin(),row.end());at+=length;
    }
    if(!ended||!inserted||changed!=2u||out.size()!=168)return false;
    result=std::move(out);return true;
}
}
