"""Owned populated player containers for the original enclosing reader audit."""
import struct


def prepare(u,gold,rows,stride,owned,string,words,word,read_owned):
    mutable=set();masks=[];row=rows+stride

    def text(at,value):
        string(at,value);mutable.add(word(at));masks.append((at-rows,16))

    def vector(at,data):
        ptr=owned(data);words(at,ptr,ptr+len(data),ptr+len(data))
        mutable.add(ptr);masks.append((at-rows,12))

    def linked(at,values):
        head=word(at);mutable.add(head)
        nodes=[owned(struct.pack('<IIB3x',0,0,x)) for x in values];mutable.update(nodes)
        for i,node in enumerate(nodes):
            words(node,nodes[i+1] if i+1<len(nodes) else head,nodes[i-1] if i else head)
        words(head,nodes[0],nodes[-1]);words(at,head,len(nodes));masks.append((at-rows,8))

    text(row+0x1c,'Synthetic Player')
    vector(row+(0x78 if gold else 0x74),struct.pack('<2I',17,23))
    vector(row+(0x52c if gold else 0x51c),struct.pack('<6I',81,82,83,91,92,93))
    vector(row+(0x558 if gold else 0x544),b'\1\0\1')
    list_offsets=(0x4e8,0x4f4,0x56c) if gold else (0x4e0,0x4e8,0x554)
    for off in list_offsets:linked(row+off,(1,4))
    # Original list-read finalization rebuilds this item-state cache. Gold
    # writes at 0x520141/0x520164; Ubisoft at 0x58e3bf/0x58e3e3;
    # Steam at 0x58de7f/0x58dea3. Keep the source consistent with its lists.
    for value in (1,4):words(row+(0xe4 if gold else 0xe0)+4*value,2)
    castle=row+(0x574 if gold else 0x55c)
    text(castle+(0 if gold else 4),'Synthetic Castle')
    text(castle+(0x10 if gold else 0x14),'Synthetic Type')
    parts=owned(bytes(32));mutable.add(parts)
    string(parts,'Synthetic Part A');string(parts+16,'Synthetic Part B')
    mutable.update((word(parts),word(parts+16)))
    words(castle+0x24,parts,parts+32,parts+32);masks.append((castle+0x24-rows,12))
    vector(castle+(0x34 if gold else 0x30),struct.pack('<2i',-1,0))
    vector(castle+(0x44 if gold else 0x3c),struct.pack('<2i',2,5))
    position_offsets=(0x9080,0x908c) if gold else (0x8dbc,0x8dc4)
    for off,values in zip(position_offsets,((0,1),(2,24))):linked(rows+off,values)

    def verify():
        reachable=set()

        def txt(at,expected):
            ptr,cap,n,_=struct.unpack('<4I',read_owned(at,16))
            if n!=len(expected) or cap<=n or read_owned(ptr,n+1)!=expected.encode()+b'\0':
                raise RuntimeError('Decoded player/Castle string differs')
            reachable.add(ptr)

        def vec(at,expected):
            begin,end,cap=struct.unpack('<3I',read_owned(at,12))
            if end-begin!=len(expected) or cap<end or read_owned(begin,len(expected))!=expected:
                raise RuntimeError('Decoded player/Castle vector differs')
            reachable.add(begin)

        def lst(at,expected):
            head,n=struct.unpack('<2I',read_owned(at,8))
            if n!=len(expected):raise RuntimeError('Decoded list size differs')
            reachable.add(head);node=struct.unpack('<I',read_owned(head,4))[0];previous=head
            for value in expected:
                if node==head:raise RuntimeError('Decoded list ended early')
                next_node,back,item=struct.unpack('<IIB3x',read_owned(node,12))
                if back!=previous or item!=value:raise RuntimeError('Decoded list value/link differs')
                reachable.add(node);previous,node=node,next_node
            if node!=head or struct.unpack('<I',read_owned(head+4,4))[0]!=previous:
                raise RuntimeError('Decoded list sentinel differs')

        txt(row+0x1c,'Synthetic Player')
        vec(row+(0x78 if gold else 0x74),struct.pack('<2I',17,23))
        vec(row+(0x52c if gold else 0x51c),struct.pack('<6I',81,82,83,91,92,93))
        for off in list_offsets:lst(row+off,(1,4))
        txt(castle+(0 if gold else 4),'Synthetic Castle')
        txt(castle+(0x10 if gold else 0x14),'Synthetic Type')
        begin,end,cap=struct.unpack('<3I',read_owned(castle+0x24,12))
        if end-begin!=32 or cap<end:raise RuntimeError('Decoded Castle part count differs')
        reachable.add(begin);txt(begin,'Synthetic Part A');txt(begin+16,'Synthetic Part B')
        vec(castle+(0x34 if gold else 0x30),struct.pack('<2i',-1,0))
        vec(castle+(0x44 if gold else 0x3c),struct.pack('<2i',2,5))
        for off,values in zip(position_offsets,((0,1),(2,24))):lst(rows+off,values)
        return reachable

    return {'mutable_initial_blocks':mutable,'normalized_ranges':masks,'verify':verify}
