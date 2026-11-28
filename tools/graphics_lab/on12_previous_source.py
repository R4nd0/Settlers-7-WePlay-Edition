"""CPU preparation of previous-position shaders from explicitly supplied HLSL.

The original current-frame program is not rewritten here. This module prepares
a separate position-only program using a previous-draw constant texture. It
does not establish draw identity, motion validity, or enable game TAA.
"""
from dataclasses import dataclass
import ctypes as C
import math
import os
import re
import struct

MAX_SOURCE=2*1024*1024
TEXTURE_WIDTH=288  # c[256], int4 i[16], bool b[16] in the first component.
PREFIX='S7TemporalPrevious'
IDENT=r'[A-Za-z_][A-Za-z0-9_]*'
TYPE=r'(?:float(?:[234](?:x[234])?)?|int[234]?|bool[234]?)'
DECL=re.compile(r'\b(?:(row_major|column_major)\s+)?('+TYPE+r')\s+('+IDENT+r')\s*(?:\[\s*('+IDENT+r'|\d+)\s*\])?\s*:\s*register\s*\(\s*([cib])(\d+)\s*\)\s*;')


def masked(source):
    """Mask comments and strings without moving any offsets or line breaks."""
    out=list(source);i=0
    while i<len(source):
        end=None
        if source.startswith('//',i):
            end=source.find('\n',i+2);end=len(source) if end<0 else end
        elif source.startswith('/*',i):
            end=source.find('*/',i+2)
            if end<0:raise ValueError('Unterminated HLSL comment.')
            end+=2
        elif source[i]=='"':
            end=i+1
            while end<len(source):
                if source[end]=='\\':end+=2;continue
                if source[end]=='"':end+=1;break
                end+=1
            else:raise ValueError('Unterminated HLSL string.')
        if end is not None:
            for at in range(i,end):
                if out[at]not in '\r\n':out[at]=' '
            i=end
        else:i+=1
    return ''.join(out)


def closing(text,start,left='[',right=']'):
    depth=0
    for at in range(start,len(text)):
        if text[at]==left:depth+=1
        elif text[at]==right:
            depth-=1
            if not depth:return at
    raise ValueError('Unterminated HLSL expression.')


@dataclass(frozen=True)
class Uniform:
    name:str
    kind:str
    bank:str
    register:int
    count:int
    array:bool
    row_major:bool

    @property
    def stride(self):
        if 'x'not in self.kind:return 1
        rows,columns=map(int,self.kind[5:].split('x'))
        return rows if self.row_major else columns

    def expression(self,index='0'):
        offset={'c':0,'i':256,'b':272}[self.bank]
        base=f'({offset+self.register}+({index})*{self.stride})'
        def read(n,components):
            return f'{PREFIX}Read({base}+{n}).{components}'
        if 'x'in self.kind:
            rows,columns=map(int,self.kind[5:].split('x'))
            major,minor=(rows,columns) if self.row_major else (columns,rows)
            matrix=f'float{major}x{minor}('+','.join(read(n,'xyzw'[:minor]) for n in range(major))+')'
            return matrix if self.row_major else f'transpose({matrix})'
        width=int(self.kind[-1]) if self.kind[-1].isdigit() else 1
        return f'(({self.kind}){read(0,"xyzw"[:width])})'


