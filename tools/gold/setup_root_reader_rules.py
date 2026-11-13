"""Owned nonempty catalogue fixture for the original enclosing reader audit."""
import struct
from setup_rule_vectors import populate


def prepare(u,image,root,stack,stop,owned,string,words,word,allocate,free,copy,original_hash):
    gold=image.label=='gold'

    def vector(at,data):
        pointer=owned(data);words(at,pointer,pointer+len(data),pointer+len(data))

    def sector_owned(data):
        # Reader lookup also needs the hash-table pointer after the registry;
        # the older writer-only fixture never dereferences this extra word.
        return owned(data+bytes(4) if len(data)==0x24 else data)

    populate(u,image,root,stack,stop,sector_owned,string,vector,words,word,allocate,free,copy)
    pointer={'gold':0x10f835c,'ubisoft':0x13a5214,'steam_analysis':0x13a2304}[image.label]
    profile=word(pointer);catalogue=word(profile+(0x1a8 if gold else 0x168))
    symbols=catalogue+(4 if gold else 8);entries=word(word(symbols+0x18))
    buckets=owned(bytes(32));table=owned(struct.pack('<2I',buckets,3))
    for index in (1,2):
        symbol=word(entries+index*4);value=original_hash(word(symbol));words(symbol+12,value)
        bucket=value&3
        for _ in range(4):
            if not word(buckets+8*bucket):break
            bucket=(bucket+1)&3
        else:raise RuntimeError('Sector fixture hash table full')
        words(buckets+8*bucket,index,value)
    words(symbols+0x1c,table)
    victories=word(root+(0x38 if gold else 0x34))
    sectors=word(root+(0x58 if gold else 0x4c));stride=0x18 if gold else 0x14
    mutable={victories,sectors}
    for index in range(2):
        at=victories+index*0x44;words(at+12,original_hash(word(at)))
        for field in range(4):mutable.add(word(at+field*16))
        mutable.add(word(sectors+index*stride+(0xc if gold else 8)))
    return {'mutable_initial_blocks':mutable,'sector_stride':stride,
            'source_victories':2,'decoded_victories':1,'sector_rules':2}
