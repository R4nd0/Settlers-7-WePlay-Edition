import test from "node:test";
import assert from "node:assert/strict";
import {DatabaseSync} from "node:sqlite";
import {readFileSync} from "node:fs";
import worker from "../src/worker.mjs";
import {expireDirectory} from "../src/directory.mjs";

const NOW=1793700000,ORIGIN="https://weplay-multiplayer.testing.workers.dev";
const run="a".repeat(32),content="b".repeat(64),owner="c".repeat(64),token=Buffer.alloc(32,9).toString("base64url");
const hash=async text=>Buffer.from(await crypto.subtle.digest("SHA-256",new TextEncoder().encode(text))).toString("hex");
class D1{
  constructor(){this.db=new DatabaseSync(":memory:");for(const name of ["0001_directory.sql","0002_join_directory.sql","0003_public_hosts.sql"])
    this.db.exec(readFileSync(new URL("../migrations/"+name,import.meta.url),"utf8"));}
  prepare(sql){const db=this.db;const wrap=args=>({bind:(...values)=>wrap(values),first:async()=>db.prepare(sql).get(...args)||null,
    all:async()=>({results:db.prepare(sql).all(...args)}),run:async()=>({meta:{changes:Number(db.prepare(sql).run(...args).changes)}}),
    _run:()=>db.prepare(sql).run(...args)});return wrap([]);}
  async batch(statements){this.db.exec("BEGIN");try{const rows=statements.map(x=>x._run());this.db.exec("COMMIT");return rows;}
    catch(error){this.db.exec("ROLLBACK");throw error;}}
}
const game=(change={})=>({id:"00000001",title:"Two-client test",state:"open",players:1,capacity:4,free_slots:3,...change});
async function setup(t){
  let clock=NOW;t.mock.method(Date,"now",()=>clock*1000);
  const env={DB:new D1(),PUBLIC_ORIGIN:ORIGIN,PUBLISHER_MASTER_KEY:Buffer.alloc(32,4).toString("base64url")};
  t.after(()=>env.DB.db.close());
  await env.DB.prepare("INSERT INTO weplay_publishers VALUES(?,?,?,?)").bind(owner,await hash(token),"0",NOW).run();
  async function request(path,method="GET",value,auth){
    const pending=[];const response=await worker.fetch(new Request(ORIGIN+path,{method,
      headers:{...(auth?{Authorization:auth}:{}),...(value?{"Content-Type":"application/json"}:{})},
      ...(value?{body:JSON.stringify(value)}:{})}),env,{waitUntil:p=>pending.push(p)});
    await Promise.all(pending);return response;
  }
  const report=async(method,sequence,rows=[])=>request("/v1/report",method,{format:"weplay-game-report-v1",run_id:run,sequence,games:rows},"Bearer "+token);
  const host=async()=>request("/v1/directory/host","PUT",{format:"weplay-directory-host-v1",run_id:run,room_id:"d".repeat(32),
    content_id:content,network_revision:7,version:"2.0.0-rc3.12"},"Bearer "+token);
  assert.equal((await report("POST",0)).status,200);assert.equal((await report("PUT",1,[game()])).status,200);
  assert.equal((await host()).status,200);
  const list=async()=> (await request("/v1/directory/games")).json();
  return {env,request,report,host,list,setTime:value=>{clock=value;}};
}
async function guest(){
  const keys=await crypto.subtle.generateKey({name:"RSA-OAEP",modulusLength:3072,publicExponent:new Uint8Array([1,0,1]),hash:"SHA-256"},true,["encrypt","decrypt"]);
  return {keys,public_key:Buffer.from(await crypto.subtle.exportKey("spki",keys.publicKey)).toString("base64url"),
    token:Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString("base64url"),
    challenge:Buffer.from(crypto.getRandomValues(new Uint8Array(16))).toString("hex")};
}
async function join(client,fixture,change={}){
  const entry=(await fixture.list()).games[0];
  return fixture.request("/v1/directory/join","POST",{format:"weplay-join-request-v1",game_id:entry.id,content_id:content,
    network_revision:7,public_key:client.public_key,challenge:client.challenge,token:client.token,...change});
}

