#pragma once
// Read only the two small saved-state members, never the native world payload.
// BAF v6 crypto/layout follows tools/lan/bba.py (S7Packer, MIT).
#include "neo_logistics_state.h"
#include <array>
#include <cstring>
#include <istream>
#include <stdexcept>
extern "C" int puff(unsigned char*, unsigned long*, const unsigned char*, unsigned long*);

namespace neo_save {
using Bytes = std::vector<unsigned char>;
constexpr size_t MaximumCache = 8 * 1024 * 1024;
inline void RequireAt(bool value,int line) { if (!value) throw std::runtime_error("Invalid NEO save archive at " + std::to_string(line)); }
#define Require(value) RequireAt((value),__LINE__)
inline uint32_t Word(const unsigned char* p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
inline void Put(unsigned char* p, uint32_t n) { for(unsigned i=0;i<4;++i)p[i]=static_cast<unsigned char>(n>>(i*8)); }
inline uint32_t Crc(const unsigned char* p, size_t n) {
    uint32_t c=~0u;
    for(size_t i=0;i<n;++i) { c^=p[i];for(unsigned k=0;k<8;++k)c=(c>>1)^(0xedb88320u & (0u-(c&1u))); }
    return ~c;
}
inline Bytes Decrypt(Bytes data, const std::array<uint32_t,4>& key, uint32_t delta, unsigned rounds) {
    Require(data.size()>=8 && data.size()%4==0);
    std::vector<uint32_t> v(data.size()/4);
    for(size_t i=0;i<v.size();++i)v[i]=Word(data.data()+i*4);
    uint32_t sum=rounds*delta,z=v[0];
    const auto mix=[&](uint32_t y,uint32_t zz,size_t p,unsigned e) {
        return (((y>>5 ^ zz<<2)+(y<<4 ^ zz>>3)) ^ ((sum^zz)+(key[(p&3)^e]^y)));
    };
    for(unsigned r=0;r<rounds;++r) {
        const unsigned e=(sum>>2)&3;
        for(size_t p=v.size()-1;p>0;--p) {v[p]-=mix(v[p-1],z,p,e);z=v[p];}
        v[0]-=mix(v.back(),z,0,e);z=v[0];sum-=delta;
    }
    for(size_t i=0;i<v.size();++i)Put(data.data()+i*4,v[i]);
    return data;
}
inline Bytes Gzip(Bytes data, uint32_t nameLength, size_t expected) {
    constexpr uint32_t x[]={68040748,166934560,1719695735,4247314248u};
    for(size_t at=0;at<data.size();at+=1048576) {
        Require(data.size()-at>=16);uint32_t w[4];for(unsigned i=0;i<4;++i)w[i]=Word(data.data()+at+i*4);
        w[3]^=w[2]^nameLength^x[3];w[2]^=w[1]^nameLength^x[2];
        w[1]^=w[0]^nameLength^x[1];w[0]^=w[3]^nameLength^x[0];
        for(unsigned i=0;i<4;++i)Put(data.data()+at+i*4,w[i]);
    }
    Require(expected<=MaximumCache && data.size()>=18 && data[0]==31 && data[1]==139 && data[2]==8 && !(data[3]&0xe0));
    size_t at=10;const size_t end=data.size()-8;
    if(data[3]&4) {Require(at+2<=end);const size_t n=data[at]+(size_t(data[at+1])<<8);at+=2;Require(n<=end-at);at+=n;}
    for(unsigned flag:{8u,16u})if(data[3]&flag) {while(at<end && data[at])++at;Require(at<end);++at;}
    if(data[3]&2) {Require(at+2<=end && (Crc(data.data(),at)&65535u)==(data[at]+(uint32_t(data[at+1])<<8)));at+=2;}
    Require(at<end && Word(data.data()+end+4)==expected);
    Bytes out(expected?expected:1);unsigned long have=static_cast<unsigned long>(out.size()),used=static_cast<unsigned long>(end-at);
    Require(puff(out.data(),&have,data.data()+at,&used)==0 && have==expected && used==end-at);
    Require(Crc(out.data(),have)==Word(data.data()+end));out.resize(have);return out;
}
struct Reader {
    std::istream& stream;uint64_t length;
    explicit Reader(std::istream& in):stream(in),length(0) {
        stream.seekg(0,std::ios::end);const auto n=stream.tellg();Require(n>=80);length=static_cast<uint64_t>(n);Require(length<=UINT32_MAX);
    }
    Bytes Read(uint64_t at,size_t n) {
        Require(n<=MaximumCache && at<=length && n<=length-at);Bytes b(n);
        stream.clear();stream.seekg(static_cast<std::streamoff>(at));stream.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(n));
        Require(stream.good());return b;
    }
    std::array<Bytes,2> Members() {
        const auto h=Read(0,16);Require(std::memcmp(h.data(),"BAF\6",4)==0);
        const auto hs=Word(h.data()+8);Require(hs==16 || hs==64);
        const auto head=Decrypt(Read(16,hs),{623092400,3578011951u,428883204,211416760},1231853475,7);
        const auto offset=Word(head.data()),high=Word(head.data()+4),size=Word(head.data()+8);
        Require(!high && offset>=16+hs && size>=276);auto encoded=Read(offset,size);
        Require(Crc(encoded.data(),encoded.size())==Word(head.data()+12));
        const auto dir=Decrypt(std::move(encoded),{892421091,3702197895u,3064602941u,189782668},1532281822,8);
        const auto namesEnd=Word(dir.data()+8);Require(Word(dir.data()+4)==272 && namesEnd>=276 && namesEnd<=dir.size()-4);
        const uint32_t count=Word(dir.data()+272);Require(count && count<=100000);
        size_t pos=276;std::array<Bytes,2> result;std::array<bool,2> found{};
        for(uint32_t i=0;i<count;++i) {
            Require(pos<=namesEnd && namesEnd-pos>=64);uint32_t v[16];for(unsigned j=0;j<16;++j)v[j]=Word(dir.data()+pos+j*4);
            const size_t n=v[12],padded=((n+3)&~size_t(3))+(n%4==0?4:0);
            Require(n && n<10000 && padded<=namesEnd-pos-64);
            std::string name(reinterpret_cast<const char*>(dir.data()+pos+64),n);while(!name.empty() && name.back()==0)name.pop_back();
            pos+=64+padded;
            const int target=name=="SaveGameCache.s7"?0:name=="SavegameInfo.xml"?1:-1;if(target<0)continue;
            Require(!found[target] && !v[1] && !v[5] && !v[7] && v[6]>=16+hs && v[2]<=MaximumCache && (target==0 || v[2]<=65536));
            // Like the existing BBA reader: the native writer leaves the stored
            // compressed CRC stale. Require gzip CRC, decoded CRC/size and directory CRC.
            auto packed=Read(v[6],v[8]);
            Bytes raw;if(v[4]==17)raw=Gzip(std::move(packed),v[12],v[2]);else {Require(v[4]==0);raw=std::move(packed);}
            Require(raw.size()==v[2] && Crc(raw.data(),raw.size())==v[3]);result[target]=std::move(raw);found[target]=true;
        }
        Require(pos==namesEnd && uint64_t(namesEnd)+4+uint64_t(Word(dir.data()+namesEnd))*8==dir.size());
        Require(found[0] && found[1]);return result;
    }
};
struct Cache {
    const Bytes& bytes;size_t at=0;
    void Tag(const char* text) {Require(bytes.size()-at>=4 && std::memcmp(bytes.data()+at,text,4)==0);at+=4;}
    uint32_t Number() {Require(bytes.size()-at>=4);const auto n=Word(bytes.data()+at);at+=4;return n;}
    std::string String() {Tag("Str_");const size_t n=Number();Require(n && n<=bytes.size()-at && bytes[at+n-1]==0);std::string out(reinterpret_cast<const char*>(bytes.data()+at),n-1);at+=n;return out;}
    std::string Neo(bool& found) {
        Tag("bin1");Require(Number()==1 && Number()==0 && Number()==1);Tag("Coll");Require(Number()==0);
        const auto count=Number();Require(count<=16384);found=false;std::string result;
        for(uint32_t i=0;i<count;++i) {Require(Number()==1);const auto key=String(),value=String();if(key=="weplay_neo_logistics_v1") {Require(!found);found=true;result=value;}}
        Tag("coll");Require(at==bytes.size());return result;
    }
};
// Parse the small serializer grammar, never execute save-supplied Lua source.
struct Pickle {
    const std::string& text;size_t at=0;
    void Space() {while(at<text.size() && (text[at]==' ' || text[at]=='\n' || text[at]=='\r' || text[at]=='\t'))++at;}
    void Token(const char* s) {Space();const size_t n=std::strlen(s);Require(text.compare(at,n,s)==0);at+=n;}
    std::string String() {
        Space();Require(at<text.size() && (text[at]=='"' || text[at]=='\''));const char quote=text[at++];std::string out;
        while(at<text.size() && text[at]!=quote) {
            unsigned char c=static_cast<unsigned char>(text[at++]);Require(c>=32);
            if(c=='\\') {
                Require(at<text.size());c=static_cast<unsigned char>(text[at++]);
                if(c>='0' && c<='9') {unsigned value=c-'0';for(unsigned i=1;i<3 && at<text.size() && text[at]>='0' && text[at]<='9';++i)value=value*10+unsigned(text[at++]-'0');Require(value<=255);c=static_cast<unsigned char>(value);}
                else if(c=='n' || c=='\n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';
                else if(c=='\r') {if(at<text.size() && text[at]=='\n')++at;c='\n';}
                else Require(c=='\\' || c=='"' || c=='\'');
            }
            Require(c!=0 && out.size()<neo_logistics::MaximumStateBytes);out.push_back(static_cast<char>(c));
        }
        Require(at<text.size() && text[at++]==quote);return out;
    }
    std::string Payload() {
        Token("return");Token("{");Token("{");bool version=false,payload=false;std::string result;
        for(unsigned i=0;i<2;++i) {
            Token("[");const auto key=String();Token("]");Token("=");
            if(key=="version") {Require(!version);version=true;Token("1");}
            else {Require(key=="payload" && !payload);payload=true;result=String();}
            Space();if(at<text.size() && text[at]==',')++at;else Require(i==1);
        }
        Token("}");Space();if(at<text.size() && text[at]==',')++at;Token("}");Space();Require(at==text.size() && version && payload);return result;
    }
};
inline uint32_t LogicTurn(const Bytes& info) {
    const std::string xml(info.begin(),info.end()),tag="<LogicTurn>";const auto begin=xml.find(tag);
    Require(begin!=std::string::npos && xml.find(tag,begin+1)==std::string::npos);
    size_t pos=begin+tag.size();int64_t turn=0;Require(neo_logistics::Number(xml,pos,'<',0,UINT32_MAX,turn) && xml.compare(pos,11,"/LogicTurn>")==0);
    return static_cast<uint32_t>(turn);
}
inline std::string Read(std::istream& input, bool enabled, bool* corrected=nullptr) {
    if(corrected)*corrected=false;
    const auto members=Reader(input).Members();bool present=false;const auto serialized=Cache{members[0]}.Neo(present);
    if(!present) {Require(!enabled);return "NEO0\n";}
    auto payload=Pickle{serialized}.Payload();neo_logistics::SavedState saved;Require(neo_logistics::Decode(payload,saved) && saved.enabled==enabled);
    if(enabled) {
        const auto turn=LogicTurn(members[1]);
        // Retain every birth/intent, including a request created at the next
        // replay turn. The native file's LogicTurn binds the loaded world.
        if(saved.logic_turn>=0)Require(saved.logic_turn==turn);
        else {
            Require(uint64_t(saved.turn)==uint64_t(turn)+1);
            if(corrected)*corrected=saved.turn!=turn;
            saved.logic_turn=turn;payload=neo_logistics::Encode(saved);Require(!payload.empty());
        }
    }
    return payload;
}
}

#undef Require
