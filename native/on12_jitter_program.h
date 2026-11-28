#pragma once
#include <d3d9.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstring>

namespace On12Graphics {
struct JitterProgram {std::vector<DWORD> words;UINT constant=0,temporary=0,position=0;};
namespace JitterDetail {
inline DWORD Type(DWORD value){return ((value&D3DSP_REGTYPE_MASK)>>D3DSP_REGTYPE_SHIFT)|((value&D3DSP_REGTYPE_MASK2)>>D3DSP_REGTYPE_SHIFT2);}
inline DWORD Register(DWORD type,UINT index){return 0x80000000u|((type<<D3DSP_REGTYPE_SHIFT)&D3DSP_REGTYPE_MASK)|((type<<D3DSP_REGTYPE_SHIFT2)&D3DSP_REGTYPE_MASK2)|index;}
inline bool Destination(DWORD op){
    switch(op){case D3DSIO_MOV:case D3DSIO_ADD:case D3DSIO_SUB:case D3DSIO_MAD:case D3DSIO_MUL:case D3DSIO_RCP:case D3DSIO_RSQ:
    case D3DSIO_DP3:case D3DSIO_DP4:case D3DSIO_MIN:case D3DSIO_MAX:case D3DSIO_SLT:case D3DSIO_SGE:case D3DSIO_EXP:case D3DSIO_LOG:
    case D3DSIO_LIT:case D3DSIO_DST:case D3DSIO_LRP:case D3DSIO_FRC:case D3DSIO_M4x4:case D3DSIO_M4x3:case D3DSIO_M3x4:case D3DSIO_M3x3:case D3DSIO_M3x2:
    case D3DSIO_POW:case D3DSIO_CRS:case D3DSIO_SGN:case D3DSIO_ABS:case D3DSIO_NRM:case D3DSIO_SINCOS:case D3DSIO_MOVA:
    case D3DSIO_CMP:case D3DSIO_DP2ADD:case D3DSIO_SETP:case D3DSIO_TEXLDL:return true;default:return false;}
}
inline bool Control(DWORD op){
    switch(op){case D3DSIO_NOP:case D3DSIO_LOOP:case D3DSIO_ENDLOOP:case D3DSIO_REP:case D3DSIO_ENDREP:
    case D3DSIO_IF:case D3DSIO_IFC:case D3DSIO_ELSE:case D3DSIO_ENDIF:case D3DSIO_BREAK:case D3DSIO_BREAKC:case D3DSIO_BREAKP:return true;default:return false;}
}
inline bool Bindings(const unsigned char* data,size_t bytes,std::array<bool,256>& constants,bool& scene){
    auto u32=[&](size_t at){DWORD value=0;if(at<=bytes&&bytes-at>=4)std::memcpy(&value,data+at,4);return value;};
    auto u16=[&](size_t at){WORD value=0;if(at<=bytes&&bytes-at>=2)std::memcpy(&value,data+at,2);return value;};
    if(bytes<28||u32(0)!=28||u32(8)!=D3DVS_VERSION(3,0))return false;
    const auto count=u32(12),offset=u32(16);if(count>256||offset<28||offset>bytes||count>(bytes-offset)/20)return false;
    for(DWORD i=0;i<count;++i){const size_t entry=offset+static_cast<size_t>(i)*20;
        const DWORD name=u32(entry),type=u32(entry+12);const WORD set=u16(entry+4),start=u16(entry+6),size=u16(entry+8);
        if(name<28||name>=bytes||type<28||type>bytes||bytes-type<16||set>3||!size)return false;
        size_t end=name;while(end<bytes&&end-name<128&&data[end])++end;if(end==bytes||end-name==128)return false;
        if(set==2){if(start>=256||size>256-start)return false;const UINT end_register=static_cast<UINT>(start)+size;for(UINT c=start;c<end_register;++c)constants[c]=true;}
        const auto equal=[&](const char* text){const size_t n=std::strlen(text);return n==end-name&&!std::memcmp(data+name,text,n);};
        const bool projection=(start==12&&equal("g_VSMainViewProjectionMatrix"))||(start==64&&equal("g_View2ClipTM"));
        if(projection){if(set!=2||size!=4||u16(type)!=3||u16(type+2)!=3||u16(type+4)!=4||u16(type+6)!=4||u16(type+8)!=1||u16(type+10)!=0)return false;scene=true;}
    }return true;
}
}
// Redirect only POSITION0 writes to one unused temporary, then apply clip-space
// jitter. Uniforms and all other outputs stay byte-identical. Original bytecode
// is never modified. A free constant is found from CTAB ranges AND instructions.
// Unsupported envelopes, early returns and ambiguous declarations are rejected.
inline bool MakeJitterProgram(const void* bytes,size_t size,JitterProgram& output){
    output={};if(!bytes||size<16||size>65536||size%4)return false;
    std::vector<DWORD> words(size/4);std::memcpy(words.data(),bytes,size);
    if(words.front()!=D3DVS_VERSION(3,0)||words.back()!=D3DSIO_END)return false;
    std::array<bool,32> temporaries{};std::array<bool,256> constants{};
    std::vector<size_t> writes;bool scene=false,table=false,position_found=false;UINT position=0;DWORD written=0;
    for(size_t at=1;at<words.size();){const DWORD instruction=words[at],op=instruction&D3DSI_OPCODE_MASK;
        if(op==D3DSIO_END){if(at!=words.size()-1) return false;break;}
        if(op==D3DSIO_COMMENT){const size_t count=(instruction>>16)&0x7fff;if(!count||count>words.size()-at-1)return false;
            if(words[at+1]==0x42415443){if(table||count<8)return false;table=true;
                if(!JitterDetail::Bindings(reinterpret_cast<const unsigned char*>(words.data()+at+2),(count-1)*4,constants,scene))return false;}
            at+=1+count;continue;
        }
        const UINT count=(instruction&D3DSI_INSTLENGTH_MASK)>>D3DSI_INSTLENGTH_SHIFT;
        if(count>words.size()-at-1||(instruction&0x50000000u))return false;
        const bool declaration=op==D3DSIO_DCL,definition=op==D3DSIO_DEF||op==D3DSIO_DEFI||op==D3DSIO_DEFB;
        if(!declaration&&!definition&&!JitterDetail::Destination(op)&&!JitterDetail::Control(op))return false;
        if(declaration){if(count!=2)return false;const DWORD reg=words[at+2];
            if(JitterDetail::Type(reg)==D3DSPR_OUTPUT&&(words[at+1]&0x000f000fu)==D3DDECLUSAGE_POSITION){
                if(position_found||(reg&D3DSP_REGNUM_MASK)>=12)return false;position_found=true;position=reg&D3DSP_REGNUM_MASK;
            }
        }
        if(definition&&count!=(op==D3DSIO_DEFB?2u:5u))return false;
        for(UINT n=1;n<=count;++n){if((declaration&&n==1)||(definition&&n>1))continue;
            const DWORD parameter=words[at+n],type=JitterDetail::Type(parameter);const UINT index=parameter&D3DSP_REGNUM_MASK;
            if(!(parameter&0x80000000u))return false;
            if(type==D3DSPR_TEMP){if(index>=temporaries.size())return false;temporaries[index]=true;}
            if(type==D3DSPR_CONST){if(index>=constants.size())return false;constants[index]=true;}
            if(type==D3DSPR_CONST2||type==D3DSPR_CONST3||type==D3DSPR_CONST4)return false;
            if(!declaration&&type==D3DSPR_OUTPUT&&position_found&&index==position){
                if(n!=1||!JitterDetail::Destination(op))return false;writes.push_back(at+n);written|=parameter&D3DSP_WRITEMASK_ALL;
            }
        }at+=1+count;
    }
    if(!table||!scene||!position_found||writes.empty()||written!=D3DSP_WRITEMASK_ALL)return false;
    UINT temporary=32,constant=256;for(UINT n=32;n;--n)if(!temporaries[n-1]){temporary=n-1;break;}
    for(UINT n=256;n;--n)if(!constants[n-1]){constant=n-1;break;}if(temporary==32||constant==256)return false;
    for(size_t at:writes)words[at]=(words[at]&~(D3DSP_REGTYPE_MASK|D3DSP_REGTYPE_MASK2|D3DSP_REGNUM_MASK))|JitterDetail::Register(D3DSPR_TEMP,temporary);
    words.pop_back();const DWORD dst=JitterDetail::Register(D3DSPR_OUTPUT,position),temp=JitterDetail::Register(D3DSPR_TEMP,temporary);
    words.insert(words.end(),{D3DSIO_MAD|(4u<<D3DSI_INSTLENGTH_SHIFT),dst|D3DSP_WRITEMASK_0|D3DSP_WRITEMASK_1,
        temp|0x00ff0000u,JitterDetail::Register(D3DSPR_CONST,constant)|D3DSP_NOSWIZZLE,temp|D3DSP_NOSWIZZLE,
        D3DSIO_MOV|(2u<<D3DSI_INSTLENGTH_SHIFT),dst|D3DSP_WRITEMASK_2|D3DSP_WRITEMASK_3,temp|D3DSP_NOSWIZZLE,D3DSIO_END});
    output.words=std::move(words);output.constant=constant;output.temporary=temporary;output.position=position;return true;
}
}