def prepare(source,entry,*,sampler=3):
    """Consume preprocessed HLSL; caller must verify it against original assets."""
    if not isinstance(source,str) or not 0<len(source.encode('utf-8'))<=MAX_SOURCE or not re.fullmatch(IDENT,entry) or type(sampler)is not int or not 0<=sampler<4:
        raise ValueError('Invalid previous-position source request.')
    clean=masked(source)
    if re.search(r'\b'+PREFIX,clean):raise ValueError('Temporal helper name collision.')
    # No implicit file reads or unresolved conditional compilation in the
    # generated shader. Preprocessing uses an explicit include provider below.
    for line in clean.splitlines():
        if line.lstrip().startswith('#') and not re.match(r'\s*#\s*(?:line\b|pragma\b|\d)',line):
            raise ValueError('Preprocess the complete HLSL include graph first.')
    def top_level(at):return clean[:at].count('{')==clean[:at].count('}')
    dimensions={}
    for constant in re.finditer(r'\bstatic\s+const\s+int\s+('+IDENT+r')\s*=\s*(\d+)\s*;',clean):
        if top_level(constant.start()):
            if constant[1]in dimensions:raise ValueError('Duplicate global array extent.')
            dimensions[constant[1]]=int(constant[2])
    declarations=list(DECL.finditer(clean));uniforms={};body=list(source)
    for match in declarations:
        if not top_level(match.start()):raise ValueError('Expected global uniform register bindings.')
        layout,kind,name,count,bank,register=match.groups()
        if name in uniforms:raise ValueError('Duplicate uniform declaration.')
        size=int(count) if count and count.isdigit() else dimensions.get(count,0) if count else 1
        if not 1<=size<=256:raise ValueError('Unsupported uniform array extent.')
        if layout is None:
            directives=re.findall(r'#\s*pragma\s+pack_matrix\s*\(\s*(row_major|column_major)\s*\)',clean[:match.start()])
            layout=directives[-1] if directives else 'column_major'
        uniform=Uniform(name,kind,bank,int(register),size,count is not None,layout=='row_major')
        if (bank=='c'and not kind.startswith('float') or bank=='i'and not kind.startswith('int') or bank=='b'and kind!='bool'
                or uniform.register+uniform.stride*size>({'c':256,'i':16,'b':16}[bank])):
            raise ValueError('Uniform register layout is unsupported.')
        uniforms[name]=uniform
        for at in range(match.start(),match.end()):
            if body[at]not in '\r\n':body[at]=' '
    if not uniforms:raise ValueError('No explicit uniform bindings.')
    body=''.join(body);body_mask=masked(body)
    if re.search(r'\bregister\s*\(\s*[cib]',body_mask):raise ValueError('Unconverted uniform register declaration.')
    for name in uniforms:
        if re.search(r'\b'+TYPE+r'\s+'+re.escape(name)+r'\b',body_mask):raise ValueError('A local declaration shadows a uniform.')
    def rewrite(text,depth=0):
        if depth>32:raise ValueError('Uniform index nesting exceeds the bound.')
        mask=masked(text);parts=[];cursor=0
        for token in re.finditer(IDENT,mask):
            if token.start()<cursor or token[0]not in uniforms:continue
            uniform=uniforms[token[0]];end=token.end();index='0'
            if uniform.array:
                while end<len(mask) and mask[end].isspace():end+=1
                if end==len(mask) or mask[end]!='[':raise ValueError('Uniform arrays require an explicit element access.')
                close=closing(mask,end);index='(int)('+rewrite(text[end+1:close],depth+1)+')';end=close+1
            parts.extend((text[cursor:token.start()],uniform.expression(index)));cursor=end
        return ''.join(parts)+text[cursor:]
    signature=re.search(r'\b('+IDENT+r')\s+'+re.escape(entry)+r'\s*\(\s*(?:in\s+)?('+IDENT+r')\s+('+IDENT+r')\s*\)',body_mask)
    if not signature:raise ValueError('Expected one structured vertex entry input.')
    output,input_type,_=signature.groups()
    definition=re.search(r'\bstruct\s+'+re.escape(output)+r'\s*\{',body_mask)
    if not definition:raise ValueError('Missing vertex output structure.')
    end=closing(body_mask,definition.end()-1,'{','}')
    positions=re.findall(r'\bfloat4\s+('+IDENT+r')\s*:\s*POSITION0?\s*;',body_mask[definition.end():end])
    if len(positions)!=1:raise ValueError('Expected exactly one float4 POSITION0 output.')
    generated=(f'sampler2D {PREFIX}Constants : register(s{sampler});\n'
        f'float4 {PREFIX}Read(int index){{if(index<0||index>={TEXTURE_WIDTH})return 0;'
        f'return tex2Dlod({PREFIX}Constants,float4((index+0.5)/{TEXTURE_WIDTH}.0,0.5,0,0));}}\n'
        +rewrite(body)+f'\nfloat4 {PREFIX}Position({input_type} input):POSITION0{{return {entry}(input).{positions[0]};}}\n')
    if len(generated.encode('utf-8'))>MAX_SOURCE*4:raise ValueError('Generated source exceeds the bound.')
    return generated,tuple(uniforms.values())


def constant_texture(floats,integers,booleans):
    if len(floats)!=256 or len(integers)!=16 or len(booleans)!=16:raise ValueError('Expected the complete previous VS constant banks.')
    values=[]
    for row in floats:
        if len(row)!=4 or any(type(x)not in(int,float) or not math.isfinite(x) for x in row):raise ValueError('Non-finite previous float constant.')
        values.extend(row)
    for row in integers:
        if len(row)!=4 or any(type(x)is not int or abs(x)>16777216 for x in row):raise ValueError('Previous integer constants lose float32 precision.')
        values.extend(row)
    for value in booleans:
        if type(value)is not bool:raise ValueError('Expected previous boolean constants.')
        values.extend((float(value),0,0,0))
    try:return struct.pack('<'+str(TEXTURE_WIDTH*4)+'f',*values)
    except (OverflowError,struct.error)as error:raise ValueError('Previous constants exceed float32.')from error


def compiler():
    if os.name!='nt':raise RuntimeError('The CPU shader compiler requires Windows.')
    k=C.WinDLL('kernel32',use_last_error=True);k.GetWindowsDirectoryW.argtypes=[C.c_wchar_p,C.c_uint];k.GetWindowsDirectoryW.restype=C.c_uint
    path=C.create_unicode_buffer(512);length=k.GetWindowsDirectoryW(path,512)
    if not length or length>=512:raise RuntimeError('Windows directory unavailable.')
    return C.WinDLL(path.value+'/System32/d3dcompiler_47.dll')


