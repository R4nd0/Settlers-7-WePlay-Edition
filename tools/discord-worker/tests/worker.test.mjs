import test from "node:test";
import assert from "node:assert/strict";
import {DatabaseSync} from "node:sqlite";
import {readFileSync} from "node:fs";
import worker,{games,board,updateBoard} from "../src/worker.mjs";

const APP="1565350833854353459",GUILD="1565306702356815962",CHANNEL="1565306705535959042";
const NOW=1793280000;
const hex=value=>Buffer.from(value).toString("hex");
const game=(change={})=>({id:"00000001",title:"Community",state:"open",players:1,capacity:4,free_slots:3,...change});

class D1 {
  constructor(){this.db=new DatabaseSync(":memory:");this.db.exec(readFileSync(new URL("../migrations/0001_directory.sql",import.meta.url),"utf8"));}
  prepare(sql){
    const db=this.db;
    const wrap=args=>({bind:(...values)=>wrap(values),
      first:async()=>db.prepare(sql).get(...args)||null,
      all:async()=>({results:db.prepare(sql).all(...args)}),
      run:async()=>({meta:{changes:Number(db.prepare(sql).run(...args).changes)}}),
      _run:()=>db.prepare(sql).run(...args)});
    return wrap([]);
  }
  async batch(statements){this.db.exec("BEGIN");try{const result=statements.map(x=>x._run());this.db.exec("COMMIT");return result;}
    catch(error){this.db.exec("ROLLBACK");throw error;}}
}

async function setup(t){
  let clock=NOW;t.mock.method(Date,"now",()=>clock*1000);
  const keys=await crypto.subtle.generateKey("Ed25519",true,["sign","verify"]);
  const env={DB:new D1(),APPLICATION_ID:APP,GUILD_ID:GUILD,CHANNEL_ID:CHANNEL,
    PUBLIC_ORIGIN:"https://weplay-multiplayer.testing.workers.dev",
    DISCORD_PUBLIC_KEY:hex(await crypto.subtle.exportKey("raw",keys.publicKey)),
    PUBLISHER_MASTER_KEY:Buffer.alloc(32,7).toString("base64url"),BOT_TOKEN:"synthetic_bot_token_never_logged"};
  t.after(()=>env.DB.db.close());
  t.mock.method(globalThis,"fetch",async()=>{throw new Error("Unexpected external call blocked by test");});
  let event=1565350833854354000n;
  const invoke=async(payload,changes={})=>{
    const value={id:String(event++),application_id:APP,guild_id:GUILD,channel_id:CHANNEL,
      member:{user:{id:"1565350833854353999"}},...payload};
    const raw=JSON.stringify(value);const stamp=String(clock);
    const signature=hex(await crypto.subtle.sign("Ed25519",keys.privateKey,new TextEncoder().encode(stamp+raw)));
    const request=new Request(env.PUBLIC_ORIGIN+"/interactions",{method:"POST",body:raw,
      headers:{"X-Signature-Ed25519":signature,"X-Signature-Timestamp":stamp,...changes}});
    return worker.fetch(request,env,{waitUntil:()=>{}});
  };
  const pair=async(user="1565350833854353999",eventId)=>{
    const response=await invoke({type:3,data:{custom_id:"weplay:connect"},member:{user:{id:user}},...(eventId?{id:eventId}:{})});
    assert.equal(response.status,200);
    const data=await response.json();assert.equal(data.data.flags,64);
    const code=data.data.content.match(/weplay-connect1\.([A-Za-z0-9_-]+)/)[1];
    return JSON.parse(Buffer.from(code,"base64url").toString());
  };
  const report=async(key,method,run="a".repeat(32),sequence=0,rows=[])=>{
    const pending=[];
    const response=await worker.fetch(new Request(env.PUBLIC_ORIGIN+"/v1/report",{method,
      headers:{Authorization:"Bearer "+key,"Content-Type":"application/json"},
      body:JSON.stringify({format:"weplay-game-report-v1",run_id:run,sequence,games:rows})}),env,
      {waitUntil:promise=>pending.push(promise)});
    await Promise.all(pending);return response;
  };
  return {env,invoke,pair,report,setTime:value=>{clock=value;}};
}

test("Ed25519 ping accepts exact signed bytes and rejects invalid/stale signatures",async t=>{
  const {invoke}=await setup(t);
  assert.deepEqual(await (await invoke({type:1})).json(),{type:1});
  assert.equal((await invoke({type:1},{"X-Signature-Ed25519":"0".repeat(128)})).status,401);
  assert.equal((await invoke({type:1},{"X-Signature-Timestamp":String(NOW-301)})).status,401);
});

