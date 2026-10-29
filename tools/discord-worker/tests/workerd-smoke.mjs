import {Miniflare,convertV4MiniflareOptions} from '../../../work/discord-worker-toolchain/node_modules/miniflare/dist/src/index.js';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
const origin='https://weplay-multiplayer.testing.workers.dev';
const keys=await crypto.subtle.generateKey('Ed25519',true,['sign','verify']);
const hex=x=>Buffer.from(x).toString('hex');
const mf=new Miniflare(convertV4MiniflareOptions({cf:false,modules:true,scriptPath:resolve('tools/discord-worker/src/worker.mjs'),
 compatibilityDate:'2026-10-29',d1Databases:['DB'],
 bindings:{PUBLIC_ORIGIN:origin,APPLICATION_ID:'1565350833854353459',GUILD_ID:'1565306702356815962',CHANNEL_ID:'1565306705535959042',
 DISCORD_PUBLIC_KEY:hex(await crypto.subtle.exportKey('raw',keys.publicKey)),PUBLISHER_MASTER_KEY:Buffer.alloc(32,7).toString('base64url')},
 outboundService:()=>new Response('{}',{status:503})}));
try{
 const db=await mf.getD1Database('DB');
 for(const sql of readFileSync('tools/discord-worker/migrations/0001_directory.sql','utf8').split(';'))if(sql.trim())await db.prepare(sql).run();
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
 console.log('PASS: real workerd/D1 signed ping, private pairing, grant authentication and public report lifecycle; outbound Discord blocked.');
}finally{await mf.dispose();}
