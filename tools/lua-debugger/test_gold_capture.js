'use strict';
const assert=require('node:assert/strict'),fs=require('node:fs'),vm=require('node:vm');
const Policy=require('./gold_capture_policy');const prefix='WEPLAY_GOLD_CAPTURE_V1 ';
let checks=0;
for(const invalid of [[],[null],[prefix],[prefix+'x',prefix+'x'],[prefix+'x'.repeat(129)],[prefix+'x\n'],[prefix+'"'],Array(17).fill(prefix+'x')]){
  assert.throws(()=>new Policy(invalid));checks++;
}
const p=new Policy([prefix+'x']);assert.equal(p.match(prefix+'x'),0);assert.equal(p.match(prefix+'other'),null);
for(let i=0;i<128;i++)assert.equal(p.commit(),true);assert.equal(p.match(prefix+'x'),null);assert.equal(p.commit(),false);checks++;
function fixture(options={}){
  const bytes=new Map(),events=[],hooks=new Map(),reads=new Set();let thread=7,hole=null;
  const moduleBase=options.base||0x400000;
  const put=(a,n,v)=>{for(let i=0;i<n;i++)bytes.set(a+i,Math.floor(v/2**(i*8))&255);};
  class P{
    constructor(x){this.x=x>>>0;}add(n){return new P(this.x+Number(n));}sub(p){return new P(this.x-(p instanceof P?p.x:Number(p)));}
    toUInt32(){return this.x;}toInt32(){return this.x|0;}toString(){return '0x'+this.x.toString(16);}isNull(){return this.x===0;}
    equals(p){return this.x===p.x;}compare(p){return Math.sign(this.x-p.x);}
    readU8(){reads.add(this.x);if(hole===this.x)throw Error('unreadable');return bytes.get(this.x)||0;}
    readU16(){return this.readU8()+this.add(1).readU8()*256;}
    readU32(){return (this.readU16()+this.add(2).readU16()*65536)>>>0;}
    readPointer(){return new P(this.readU32());}readByteArray(n){return Uint8Array.from({length:n},(_,i)=>this.add(i).readU8()).buffer;}
  }
  const ptr=x=>x instanceof P?x:new P(Number(x)),L=ptr(0x1000),g=ptr(0x2000),base=ptr(0x3000),str=ptr(0x4000);
  put(moduleBase+0x3c,4,0x80);put(moduleBase+0x80,4,0x4550);put(moduleBase+0x84,2,0x14c);put(moduleBase+0x88,4,42);
  put(L.x+8,4,g.x);put(g.x+0x84,4,L.x);put(L.x+0x10,4,base.x);put(L.x+0x14,4,base.x+8);
  put(base.x,4,str.x);put(base.x+4,4,0xfffffffb);
  const text=t=>{put(str.x+12,4,t.length);for(let i=0;i<t.length;i++)put(str.x+16+i,1,t.charCodeAt(i));put(str.x+16+t.length,1,0);};text(prefix+'x');
  const config={path:'Z:/work/Settlers7R.exe',identity:{version:'gold',timestamp:42,imageSize:0x2000},guards:[],callbacks:{console:0x9842a0,log:0x984210},markers:[prefix+'x'],api:{newstate:0xbe1550,close:0xbd4910},fixture:null};
  if(options.guard){config.guards=[{address:0x401000,bytes:'103412400020',relocations:[{offset:1,word:0x401234}]}];
    put(moduleBase+0x1000,1,0x10);put(moduleBase+0x1001,4,moduleBase+0x1234);put(moduleBase+0x1005,1,options.tamper?0x21:0x20);}
  const process={arch:'ia32',pointerSize:4,getModuleByName:()=>({base:ptr(moduleBase),size:0x2000,path:config.path}),getCurrentThreadId:()=>thread,
    findRangeByAddress:p=>hole===p.x?null:{base:ptr(0x1000),size:0x10000,protection:'rw-'}};
  const context={CONFIG:config,Process:process,ptr,Uint8Array,send:e=>events.push(JSON.parse(JSON.stringify(e))),recv:()=>{},setInterval:()=>1,clearInterval:()=>{},
    Interceptor:{attach:(a,h)=>{hooks.set(a.x,h);return {detach:()=>{}};}}};
  vm.runInNewContext(fs.readFileSync(__dirname+'/gold_capture_policy.js','utf8')+'\n'+fs.readFileSync(__dirname+'/gold_capture_agent.js','utf8'),context);
  const runtime=a=>moduleBase+a-0x400000;
  hooks.get(runtime(config.api.newstate)).onLeave.call({threadId:7,lastError:19},L);
  const call=()=>{const h=hooks.get(runtime(config.callbacks.console)),ctx={lastError:37,context:{ebx:ptr(1),esi:ptr(2),edi:ptr(3),ebp:ptr(4)}};
    const before=JSON.stringify([...bytes]);h.onEnter.call(ctx,[L]);assert.equal(ctx.lastError,37);ctx.lastError=83;h.onLeave.call(ctx,ptr(0));assert.equal(ctx.lastError,83);
    assert.equal(JSON.stringify([...bytes]),before);return events.filter(e=>e.event==='gold-lua-marker');};
  return {call,text,put,L,g,base,str,reads,setThread:x=>thread=x,setHole:x=>hole=x,close:()=>hooks.get(runtime(config.api.close)).onEnter.call({lastError:0},[L])};
}
let f=fixture();assert.deepEqual(f.call(),[{event:'gold-lua-marker',channel:'console',marker:0,original_return:0,lua_stack_unchanged:true,nonvolatile_registers_preserved:true,capture_writes_lua_state:false}]);checks++;
for(const mutate of [f=>f.text(prefix+'unapproved'),f=>f.text('private account text'),f=>f.text('x'.repeat(129)),f=>f.put(f.base.x+4,4,0xffffffff),
  f=>f.put(f.L.x+0x14,4,f.base.x+7),f=>f.put(f.L.x+0x14,4,f.base.x+520),f=>f.setThread(8),f=>f.put(f.g.x+0x84,4,0),
  f=>f.put(f.str.x+16+prefix.length+1,1,1),f=>f.setHole(f.str.x),f=>f.setHole(f.str.x+17),f=>f.close()]){
  f=fixture();mutate(f);assert.equal(f.call().length,0);checks++;
}
f=fixture();for(let i=0;i<140;i++)f.call();assert.equal(f.call().length,128);checks++;
f=fixture();f.text('unapproved-account-data-must-not-be-read');f.setHole(f.str.x+30);f.call();assert.equal(f.reads.has(f.str.x+30),false);checks++;
assert.equal(fixture({base:0x500000,guard:true}).call().length,1);checks++;
assert.throws(()=>fixture({base:0x500000,guard:true,tamper:true}),/gold-native-code-guard/);checks++;
console.log(`${checks} Gold capture policy/layout/owner/lifetime/limit/LastError/no-write checks passed`);