test("Discord commands are scoped to exact app, guild and channel",async t=>{
  const {invoke}=await setup(t);
  for(const mismatch of [{application_id:"1"},{guild_id:"1"},{channel_id:"1"},{member:null}])
    assert.equal((await invoke({type:2,data:{name:"spiele"},...mismatch})).status,403);
  const help=await (await invoke({type:2,data:{name:"weplay-hilfe"}})).json();
  assert.equal(help.data.flags,64);assert.equal(help.data.components[0].components[0].custom_id,"weplay:connect");
});

test("Publisher pairing is private, hashed, idempotent and never stores its raw token",async t=>{
  const {pair,env}=await setup(t);const event="1565350833854354999";
  const first=await pair(undefined,event),again=await pair(undefined,event);
  assert.deepEqual(first,again);assert.equal(first.origin,env.PUBLIC_ORIGIN);
  const stored=await env.DB.prepare("SELECT * FROM weplay_publishers").all();
  assert.equal(stored.results.length,1);assert.ok(!JSON.stringify(stored).includes(first.key));
  const checked=await worker.fetch(new Request(env.PUBLIC_ORIGIN+"/v1/publisher",{headers:{Authorization:"Bearer "+first.key}}),env,{});
  assert.deepEqual(await checked.json(),{ok:true});
  assert.equal((await worker.fetch(new Request(env.PUBLIC_ORIGIN+"/v1/publisher"),env,{})).status,401);
});

test("Native game projection rejects private fields, malformed IDs and unsafe titles",()=>{
  assert.deepEqual(games([game()]),[game()]);
  for(const change of [{network_secret:"no"},{id:12345678},{state:"running"},{players:true},{players:5},
                       {title:"@everyone"},{title:"weplay1.private"},{title:"https://bad.invalid"},{state:"started",free_slots:1}])
    assert.throws(()=>games([game(change)]));
  assert.throws(()=>games([game(),game()]));
});

test("Authenticated lifecycle rejects stale updates and never resurrects a closed run",async t=>{
  const {pair,report,env}=await setup(t);const {key}=await pair();const run="a".repeat(32);
  assert.equal((await report(key,"POST")).status,200);
  assert.equal((await report(key,"PUT",run,1,[game()])).status,200);
  assert.equal((await report(key,"POST")).status,200);
  assert.equal((await board(env,NOW)).fields.length,1,"replayed start must not clear data");
  assert.equal((await report(key,"PUT",run,0,[])).status,409);
  assert.equal((await report(key,"DELETE",run,2)).status,200);
  assert.equal((await report(key,"PUT",run,3,[game()])).status,409);
  assert.equal((await report(key,"POST")).status,409);
  assert.equal((await board(env,NOW)).fields.length,0);
  assert.equal((await report(key,"POST","b".repeat(32))).status,200);
  assert.equal((await report(key,"DELETE",run,4)).status,409);
});

test("Expired reports disappear and an update cannot pretend to renew a stale lease",async t=>{
  const {pair,report,env,setTime}=await setup(t);const {key}=await pair();
  await report(key,"POST");await report(key,"PUT","a".repeat(32),1,[game()]);
  setTime(NOW+181);
  assert.equal((await board(env,NOW+181)).fields.length,0);
  assert.equal((await report(key,"PUT","a".repeat(32),1,[game()])).status,409);
  assert.equal((await report(key,"POST")).status,200);
  assert.equal((await report(key,"PUT","a".repeat(32),2,[game()])).status,200);
});

test("Token rotation withdraws old data and invalidates the previous publisher token",async t=>{
  const {pair,report,env}=await setup(t);const old=await pair();
  await report(old.key,"POST");await report(old.key,"PUT","a".repeat(32),1,[game()]);
  const next=await pair();assert.notEqual(old.key,next.key);
  assert.equal((await report(old.key,"PUT","a".repeat(32),2,[game()])).status,401);
  assert.equal((await board(env,NOW)).fields.length,0);
  assert.equal((await report(next.key,"POST","b".repeat(32))).status,200);
});

test("One publisher cannot close another owner's report",async t=>{
  const {pair,report,env}=await setup(t);const a=await pair();const b=await pair("1565350833854353888");
  await report(a.key,"POST");await report(a.key,"PUT","a".repeat(32),1,[game()]);
  assert.equal((await report(b.key,"DELETE","a".repeat(32),2)).status,409);
  assert.equal((await board(env,NOW)).fields.length,1);
});

test("The 16-active-publisher limit cannot be bypassed with an expired owner row",async t=>{
  const {pair,report,env}=await setup(t);const {key}=await pair();
  const own=await env.DB.prepare("SELECT owner FROM weplay_publishers").first();
  for(let i=0;i<16;i++){
    await env.DB.prepare("INSERT INTO weplay_publishers VALUES(?,?,?,?)").bind("other"+i,"hash"+i,"0",NOW).run();
    await env.DB.prepare("INSERT INTO weplay_reports(owner,run_id,sequence,payload,expires_at,closed) VALUES(?,?,0,'[]',?,0)")
      .bind("other"+i,"b".repeat(32),NOW+180).run();
  }
  await env.DB.prepare("INSERT INTO weplay_reports(owner,run_id,sequence,payload,expires_at,closed) VALUES(?,?,0,'[]',?,0)")
    .bind(own.owner,"c".repeat(32),NOW-1).run();
  assert.equal((await report(key,"POST")).status,409);
});

