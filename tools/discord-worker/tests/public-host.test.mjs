import test from "node:test";
import assert from "node:assert/strict";
import {DatabaseSync} from "node:sqlite";
import {readFileSync} from "node:fs";
import worker,{board} from "../src/worker.mjs";
import {expirePublicPublishers} from "../src/public-publisher.mjs";
import {expireDirectory} from "../src/directory.mjs";

const ORIGIN="https://weplay-multiplayer.testing.workers.dev",NOW=1793780000;
const token=n=>Buffer.alloc(32,n).toString("base64url");
class D1 {
  constructor(){this.db=new DatabaseSync(":memory:");this.db.exec("PRAGMA foreign_keys=ON");
    for(const name of ["0001_directory.sql","0002_join_directory.sql","0003_public_hosts.sql"])
      this.db.exec(readFileSync(new URL("../migrations/"+name,import.meta.url),"utf8"));}
  prepare(sql){const db=this.db;const wrap=args=>({bind:(...values)=>wrap(values),
    first:async()=>db.prepare(sql).get(...args)||null,all:async()=>({results:db.prepare(sql).all(...args)}),
    run:async()=>({meta:{changes:Number(db.prepare(sql).run(...args).changes)}}),_run:()=>db.prepare(sql).run(...args)});return wrap([]);}
  async batch(statements){this.db.exec("BEGIN");try{const out=statements.map(s=>s._run());this.db.exec("COMMIT");return out;}
    catch(error){this.db.exec("ROLLBACK");throw error;}}
}
function setup(t){
  let now=NOW,outbound=0;t.mock.method(Date,"now",()=>now*1000);
  t.mock.method(globalThis,"fetch",async()=>{outbound++;throw new Error("No Discord call is authorized");});
  const env={DB:new D1(),PUBLIC_ORIGIN:ORIGIN,PUBLISHER_MASTER_KEY:token(12)};t.after(()=>env.DB.db.close());
  async function api(path,method="GET",value,secret,source="192.0.2.10"){
    const pending=[];const response=await worker.fetch(new Request(ORIGIN+path,{method,
      headers:{"CF-Connecting-IP":source,...(secret?{Authorization:"Bearer "+secret}:{})},
      ...(value?{body:JSON.stringify(value)}:{})}),env,{waitUntil:p=>pending.push(p)});
    await Promise.all(pending);return response;
  }
  const register=key=>api("/v1/directory/register","POST",{format:"weplay-public-host-v1",token:key});
  const report=(key,method,sequence,games=[],run="a".repeat(32))=>api("/v1/directory/report",method,{format:"weplay-game-report-v1",run_id:run,sequence,games},key);
  const host=(key,run="a".repeat(32))=>api("/v1/directory/host","PUT",{format:"weplay-directory-host-v1",run_id:run,
    room_id:"d".repeat(32),content_id:"b".repeat(64),network_revision:7,version:"2.0.0-rc3.13"},key);
  const game={id:"00000001",title:"Public native match",state:"open",players:1,capacity:4,free_slots:3};
  return {env,api,register,report,host,game,setTime:n=>{now=n;},outbound:()=>outbound};
}

test("Public hosting works without Discord, and its credential cannot publish Discord messages",async t=>{
  const f=setup(t),key=token(1);
  assert.equal((await f.register(key)).status,200);
  assert.equal((await f.report(key,"POST",0)).status,200);
  assert.equal((await f.report(key,"PUT",1,[f.game])).status,200);
  assert.equal((await f.host(key)).status,200);
  const list=await (await f.api("/v1/directory/games")).json();assert.equal(list.games.length,1);
  assert.equal(list.games[0].title,f.game.title);
  for(const privateField of [key,"owner","room_id","token_hash","anonymous","weplay1."])
    assert.ok(!JSON.stringify(list).includes(privateField));
  assert.equal((await f.api("/v1/publisher","GET",null,key)).status,401);
  assert.equal((await f.api("/v1/report","POST",{format:"weplay-game-report-v1",run_id:"e".repeat(32),sequence:0,games:[]},key)).status,401);
  assert.equal((await board(f.env,NOW)).fields.length,0,"public-only games must not silently appear on Discord");
  assert.equal(f.outbound(),0);
});

test("Registration is idempotent, source-limited, secret-free in storage and strictly typed",async t=>{
  const f=setup(t),key=token(1);
  for(let i=0;i<10;i++)assert.equal((await f.register(key)).status,200);
  assert.equal(f.env.DB.db.prepare("SELECT COUNT(*) AS n FROM weplay_public_publishers").get().n,1);
  for(let i=2;i<=6;i++)assert.equal((await f.register(token(i))).status,200);
  assert.equal((await f.register(token(7))).status,429);
  const stored=JSON.stringify(f.env.DB.db.prepare("SELECT * FROM weplay_publishers").all())+JSON.stringify(f.env.DB.db.prepare("SELECT * FROM weplay_join_rates").all());
  assert.ok(!stored.includes(key));assert.ok(!stored.includes("192.0.2.10"));
  for(const bad of [{format:"weplay-public-host-v1",token:true},{format:"weplay-public-host-v1",token:key,discord:true},{token:key}])
    assert.equal((await f.api("/v1/directory/register","POST",bad)).status,400);
});

