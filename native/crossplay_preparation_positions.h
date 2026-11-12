#pragma once
#include <array>
#include <stdint.h>
#include <stddef.h>

// Read-only bounds for the two index lists traversed by History's original
// preparation position setter. This laboratory path is limited to Monhaim.
namespace CrossplayPreparationPositions {
constexpr uint32_t RowSize=0x5a8,Count=25;
struct Snapshot {
    uint32_t rows=0,nodes=0;
    std::array<uint32_t,2> heads{};
    std::array<uint32_t,Count> addresses{};
    std::array<uint8_t,Count> indices{},positions{},seen{};
};
template<class Read> bool Capture(uint32_t rows,Read read,Snapshot& result) {
    result={};Snapshot value;value.rows=rows;
    auto get=[&](uint32_t address,auto& output) {
        return address>=0x10000 && address<=0x7fffffffu-sizeof(output) && read(address,&output,sizeof(output));
    };
    if(rows<0x10000 || rows>0x7fffffffu-0x8dcc)return false;
    uint8_t slots=0;
    if(!get(rows+0x8db8,slots) || slots!=4)return false;
    for(uint32_t index=0;index<Count;++index)
        if(!get(rows+index*RowSize+0x58,value.positions[index]))return false;
    for(unsigned list=0;list<2;++list) {
        const uint32_t field=rows+(list?0x8dc4:0x8dbc);
        uint32_t head=0,node=0,tail=0;
        if(!get(field,head) || head<0x10000 || head>0x7fffffffu-12
            || (list && value.heads[0]==head) || !get(head,node) || !get(head+4,tail))return false;
        value.heads[list]=head;uint32_t previous=head;
        while(node!=head) {
            if(value.nodes>=Count || node<0x10000 || node>0x7fffffffu-12
                || node==value.heads[0] || (list && node==value.heads[1]))return false;
            for(uint32_t i=0;i<value.nodes;++i)if(value.addresses[i]==node)return false;
            uint32_t next=0,back=0;uint8_t index=0;
            if(!get(node,next) || !get(node+4,back) || back!=previous || !get(node+8,index)
                || index>=Count || index>4 || value.seen[index] || value.positions[index]>4)return false;
            value.addresses[value.nodes]=node;value.indices[value.nodes++]=index;value.seen[index]=1;
            previous=node;node=next;
        }
        uint32_t current=0;
        if(tail!=previous || !get(field,current) || current!=head)return false;
    }
    if(!value.seen[1] || !value.seen[2] || !value.positions[1] || !value.positions[2]
        || (value.seen[0] && value.positions[0]))return false;
    for(uint32_t a=0;a<Count;++a)for(uint32_t b=a+1;b<Count;++b)
        if(value.seen[a] && value.seen[b] && value.positions[a] && value.positions[a]==value.positions[b])return false;
    result=value;return true;
}
inline bool Plan(const Snapshot& before,uint32_t target,uint32_t castle,Snapshot& after) {
    after={};
    if(!before.rows || target<1 || target>2 || castle<1 || castle>4 || !before.seen[target]
        || !before.positions[target] || before.positions[target]>4)return false;
    after=before;
    for(uint32_t index=0;index<Count;++index)
        if(before.seen[index] && before.positions[index]==castle)after.positions[index]=before.positions[target];
    after.positions[target]=static_cast<uint8_t>(castle);return true;
}
inline bool Equal(const Snapshot& a,const Snapshot& b) {
    return a.rows==b.rows && a.nodes==b.nodes && a.heads==b.heads && a.addresses==b.addresses
        && a.indices==b.indices && a.positions==b.positions && a.seen==b.seen;
}
}