function fakeDiscord(t,env,{wrongApp=false,wrongBot=false,wrongChannel=false,foreign=false,missing=false,rate=false}={}){
  const calls=[];let message=null;
  t.mock.method(globalThis,"fetch",async(url,options)=>{
    calls.push({url,method:options.method||"GET",body:options.body});
    assert.equal(options.headers.Authorization,"Bot "+env.BOT_TOKEN);
    assert.equal(options.redirect,"manual");
    const path=new URL(url).pathname.replace("/api/v10","");
    const result=(value,status=200)=>new Response(JSON.stringify(value),{status});
    if(rate)return result({retry_after:120},429);
    if(path==="/applications/@me")return result({id:wrongApp?"1":APP});
    if(path==="/users/@me")return result({id:APP,bot:!wrongBot});
    if(path==="/channels/"+CHANNEL)return result({guild_id:wrongChannel?"1":GUILD,type:0});
    if(options.method==="POST" || options.method==="PATCH"){
      const value=JSON.parse(options.body);assert.deepEqual(value.allowed_mentions,{parse:[]});
      message={id:"1565350833854353777",author:{id:APP},embeds:value.embeds};return result(message);
    }
    if(path.endsWith("/messages/1565350833854353777")){
      if(missing)return result({},404);
      return result(foreign?{...message,author:{id:"1"}}:message);
    }
    throw Error("Unexpected Discord test request");
  });return calls;
}

test("Automatic board creates once, edits only its own message and skips unchanged data",async t=>{
  const {env}=await setup(t);const calls=fakeDiscord(t,env);
  await updateBoard(env,NOW);
  assert.equal(calls.filter(x=>x.method==="POST").length,1);
  await updateBoard(env,NOW+6);assert.equal(calls.filter(x=>x.method==="POST").length,1);
  await env.DB.prepare("INSERT INTO weplay_publishers VALUES('x','hash','0',?)").bind(NOW).run();
  await env.DB.prepare("INSERT INTO weplay_reports(owner,run_id,sequence,payload,expires_at,closed) VALUES('x',?,1,?,?,0)")
    .bind("a".repeat(32),JSON.stringify([game({title:"*Test*"})]),NOW+180).run();
  await updateBoard(env,NOW+12);
  assert.equal(calls.filter(x=>x.method==="PATCH").length,1);
  assert.ok(calls.at(-1).body.includes("Offen"));assert.ok(!calls.at(-1).body.includes(env.BOT_TOKEN));
});

test("Wrong bot application or guild cannot produce a channel message",async t=>{
  const {env}=await setup(t);let calls=fakeDiscord(t,env,{wrongApp:true});await updateBoard(env,NOW);
  assert.equal(calls.filter(x=>x.method==="POST").length,0);
  calls=fakeDiscord(t,env,{wrongChannel:true});await updateBoard(env,NOW+31);
  assert.equal(calls.filter(x=>x.method==="POST").length,0);
  calls=fakeDiscord(t,env,{wrongBot:true});await updateBoard(env,NOW+62);
  assert.equal(calls.filter(x=>x.method==="POST").length,0);
});

test("A foreign message is never edited and a deleted own board can be recreated",async t=>{
  const {env}=await setup(t);fakeDiscord(t,env);await updateBoard(env,NOW);
  await env.DB.prepare("UPDATE weplay_board SET digest='changed' WHERE slot=1").run();
  let calls=fakeDiscord(t,env,{foreign:true});await updateBoard(env,NOW+6);
  assert.equal(calls.filter(x=>x.method==="PATCH"||x.method==="POST").length,0);
  calls=fakeDiscord(t,env,{missing:true});await updateBoard(env,NOW+37);
  assert.equal(calls.filter(x=>x.method==="POST").length,1);
});

test("Board leases prevent duplicate creates and honor Discord rate-limit backoff",async t=>{
  const {env}=await setup(t);let calls=fakeDiscord(t,env,{rate:true});await updateBoard(env,NOW);
  await updateBoard(env,NOW+30);assert.equal(calls.length,1);
  calls=fakeDiscord(t,env);await Promise.all([updateBoard(env,NOW+121),updateBoard(env,NOW+121)]);
  assert.equal(calls.filter(x=>x.method==="POST").length,1);
});

test("The public API never returns publisher data or exception details on rejection",async t=>{
  const {env}=await setup(t);
  const response=await worker.fetch(new Request(env.PUBLIC_ORIGIN+"/v1/report",{method:"POST",body:"private-material"}),env,{waitUntil:()=>{}});
  assert.equal(response.status,401);assert.deepEqual(await response.json(),{error:"request_refused"});
});