test("Public hosting grants expire, refresh with possession, and clean up without deleting paired Discord owners",async t=>{
  const f=setup(t),key=token(1);await f.register(key);await f.report(key,"POST",0);await f.report(key,"PUT",1,[f.game]);await f.host(key);
  f.setTime(NOW+301);assert.equal((await f.host(key)).status,401);
  assert.equal((await f.register(key)).status,200);assert.equal((await f.report(key,"POST",0)).status,200);
  assert.equal((await f.report(key,"PUT",2,[f.game])).status,200);assert.equal((await f.host(key)).status,200);
  await f.env.DB.prepare("INSERT INTO weplay_publishers VALUES(?,?,?,?)").bind("legacy",token(90),"signed-interaction",NOW).run();
  await expirePublicPublishers(f.env,NOW+602);await expireDirectory(f.env,NOW+602);
  assert.equal(f.env.DB.db.prepare("SELECT COUNT(*) AS n FROM weplay_public_publishers").get().n,0);
  assert.equal(f.env.DB.db.prepare("SELECT COUNT(*) AS n FROM weplay_reports").get().n,0);
  assert.equal(f.env.DB.db.prepare("SELECT COUNT(*) AS n FROM weplay_directory_hosts").get().n,0);
  assert.equal(f.env.DB.db.prepare("SELECT owner FROM weplay_publishers").get().owner,"legacy");
});

test("Public credentials cannot answer another host's encrypted join mailbox",async t=>{
  const f=setup(t),a=token(1),b=token(2);await f.register(a);await f.register(b);
  await f.report(a,"POST",0);await f.report(a,"PUT",1,[f.game]);await f.host(a);
  await f.report(b,"POST",0,[],"c".repeat(32));await f.report(b,"PUT",1,[f.game],"c".repeat(32));await f.host(b,"c".repeat(32));
  const rows=(await (await f.api("/v1/directory/games")).json()).games;assert.equal(rows.length,2);assert.notEqual(rows[0].id,rows[1].id);
  const key=await crypto.subtle.generateKey({name:"RSA-OAEP",modulusLength:3072,publicExponent:new Uint8Array([1,0,1]),hash:"SHA-256"},true,["encrypt","decrypt"]);
  const request={format:"weplay-join-request-v1",game_id:rows[0].id,content_id:"b".repeat(64),network_revision:7,
    public_key:Buffer.from(await crypto.subtle.exportKey("spki",key.publicKey)).toString("base64url"),challenge:"f".repeat(32),token:token(3)};
  const ticket=await (await f.api("/v1/directory/join","POST",request)).json();
  const aRequests=(await (await f.host(a)).json()).requests,bRequests=(await (await f.host(b,"c".repeat(32))).json()).requests;
  assert.equal(aRequests.length+bRequests.length,1);
  const wrong=aRequests.length?b:a,run=aRequests.length?"a".repeat(32):"c".repeat(32);
  assert.equal((await f.api(`/v1/directory/host/${run}/requests/${ticket.id}`,"PUT",{format:"weplay-join-response-v1",envelope:null},wrong)).status,409);
});

test("Renewing a public host keeps the same visible game through repeated five-minute expiry boundaries",async t=>{
  const f=setup(t),key=token(1);
  assert.equal((await f.register(key)).status,200);
  assert.equal((await f.report(key,"POST",0)).status,200);
  assert.equal((await f.report(key,"PUT",1,[f.game])).status,200);
  assert.equal((await f.host(key)).status,200);
  const identifier=(await (await f.api("/v1/directory/games")).json()).games[0].id;
  for(let minute=1;minute<=12;minute++){
    const now=NOW+minute*60;f.setTime(now);
    // Maintenance can run just before a host heartbeat in real deployment.
    await expirePublicPublishers(f.env,now);
    assert.equal((await f.register(key)).status,200);
    const expires=f.env.DB.db.prepare("SELECT expires_at FROM weplay_public_publishers").get().expires_at;
    assert.equal(expires,now+300,"acknowledged renewal must really extend the credential");
    assert.equal((await f.report(key,"PUT",minute+1,[f.game])).status,200);
    assert.equal((await f.host(key)).status,200);
    const rows=(await (await f.api("/v1/directory/games")).json()).games;
    assert.equal(rows.length,1);assert.equal(rows[0].id,identifier);
  }
  assert.equal(f.outbound(),0);
  await expirePublicPublishers(f.env,NOW+12*60+301);await expireDirectory(f.env,NOW+12*60+301);
  assert.equal((await (await f.api("/v1/directory/games")).json()).games.length,0);
});