test("Public list contains joinable native metadata, no owner, invitation, address or host credential",async t=>{
  const f=await setup(t);const result=await f.list();assert.equal(result.games.length,1);
  assert.match(result.games[0].id,/^[0-9a-f]{32}$/);assert.equal(result.games[0].content_id,content);
  for(const secret of [token,owner,'"_host"','"room_id"',"weplay1.","token_hash","public_key"])
    assert.ok(!JSON.stringify(result).includes(secret));
});
test("Two independent clients request the same public game and receive only their own encrypted envelope",async t=>{
  const f=await setup(t),a=await guest(),b=await guest();
  const ra=await join(a,f),rb=await join(b,f);assert.equal(ra.status,200);assert.equal(rb.status,200);
  const ta=await ra.json(),tb=await rb.json();assert.notEqual(ta.id,tb.id);
  const pending=await (await f.host()).json();assert.equal(pending.requests.length,2);
  const invitation="weplay1.synthetic-private-room-never-published";
  const sealed={};
  for(const [client,ticket] of [[a,ta],[b,tb]]){
    const key=crypto.getRandomValues(new Uint8Array(32)),nonce=crypto.getRandomValues(new Uint8Array(12));
    const aes=await crypto.subtle.importKey("raw",key,"AES-GCM",false,["encrypt"]);
    const raw=new Uint8Array(await crypto.subtle.encrypt({name:"AES-GCM",iv:nonce,additionalData:new TextEncoder().encode(ticket.id)},aes,new TextEncoder().encode(invitation)));
    const envelope={scheme:"rsa3072-oaep256-a256gcm-v1",key:Buffer.from(await crypto.subtle.encrypt("RSA-OAEP",client.keys.publicKey,key)).toString("base64url"),
      nonce:Buffer.from(nonce).toString("base64url"),ciphertext:Buffer.from(raw.slice(0,-16)).toString("base64url"),tag:Buffer.from(raw.slice(-16)).toString("base64url")};
    assert.equal((await f.request(`/v1/directory/host/${run}/requests/${ticket.id}`,"PUT",{format:"weplay-join-response-v1",envelope},"Bearer "+token)).status,200);
    const result=await (await f.request(`/v1/directory/join/${ticket.id}`,"GET",undefined,"Join "+client.token)).json();
    sealed[ticket.id]=result.envelope;
    const opened=await crypto.subtle.decrypt("RSA-OAEP",client.keys.privateKey,Buffer.from(result.envelope.key,"base64url"));
    const actual=await crypto.subtle.importKey("raw",opened,"AES-GCM",false,["decrypt"]);
    const clear=await crypto.subtle.decrypt({name:"AES-GCM",iv:Buffer.from(result.envelope.nonce,"base64url"),additionalData:new TextEncoder().encode(ticket.id)},
      actual,Buffer.concat([Buffer.from(result.envelope.ciphertext,"base64url"),Buffer.from(result.envelope.tag,"base64url")]));
    assert.equal(new TextDecoder().decode(clear),invitation);
  }
  assert.equal((await f.request(`/v1/directory/join/${ta.id}`,"GET",undefined,"Join "+b.token)).status,404);
  await assert.rejects(()=>crypto.subtle.decrypt("RSA-OAEP",b.keys.privateKey,Buffer.from(sealed[ta.id].key,"base64url")));
  assert.ok(!JSON.stringify(f.env.DB.db.prepare("SELECT * FROM weplay_join_requests").all()).includes(invitation));
  assert.equal((await f.request(`/v1/directory/join/${ta.id}`,"DELETE",undefined,"Join "+a.token)).status,200);
  assert.equal((await f.request(`/v1/directory/join/${ta.id}`,"GET",undefined,"Join "+a.token)).status,410);
});
test("Wrong content/revision, full or started games never issue a join ticket",async t=>{
  const f=await setup(t),a=await guest();
  assert.equal((await join(a,f,{content_id:"e".repeat(64)})).status,409);
  assert.equal((await join(a,f,{network_revision:8})).status,400);
  await f.report("PUT",2,[game({state:"full",players:4,free_slots:0})]);
  assert.equal((await join(a,f)).status,409);
  await f.report("PUT",3,[game({state:"started",free_slots:0})]);
  assert.equal((await join(a,f)).status,409);
});
test("Expired hosts, closed reports and expired tickets cannot be revived or consumed",async t=>{
  const f=await setup(t),a=await guest();const ticket=await (await join(a,f)).json();
  f.setTime(NOW+36);assert.equal((await f.list()).games.length,0);
  assert.equal((await f.request(`/v1/directory/join/${ticket.id}`,"GET",undefined,"Join "+a.token)).status,409);
  await f.host();f.setTime(NOW+46);
  assert.equal((await f.request(`/v1/directory/join/${ticket.id}`,"GET",undefined,"Join "+a.token)).status,410);
  await f.report("DELETE",2);
  assert.equal((await f.list()).games.length,0);assert.equal((await f.host()).status,409);
});
test("Reservations cannot exceed native free places and owner cannot be changed",async t=>{
  const f=await setup(t),a=await guest();await f.report("PUT",2,[game({free_slots:1})]);
  assert.equal((await join(a,f)).status,200);assert.equal((await join(a,f)).status,429);
  assert.equal((await f.request("/v1/directory/host","DELETE",{run_id:run})).status,401);
  const unknown=await f.request(`/v1/directory/host/${run}/requests/${"e".repeat(32)}`,"PUT",{format:"weplay-join-response-v1",envelope:null},"Bearer "+token);
  assert.equal(unknown.status,200); // Idempotent rejection never reveals another owner's request.
});
test("Minute maintenance removes expired encrypted requests, hosts and keyed source buckets",async t=>{
  const f=await setup(t),a=await guest();assert.equal((await join(a,f)).status,200);
  assert.equal(f.env.DB.db.prepare('SELECT COUNT(*) n FROM weplay_join_requests').get().n,1);
  await expireDirectory(f.env,NOW+36);
  assert.equal(f.env.DB.db.prepare('SELECT COUNT(*) n FROM weplay_join_requests').get().n,0);
  assert.equal(f.env.DB.db.prepare('SELECT COUNT(*) n FROM weplay_directory_hosts').get().n,0);
  await expireDirectory(f.env,NOW+121);
  assert.equal(f.env.DB.db.prepare('SELECT COUNT(*) n FROM weplay_join_rates').get().n,0);
});
