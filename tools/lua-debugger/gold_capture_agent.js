'use strict';
const game=Process.getModuleByName('Settlers7R.exe');
const norm=s=>s.replace(/\\/g,'/').toLowerCase();
if(Process.arch!=='ia32'||Process.pointerSize!==4||norm(game.path)!==norm(CONFIG.path)||CONFIG.identity.version!=='gold')throw Error('gold-target-identity');
const at=va=>game.base.add(va-0x400000);
const pe=game.base.add(game.base.add(0x3c).readU32());
if(pe.readU32()!==0x4550||pe.add(4).readU16()!==0x14c||pe.add(8).readU32()!==CONFIG.identity.timestamp||game.size!==CONFIG.identity.imageSize)throw Error('gold-pe-identity');
for(const guard of CONFIG.guards){
  const actual=Array.from(new Uint8Array(at(guard.address).readByteArray(guard.bytes.length/2)));
  const expected=guard.bytes.match(/../g).map(x=>parseInt(x,16));
  for(const r of guard.relocations){const word=(r.word+game.base.sub(ptr(0x400000)).toUInt32())>>>0;
    for(let i=0;i<4;i++)if(r.offset+i>=0&&r.offset+i<expected.length)expected[r.offset+i]=(word>>>(8*i))&255;}
  if(actual.some((b,i)=>b!==expected[i]))throw Error('gold-native-code-guard');
}
const policy=new GoldMarkerPolicy(CONFIG.markers),states=new Map(),listeners=[];
const markerPrefix='WEPLAY_GOLD_CAPTURE_V1 ';
const counts={console:0,log:0,no_state:0,bad_frame:0,non_string:0,bad_string:0,not_allowed:0,read_failure:0};
const rejected=key=>{counts[key]=Math.min(65535,counts[key]+1);return null;};
let stopping=false,active=0;
const emit=(event,fields={})=>send(Object.assign({event},fields));
function readable(p,n){
  if(p.isNull()||!Number.isInteger(n)||n<0||n>4096)return false;
  const r=Process.findRangeByAddress(p);
  return !!r&&r.protection.indexOf('r')>=0&&p.add(n).compare(p)>=0&&p.add(n).compare(r.base.add(r.size))<=0;
}
function stateOf(L){
  if(!readable(L,24))return null;
  const g=L.add(8).readPointer(),s=states.get(g.toString());
  if(!s||s.thread!==Process.getCurrentThreadId()||!readable(g,0x88))return null;
  const main=g.add(0x84).readPointer();
  return main.equals(s.main)&&readable(main,24)&&main.add(8).readPointer().equals(g)?s:null;
}
function pending(L,context){
  if(stopping||policy.count>=128)return null;
  if(!stateOf(L))return rejected('no_state');
  const base=L.add(0x10).readPointer(),top=L.add(0x14).readPointer(),size=top.sub(base).toInt32();
  if(size<8||size>512||size%8||!readable(base,8))return rejected('bad_frame');
  if(base.add(4).readU32()!==0xfffffffb)return rejected('non_string');
  const string=base.readPointer();if(!readable(string,16))return rejected('bad_string');
  const length=string.add(12).readU32();
  if(length<22||length>128||!readable(string,17+length)||string.add(16+length).readU8()!==0)return rejected('bad_string');
  // Do not copy unrelated game messages into a JavaScript string.
  for(let i=0;i<markerPrefix.length;i++)if(string.add(16+i).readU8()!==markerPrefix.charCodeAt(i))return rejected('not_allowed');
  const bytes=new Uint8Array(string.add(16).readByteArray(length));
  if(bytes.some(x=>x<32||x>126))return rejected('bad_string');
  const id=policy.match(String.fromCharCode(...bytes));if(id===null)return rejected('not_allowed');
  const saved={};for(const r of ['ebx','esi','edi','ebp'])saved[r]=ptr(context[r].toString());
  return {id,L,base,top,word:base.readU32(),tag:base.add(4).readU32(),saved};
}
for(const [channel,address] of Object.entries(CONFIG.callbacks))listeners.push(Interceptor.attach(at(address),{
  onEnter(args){const error=this.lastError;active++;
    try{counts[channel]=Math.min(65535,counts[channel]+1);const L=ptr(args[0].toString());
      this.marker=pending(L,this.context);}
    catch(_){this.marker=null;rejected('read_failure');}
    finally{active--;this.lastError=error;}
  },
  onLeave(result){const error=this.lastError;active++;
    try{const m=this.marker;if(m&&!stopping&&policy.commit())emit('gold-lua-marker',{
      channel,marker:m.id,original_return:result.toInt32(),
      lua_stack_unchanged:m.L.add(0x10).readPointer().equals(m.base)&&m.L.add(0x14).readPointer().equals(m.top)&&m.base.readU32()===m.word&&m.base.add(4).readU32()===m.tag,
      nonvolatile_registers_preserved:Object.keys(m.saved).every(r=>this.context[r].equals(m.saved[r])),
      capture_writes_lua_state:false});
    }catch(_){emit('capture-failed');}finally{active--;this.lastError=error;}
  }
}));
listeners.push(Interceptor.attach(at(CONFIG.api.newstate),{onLeave(result){const error=this.lastError;
  try{const L=ptr(result.toString());if(stopping||states.size>=8||!readable(L,24))return;
    const g=L.add(8).readPointer();if(!readable(g,0x88))return;
    const main=g.add(0x84).readPointer();if(!main.equals(L)||!readable(main,24)||!main.add(8).readPointer().equals(g))return;
    states.set(g.toString(),{main,thread:this.threadId});
  }catch(_){}finally{this.lastError=error;}
}}));
listeners.push(Interceptor.attach(at(CONFIG.api.close),{onEnter(args){const error=this.lastError;
  try{if(readable(args[0],24))states.delete(args[0].add(8).readPointer().toString());}catch(_){}finally{this.lastError=error;}
}}));
function commands(){recv('command',message=>{if(message.payload.op==='detach')stopping=true;else emit('command-rejected');commands();});}
commands();
const timer=setInterval(()=>{if(!stopping||active)return;
  for(const listener of listeners)listener.detach();states.clear();clearInterval(timer);
  emit('detached',{hooksRestored:true,luaLineHooksInstalled:false,counts});
},50);
emit('ready',{profile:'gold',goldMarkerCapture:true,luaLineHooksInstalled:false});
