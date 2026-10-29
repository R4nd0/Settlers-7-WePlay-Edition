import {Miniflare,convertV4MiniflareOptions} from '../../../work/discord-worker-toolchain/node_modules/miniflare/dist/src/index.js';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
const origin='https://weplay-multiplayer.testing.workers.dev';
const keys=await crypto.subtle.generateKey('Ed25519',true,['sign','verify']);
const hex=x=>Buffer.from(x).toString('hex');
const outbound=[];let redirect=false;
const app='1565350833854353459',guild='1565306702356815962',channel='1565306705535959042',message='1565350833854353777';
const mf=new Miniflare(convertV4MiniflareOptions({cf:false,modules:[
 {type:'ESModule',path:resolve('tools/discord-worker/tests/workerd-entry.mjs')},
 {type:'ESModule',path:resolve('tools/discord-worker/src/worker.mjs')}],
 compatibilityDate:'2026-10-29',d1Databases:['DB'],
 bindings:{PUBLIC_ORIGIN:origin,APPLICATION_ID:'1565350833854353459',GUILD_ID:'1565306702356815962',CHANNEL_ID:'1565306705535959042',
 DISCORD_PUBLIC_KEY:hex(await crypto.subtle.exportKey('raw',keys.publicKey)),PUBLISHER_MASTER_KEY:Buffer.alloc(32,7).toString('base64url'),
 BOT_TOKEN:'synthetic_local_bot_token_not_real'},
 outboundService:request=>{
  const url=new URL(request.url);assert.equal(url.origin,'https://discord.com');
  assert.equal(request.headers.get('Authorization'),'Bot synthetic_local_bot_token_not_real');outbound.push(url.pathname);
  if(redirect)return new Response(null,{status:302,headers:{Location:'https://attacker.invalid/never-follow'}});
  const path=url.pathname.replace('/api/v10','');let value;
  if(path==='/applications/@me')value={id:app};
  else if(path==='/users/@me')value={id:app,bot:true};
  else if(path==='/channels/'+channel)value={guild_id:guild,type:0};
  else if(path==='/channels/'+channel+'/messages' && request.method==='POST')value={id:message};
  else throw new Error('Unexpected fixture request; external network is blocked');
  return new Response(JSON.stringify(value),{headers:{'Content-Type':'application/json'}});
 }}));
try{
 const db=await mf.getD1Database('DB');
 for(const sql of readFileSync('tools/discord-worker/migrations/0001_directory.sql','utf8').split(';'))if(sql.trim())await db.prepare(sql).run();
 await mf.dispatchFetch(origin+'/__test_board',{method:'POST'});
 assert.equal((await db.prepare('SELECT message_id FROM weplay_board').first()).message_id,message);
 assert.equal(outbound.length,4,'real workerd must construct all four outbound Discord requests');
 redirect=true;await db.prepare("UPDATE weplay_board SET digest='changed',lease_until=0").run();
 await mf.dispatchFetch(origin+'/__test_board',{method:'POST'});
 assert.equal(outbound.length,5,'a redirect must fail without forwarding the bot credential');
 redirect=false;
 async function interaction(value){
  const raw=JSON.stringify(value),stamp=String(Math.floor(Date.now()/1000));
  const sig=hex(await crypto.subtle.sign('Ed25519',keys.privateKey,new TextEncoder().encode(stamp+raw)));
  return mf.dispatchFetch(origin+'/interactions',{method:'POST',body:raw,headers:{'X-Signature-Timestamp':stamp,'X-Signature-Ed25519':sig}});
 }
 assert.deepEqual(await (await interaction({type:1})).json(),{type:1});
 const paired=await interaction({type:3,id:'1565350833854354999',application_id:'1565350833854353459',guild_id:'1565306702356815962',channel_id:'1565306705535959042',member:{user:{id:'1565350833854353999'}},data:{custom_id:'weplay:connect'}});
 assert.equal(paired.status,200);
 const code=(await paired.json()).data.content.match(/weplay-connect1\.([A-Za-z0-9_-]+)/)[1];
 const grant=JSON.parse(Buffer.from(code,'base64url'));
 const checked=await mf.dispatchFetch(origin+'/v1/publisher',{headers:{Authorization:'Bearer '+grant.key}});assert.equal(checked.status,200);
 const report={format:'weplay-game-report-v1',run_id:'a'.repeat(32),sequence:0,games:[]};
 assert.equal((await mf.dispatchFetch(origin+'/v1/report',{method:'POST',headers:{Authorization:'Bearer '+grant.key},body:JSON.stringify(report)})).status,200);
 report.sequence=1;report.games=[{id:'00000001',title:'Synthetic local-only test',state:'open',players:1,capacity:4,free_slots:3}];
 assert.equal((await mf.dispatchFetch(origin+'/v1/report',{method:'PUT',headers:{Authorization:'Bearer '+grant.key},body:JSON.stringify(report)})).status,200);
 assert.equal((await db.prepare('SELECT payload FROM weplay_reports').first()).payload,JSON.stringify(report.games));
 assert.equal((await mf.dispatchFetch(origin+'/v1/publisher')).status,401);
 console.log('PASS: real workerd/D1 signed ping, private pairing, reports and board HTTP construction; redirects blocked and all Discord traffic mocked.');
}finally{await mf.dispose();}
