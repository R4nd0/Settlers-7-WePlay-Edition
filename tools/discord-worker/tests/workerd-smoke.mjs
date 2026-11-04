import {Miniflare,convertV4MiniflareOptions} from '../../../work/discord-worker-toolchain/node_modules/miniflare/dist/src/index.js';
import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';
import assert from 'node:assert/strict';
import {spawn} from 'node:child_process';
const origin='https://weplay-multiplayer.testing.workers.dev';
const keys=await crypto.subtle.generateKey('Ed25519',true,['sign','verify']);
const hex=x=>Buffer.from(x).toString('hex');
const outbound=[];let redirect=false;
const app='1565350833854353459',guild='1565306702356815962',channel='1565306705535959042',message='1565350833854353777';
const mf=new Miniflare(convertV4MiniflareOptions({cf:false,modules:[
 {type:'ESModule',path:resolve('tools/discord-worker/tests/workerd-entry.mjs')},
 {type:'ESModule',path:resolve('tools/discord-worker/src/worker.mjs')},
 {type:'ESModule',path:resolve('tools/discord-worker/src/public-publisher.mjs')},
 {type:'ESModule',path:resolve('tools/discord-worker/src/directory.mjs')}],
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
 for(const file of ['0001_directory.sql','0002_join_directory.sql','0003_public_hosts.sql'])
  for(const sql of readFileSync('tools/discord-worker/migrations/'+file,'utf8').split(';'))if(sql.trim())await db.prepare(sql).run();
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
 const host={format:'weplay-directory-host-v1',run_id:report.run_id,room_id:'d'.repeat(32),content_id:'c'.repeat(64),network_revision:7,version:'2.0.0-rc3.12'};
 const api=async(path,method='GET',value=null,auth=null)=>{
  const response=await mf.dispatchFetch(origin+path,{method,headers:auth?{Authorization:auth}:{},body:value===null?undefined:JSON.stringify(value)});
  assert.equal(response.status,200,path);return response.json();
 };
 await api('/v1/directory/host','PUT',host,'Bearer '+grant.key);
 const game=(await api('/v1/directory/games')).games[0];assert.equal(game.content_id,host.content_id);
 assert.equal(game.title,report.games[0].title);assert.equal('owner' in game,false);
 const joinInteraction={type:3,id:'1565350833854354998',application_id:app,guild_id:guild,channel_id:channel,member:{user:{id:'1565350833854353999'}},data:{custom_id:'weplay:join',values:[game.id]}};
 const privateJoin=await (await interaction(joinInteraction)).json();
 assert.equal(privateJoin.data.flags,64);assert.match(privateJoin.data.content,/weplay-join1\./);
 assert.equal(privateJoin.data.content.includes('weplay1.'),false);
 const publicToken=Buffer.from(crypto.getRandomValues(new Uint8Array(32))).toString('base64url');
 const previousOutbound=outbound.length;
 await api('/v1/directory/register','POST',{format:'weplay-public-host-v1',token:publicToken});
 const publicReport={format:'weplay-game-report-v1',run_id:'e'.repeat(32),sequence:0,games:[]};
 await api('/v1/directory/report','POST',publicReport,'Bearer '+publicToken);
 publicReport.sequence=1;publicReport.games=[{...report.games[0],title:'Anonymous public native fixture'}];
 await api('/v1/directory/report','PUT',publicReport,'Bearer '+publicToken);
 const publicHost={...host,run_id:publicReport.run_id};
 await api('/v1/directory/host','PUT',publicHost,'Bearer '+publicToken);
 const publicGame=(await api('/v1/directory/games')).games.find(row=>row.title===publicReport.games[0].title);
 assert.ok(publicGame);assert.equal(outbound.length,previousOutbound,'public-only hosting must not call Discord');
 assert.equal((await mf.dispatchFetch(origin+'/v1/publisher',{headers:{Authorization:'Bearer '+publicToken}})).status,401);
 if(process.env.S7_TEST_DIRECTORY_CRYPTO){
  const host=publicHost,game=publicGame,grant={key:publicToken};
  const helper=resolve(process.env.S7_TEST_DIRECTORY_CRYPTO),dotnet=resolve('work/dotnet/dotnet.exe');
  const cryptoOperation=(action,value)=>new Promise((accept,reject)=>{
   const child=spawn(dotnet,[helper,'directory-crypto',action],{windowsHide:true,stdio:['pipe','pipe','pipe']});let output='';
   child.stdout.on('data',data=>{output+=data;if(output.length>16384)child.kill();});child.stderr.resume();
   child.on('error',reject);child.on('exit',code=>code===0?accept(JSON.parse(output)):reject(new Error('crypto_helper_failed')));
   child.stdin.end(JSON.stringify({format:'weplay-join-crypto-v1',...value}));
  });
  const clients=[];
  for(let i=0;i<2;i++){
   const keys=await cryptoOperation('keygen',{}),token=crypto.getRandomValues(new Uint8Array(32));
   const ticket=await api('/v1/directory/join','POST',{format:'weplay-join-request-v1',game_id:game.id,content_id:host.content_id,network_revision:7,
    public_key:keys.public_key,challenge:String(i+1).repeat(32),token:Buffer.from(token).toString('base64url')});
   clients.push({keys,ticket,token:Buffer.from(token).toString('base64url')});
  }
  const mailbox=(await api('/v1/directory/host','PUT',host,'Bearer '+grant.key)).requests;assert.equal(mailbox.length,2);
  for(const client of clients){
   const item=mailbox.find(item=>item.id===client.ticket.id);assert.ok(item);
   const context={challenge:item.challenge,expires_at:item.expires_at,game_id:item.game_id,id:item.id};
   const associated=Buffer.from(JSON.stringify(context)).toString('base64url');
   const plaintext=Buffer.from('private synthetic invitation for client '+item.challenge).toString('base64url');
   const envelope=await cryptoOperation('seal',{public_key:item.public_key,associated,plaintext});
   await api('/v1/directory/host/'+host.run_id+'/requests/'+item.id,'PUT',{format:'weplay-join-response-v1',envelope},'Bearer '+grant.key);
   const delivered=await api('/v1/directory/join/'+item.id,'GET',null,'Join '+client.token);
   const opened=await cryptoOperation('open',{private_key:client.keys.private_key,associated,envelope:delivered.envelope});assert.equal(opened.plaintext,plaintext);
   await assert.rejects(cryptoOperation('open',{private_key:clients.find(other=>other!==client).keys.private_key,associated,envelope}));
   await assert.rejects(cryptoOperation('open',{private_key:client.keys.private_key,associated:Buffer.from('wrong context').toString('base64url'),envelope}));
   // Independent WebCrypto consumer verifies the .NET envelope wire format.
   const privateKey=await crypto.subtle.importKey('pkcs8',Buffer.from(client.keys.private_key,'base64url'),{name:'RSA-OAEP',hash:'SHA-256'},false,['decrypt']);
   const aesBytes=await crypto.subtle.decrypt('RSA-OAEP',privateKey,Buffer.from(envelope.key,'base64url'));
   const aes=await crypto.subtle.importKey('raw',aesBytes,'AES-GCM',false,['decrypt']);
   const clear=await crypto.subtle.decrypt({name:'AES-GCM',iv:Buffer.from(envelope.nonce,'base64url'),additionalData:Buffer.from(associated,'base64url')},aes,
     Buffer.concat([Buffer.from(envelope.ciphertext,'base64url'),Buffer.from(envelope.tag,'base64url')]));
   assert.equal(Buffer.from(clear).toString('base64url'),plaintext);
   await api('/v1/directory/join/'+item.id,'DELETE',null,'Join '+client.token);
   assert.equal((await mf.dispatchFetch(origin+'/v1/directory/join/'+item.id,{headers:{Authorization:'Join '+client.token}})).status,410);
  }
  console.log('PASS: anonymous public host and two independent .NET crypto clients through real workerd/D1; wrong keys/context rejected; WebCrypto interoperability; separate private Discord join code.');
 }
 console.log('PASS: real workerd/D1 signed ping, private pairing, reports and board HTTP construction; redirects blocked and all Discord traffic mocked.');
}finally{await mf.dispose();}