def blob_bytes(blob):
    if not blob.value:return b''
    table=C.cast(blob,C.POINTER(C.POINTER(C.c_void_p))).contents
    try:
        pointer=C.WINFUNCTYPE(C.c_void_p,C.c_void_p)(table[3])(blob)
        size=C.WINFUNCTYPE(C.c_size_t,C.c_void_p)(table[4])(blob)
        if size>MAX_SOURCE*4:raise ValueError('Compiler output exceeds the bound.')
        return C.string_at(pointer,size)
    finally:C.WINFUNCTYPE(C.c_ulong,C.c_void_p)(table[2])(blob)


def compile_vertex(source,entry):
    raw=source.encode('utf-8')
    if not 0<len(raw)<=MAX_SOURCE*4 or not re.fullmatch(IDENT,entry):raise ValueError('Invalid generated HLSL request.')
    # Includes are deliberately disabled. No compiler filesystem reads.
    function=compiler().D3DCompile
    function.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p,C.c_void_p,C.c_void_p,C.c_char_p,C.c_char_p,C.c_uint,C.c_uint,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)]
    function.restype=C.c_long;code=C.c_void_p();errors=C.c_void_p();buffer=C.create_string_buffer(raw)
    hr=function(buffer,len(raw),b'authored-temporal-previous',None,None,entry.encode('ascii'),b'vs_3_0',1|(1<<15),0,C.byref(code),C.byref(errors))
    result=blob_bytes(code);diagnostic=blob_bytes(errors).decode('utf-8',errors='replace')
    if hr<0:raise ValueError('Previous-position shader compilation failed: '+diagnostic[:4000])
    if not result or len(result)>65536:raise ValueError('Invalid previous-position program size.')
    return result


def compile_previous(source):
    result=compile_vertex(source,PREFIX+'Position')
    from tools.material_shaders.semantics import reflect
    bindings=reflect(result)
    helper=[b for b in bindings if b['name']==PREFIX+'Constants']
    if (len(helper)!=1 or helper[0]['register_set']!='sampler' or helper[0]['register_count']!=1 or not 0<=helper[0]['register_index']<4
            or any(b['register_set']!='sampler' for b in bindings)):
        raise ValueError('The previous-position program still reads external current-frame constants.')
    return result


def preprocess(source,name,include):
    """Preprocess via an explicit byte provider; never fall back to disk paths."""
    if not isinstance(source,bytes) or not 0<len(source)<=MAX_SOURCE:raise ValueError('Invalid HLSL root source.')
    from tools.material_shaders.inventory import safe_name
    from pathlib import PurePosixPath
    safe_name(name);buffers={};failures=[];include_count=0;include_bytes=0
    open_type=C.WINFUNCTYPE(C.c_long,C.c_void_p,C.c_uint,C.c_char_p,C.c_void_p,C.POINTER(C.c_void_p),C.POINTER(C.c_uint))
    close_type=C.WINFUNCTYPE(C.c_long,C.c_void_p,C.c_void_p)
    @open_type
    def opened(_self,kind,file,parent,data,size):
        nonlocal include_count,include_bytes
        try:
            if kind!=0 or not file:raise ValueError('Only local explicit HLSL includes are supported.')
            relative=file.decode('ascii');safe_name(relative)
            parent_name=buffers[parent][1] if parent in buffers else name
            resource=str(PurePosixPath(parent_name).parent/relative);safe_name(resource)
            raw=include(resource)
            if not isinstance(raw,bytes) or not 0<len(raw)<=MAX_SOURCE or len(buffers)>=64:raise ValueError('HLSL include bound exceeded.')
            include_count+=1;include_bytes+=len(raw)
            if include_count>2048 or include_bytes>MAX_SOURCE*4:raise ValueError('HLSL include work bound exceeded.')
            buffer=C.create_string_buffer(raw);address=C.addressof(buffer);buffers[address]=(buffer,resource)
            data[0]=address;size[0]=len(raw);return 0
        except Exception as error:
            failures.append(str(error));return -2147467259
    @close_type
    def closed(_self,data):
        if data not in buffers:return -2147467259
        del buffers[data];return 0
    table=(C.c_void_p*2)(C.cast(opened,C.c_void_p),C.cast(closed,C.c_void_p))
    instance=(C.POINTER(C.c_void_p)*1)(C.cast(table,C.POINTER(C.c_void_p)))
    function=compiler().D3DPreprocess
    function.argtypes=[C.c_void_p,C.c_size_t,C.c_char_p,C.c_void_p,C.c_void_p,C.POINTER(C.c_void_p),C.POINTER(C.c_void_p)];function.restype=C.c_long
    code=C.c_void_p();errors=C.c_void_p();root=C.create_string_buffer(source)
    hr=function(root,len(source),name.encode('ascii'),None,instance,C.byref(code),C.byref(errors))
    result=blob_bytes(code);diagnostic=blob_bytes(errors).decode('utf-8',errors='replace')
    if failures or hr<0:raise ValueError('HLSL preprocessing failed: '+'; '.join(failures)+diagnostic[:3000])
    if not result or len(result)>MAX_SOURCE:raise ValueError('Preprocessed HLSL size is invalid.')
    return result.rstrip(b'\0').decode('utf-8')
