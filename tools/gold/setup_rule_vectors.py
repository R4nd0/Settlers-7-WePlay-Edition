"""Synthetic nonempty enclosing-rule fixtures; never reads a game process."""
import struct
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_ECX, UC_X86_REG_ESP, UC_X86_REG_EIP

# Independently located, including Steam's different catalogue-lookup offset.
METHODS = {
    'gold': ((0x9ce320,0xc0),(0x9ce3f0,0x3b0),(0x9cdab0,0x50),
             (0x429470,0x100),(0x4281d0,0x100),(0x415e00,0xa00)),
    'ubisoft': ((0x5f5240,0xc0),(0x5f5310,0x480),(0x5ef990,0x50),
                (0x4f30b0,0x100),(0x4f2230,0x100),(0x4e2300,0x800)),
    'steam_analysis': ((0x5f4da0,0xc0),(0x5f4e70,0x480),(0x5ef3f0,0x50),
                       (0x4f2d30,0x100),(0x4f1eb0,0x100),(0x4e2100,0x800)),
}


def populate(u, image, root, stack, stop, owned, string, vector, words, word, allocate, free, copy):
    gold = image.label == 'gold'
    if set(image.crt_imports) != {b'malloc', b'free', b'memcpy', b'memmove'}:
        raise RuntimeError('Pinned CRT import contract differs')
    for name, address in image.crt_imports.items():
        words(address, allocate if name == b'malloc' else free if name == b'free' else copy)
    profile = owned(bytes(0x200))
    catalogue = owned(bytes(0xb08 if gold else 0xa7c))
    catalogue_rows = owned(bytes(3*(0x98 if gold else 0xa0)))
    sector_table = owned(bytes(0x24))
    global_pointer = {'gold':0x10f835c, 'ubisoft':0x13a5214, 'steam_analysis':0x13a2304}[image.label]
    words(global_pointer, profile)
    words(profile+(0x188 if gold else 0x148), catalogue)
    words(profile+(0x1a8 if gold else 0x168), sector_table)
    words(catalogue+(0xb04 if gold else 0xa78), catalogue_rows)

    def original_hash(text):
        pointer = owned(text.encode('ascii')+b'\0')

        def finish(uc, at, size, data):
            if at == stop:
                uc.emu_stop()

        def write(uc, access, at, size, value, data):
            if not stack <= at <= at+size <= stack+0x10000:
                raise RuntimeError('Original CString hash changed non-stack storage')

        hooks = (u.hook_add(UC_HOOK_CODE, finish), u.hook_add(UC_HOOK_MEM_WRITE, write))
        sp = stack+0xe000
        words(sp, stop)
        u.reg_write(UC_X86_REG_ECX, pointer);u.reg_write(UC_X86_REG_ESP, sp)
        try:
            u.emu_start(METHODS[image.label][4][0], stop+1, count=100000, timeout=3000000)
        finally:
            for hook in hooks:
                u.hook_del(hook)
        value = u.reg_read(UC_X86_REG_EAX)
        if u.reg_read(UC_X86_REG_EIP) != stop or u.reg_read(UC_X86_REG_ESP) != sp+4 or not value:
            raise RuntimeError('Incomplete original CString hash')
        return value

    names = owned(bytes(3*16))
    keys = ('', 'Synthetic Victory One', 'Synthetic Victory Two')
    for i, key in enumerate(keys):
        string(names+16*i, key)
    array = owned(struct.pack('<3I', names, names+16, names+32))
    registry = owned(struct.pack('<4I', array, 0, 3, 3))
    buckets = owned(bytes(4*8));table = owned(struct.pack('<2I', buckets, 3))
    for i, key in enumerate(keys[1:], 1):
        pin = original_hash(key);bucket = pin&3
        for _ in range(4):
            if not word(buckets+bucket*8):
                break
            bucket = (bucket+1)&3
        else:
            raise RuntimeError('Synthetic hash table overflow')
        words(buckets+bucket*8, i, pin)
    words(catalogue+(0xac0 if gold else 0xa38)+0x18, registry, table)
    # Independently exercise the original Type3 suppression branch.
    words(catalogue_rows+(0x98 if gold else 0xa0)*2+(0x80 if gold else 0x88), 3)
    sector_names = owned(bytes(3*16))
    for i, key in enumerate(('', 'Synthetic Sector Type', 'Synthetic Sector Name')):
        string(sector_names+16*i, key)
    sector_array = owned(struct.pack('<3I', sector_names, sector_names+16, sector_names+32))
    sector_registry = owned(struct.pack('<4I', sector_array, 0, 3, 3))
    words(sector_table+(4 if gold else 8)+0x18, sector_registry)
    victories = owned(bytes(2*0x44))
    for i, key in enumerate(keys[1:]):
        item = victories+i*0x44;string(item, key)
        for offset, value in ((0x10,'Synthetic Caption '+str(i)), (0x20,'Synthetic Icon '+str(i)), (0x30,'Synthetic Hint '+str(i))):
            string(item+offset, value)
        words(item+0x40, 7+i)
    words(root+(0x38 if gold else 0x34), victories, victories+2*0x44, victories+2*0x44)
    stride = 0x18 if gold else 0x14
    sectors = owned(bytes(2*stride))
    for i in range(2):
        item = sectors+i*stride;u.mem_write(item, bytes([i+1,i+2]));words(item+4, 1)
        vector(item+(0xc if gold else 8), struct.pack('<4I', 2,7+i,2,9+i))
    words(root+(0x58 if gold else 0x4c), sectors, sectors+2*stride, sectors+2*stride)
