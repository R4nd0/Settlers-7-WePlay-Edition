// Original WePlay implementation. Public discovery never carries an EasyTier secret.
const encode=new TextEncoder();
const MAX_REQUESTS=64,JOIN_TTL=45,HOST_TTL=35;
const hex=bytes=>Array.from(new Uint8Array(bytes),x=>x.toString(16).padStart(2,"0")).join("");
const sha=async text=>hex(await crypto.subtle.digest("SHA-256",encode.encode(text)));
const isHex=(value,n)=>typeof value==="string"&&new RegExp(`^[0-9a-f]{${n}}$`).test(value);
const exact=(value,keys)=>value&&typeof value==="object"&&!Array.isArray(value)&&
  Object.keys(value).length===keys.length&&keys.every(key=>Object.hasOwn(value,key));
const b64=(value,min,max)=>typeof value==="string"&&value.length>=min&&value.length<=max&&/^[A-Za-z0-9_-]+$/.test(value);
export class DirectoryRefusal extends Error{constructor(status=400){super("directory_refused");this.status=status;}}
const fail=status=>{throw new DirectoryRefusal(status);};
const reply=(value,status=200)=>new Response(JSON.stringify(value),{status,headers:{"Content-Type":"application/json; charset=utf-8","Cache-Control":"no-store"}});

async function input(request,limit=8192){
  if(Number(request.headers.get("Content-Length"))>limit)fail(413);
  if(!request.body)fail(400);
  const reader=request.body.getReader();let size=0;const chunks=[];
  try{while(true){const value=await reader.read();if(value.done)break;size+=value.value.length;
    if(size>limit){await reader.cancel();fail(413);}chunks.push(value.value);}}
  finally{reader.releaseLock();}
  const raw=new Uint8Array(size);let offset=0;for(const chunk of chunks){raw.set(chunk,offset);offset+=chunk.length;}
  try{return JSON.parse(new TextDecoder("utf-8",{fatal:true}).decode(raw));}catch{fail(400);}
}

export async function directoryGames(env,now,validateGames){
  const result=await env.DB.prepare(`SELECT h.*,r.payload FROM weplay_directory_hosts h
    JOIN weplay_reports r ON r.owner=h.owner AND r.run_id=h.run_id
    WHERE h.expires_at>? AND r.expires_at>? AND r.closed=0 ORDER BY h.owner LIMIT 16`).bind(now,now).all();
  const rows=[];
  for(const host of result.results){
    for(const game of validateGames(JSON.parse(host.payload))){
      const id=(await sha(host.owner+":"+host.run_id+":"+game.id)).slice(0,32);
      rows.push({...game,id,network_revision:host.network_revision,content_id:host.content_id,
        version:host.version,expires_at:Math.min(host.expires_at,now+35),_host:host,_session:game.id});
    }
  }
  rows.sort((a,b)=>a.title.localeCompare(b.title)||a.id.localeCompare(b.id));
  return rows;
}
export function publicGame(row){const { _host,_session,...value}=row;return value;}
export async function expireDirectory(env,now){
  await env.DB.batch([
    env.DB.prepare(`DELETE FROM weplay_join_requests WHERE expires_at<=? OR NOT EXISTS
      (SELECT 1 FROM weplay_directory_hosts h WHERE h.owner=weplay_join_requests.owner
       AND h.run_id=weplay_join_requests.run_id AND h.expires_at>?)`).bind(now,now),
    env.DB.prepare("DELETE FROM weplay_directory_hosts WHERE expires_at<=?").bind(now),
    env.DB.prepare("DELETE FROM weplay_join_rates WHERE expires_at<=?").bind(now)
  ]);
}
function envelope(value){
  if(!exact(value,["scheme","key","nonce","ciphertext","tag"])||value.scheme!=="rsa3072-oaep256-a256gcm-v1"||
     !b64(value.key,512,512)||!b64(value.nonce,16,16)||!b64(value.ciphertext,1,5462)||!b64(value.tag,22,22))fail(400);
  return JSON.stringify(value);
}
async function liveHost(env,owner,run,now){
  const host=await env.DB.prepare(`SELECT h.* FROM weplay_directory_hosts h JOIN weplay_reports r
    ON r.owner=h.owner AND r.run_id=h.run_id WHERE h.owner=? AND h.run_id=? AND h.expires_at>?
    AND r.closed=0 AND r.expires_at>?`).bind(owner,run,now,now).first();
  if(!host)fail(409);return host;
}
async function guest(env,request,id,now){
  const auth=request.headers.get("Authorization")||"";
  if(!/^Join [A-Za-z0-9_-]{43}$/.test(auth))fail(401);
  const row=await env.DB.prepare("SELECT * FROM weplay_join_requests WHERE id=? AND token_hash=?")
    .bind(id,await sha(auth.slice(5))).first();
  if(!row)fail(404);
  if(row.expires_at<=now)fail(410);
  await liveHost(env,row.owner,row.run_id,now);
  return row;
}
async function rate(env,request,now){
  // Only a short-lived keyed source bucket is retained, never the source address.
  const source=request.headers.get("CF-Connecting-IP")||"local";
  if(source.length>64||/[\s\x00-\x1f]/.test(source))fail(400);
  const key=await crypto.subtle.importKey("raw",encode.encode(env.PUBLISHER_MASTER_KEY||""),{name:"HMAC",hash:"SHA-256"},false,["sign"]);
  const bucket=hex(await crypto.subtle.sign("HMAC",key,encode.encode("join:"+Math.floor(now/60)+":"+source)));
  await env.DB.batch([
    env.DB.prepare("DELETE FROM weplay_join_rates WHERE expires_at<=?").bind(now),
    env.DB.prepare(`INSERT INTO weplay_join_rates VALUES(?,1,?) ON CONFLICT(bucket) DO UPDATE SET count=count+1`).bind(bucket,now+120)
  ]);
  const value=await env.DB.prepare("SELECT count FROM weplay_join_rates WHERE bucket=?").bind(bucket).first();
  if(value.count>12)fail(429);
}

export async function directoryRequest(request,env,now,{publisher,games}){
  const path=new URL(request.url).pathname;
  if(!path.startsWith("/v1/directory/"))return null;
  if(path==="/v1/directory/games"&&request.method==="GET")
    return reply({format:"weplay-games-directory-v1",games:(await directoryGames(env,now,games)).map(publicGame)});
  if(path==="/v1/directory/host"){
    const {owner}=await publisher(request,env);const value=await input(request);
    if(request.method==="DELETE"){
      if(!exact(value,["run_id"])||!isHex(value.run_id,32))fail(400);
      await env.DB.batch([
        env.DB.prepare("DELETE FROM weplay_join_requests WHERE owner=? AND run_id=?").bind(owner,value.run_id),
        env.DB.prepare("DELETE FROM weplay_directory_hosts WHERE owner=? AND run_id=?").bind(owner,value.run_id)
      ]);return reply({ok:true});
    }
    if(request.method!=="PUT")fail(405);
    if(!exact(value,["format","run_id","room_id","content_id","network_revision","version"])||
       value.format!=="weplay-directory-host-v1"||!isHex(value.run_id,32)||!isHex(value.room_id,32)||
       !isHex(value.content_id,64)||value.network_revision!==7||typeof value.version!=="string"||
       !/^2\.0\.0-rc[0-9]+(?:\.[0-9]+)?$/.test(value.version)||value.version.length>40)fail(400);
    await env.DB.prepare(`INSERT INTO weplay_directory_hosts(owner,run_id,room_id,content_id,network_revision,version,expires_at)
      SELECT ?,?,?,?,?,?,? WHERE EXISTS(SELECT 1 FROM weplay_reports WHERE owner=? AND run_id=? AND closed=0 AND expires_at>?)
      ON CONFLICT(owner) DO UPDATE SET run_id=excluded.run_id,room_id=excluded.room_id,content_id=excluded.content_id,
      network_revision=excluded.network_revision,version=excluded.version,expires_at=excluded.expires_at
      WHERE weplay_directory_hosts.run_id<>excluded.run_id OR
        (weplay_directory_hosts.room_id=excluded.room_id AND weplay_directory_hosts.content_id=excluded.content_id)`).bind(
        owner,value.run_id,value.room_id,value.content_id,7,value.version,now+HOST_TTL,owner,value.run_id,now).run();
    const host=await liveHost(env,owner,value.run_id,now);
    if(host.room_id!==value.room_id||host.content_id!==value.content_id)fail(409);
    await env.DB.prepare("DELETE FROM weplay_join_requests WHERE expires_at<=?").bind(now).run();
    const pending=await env.DB.prepare(`SELECT id,session_id,game_id,public_key,challenge,expires_at FROM weplay_join_requests
      WHERE owner=? AND run_id=? AND envelope IS NULL AND consumed=0 AND expires_at>? ORDER BY created_at LIMIT 8`)
      .bind(owner,value.run_id,now).all();
    return reply({ok:true,requests:pending.results});
  }
  const hostReply=path.match(/^\/v1\/directory\/host\/([0-9a-f]{32})\/requests\/([0-9a-f]{32})$/);
  if(hostReply&&request.method==="PUT"){
    const {owner}=await publisher(request,env);await liveHost(env,owner,hostReply[1],now);
    const value=await input(request);
    if(!exact(value,["format","envelope"])||value.format!=="weplay-join-response-v1")fail(400);
    if(value.envelope===null){
      await env.DB.prepare("UPDATE weplay_join_requests SET consumed=1,envelope=NULL WHERE id=? AND owner=? AND run_id=? AND expires_at>?")
        .bind(hostReply[2],owner,hostReply[1],now).run();return reply({ok:true});
    }
    const encoded=envelope(value.envelope);
    await env.DB.prepare(`UPDATE weplay_join_requests SET envelope=? WHERE id=? AND owner=? AND run_id=?
      AND consumed=0 AND expires_at>? AND (envelope IS NULL OR envelope=?)`)
      .bind(encoded,hostReply[2],owner,hostReply[1],now,encoded).run();
    const current=await env.DB.prepare("SELECT envelope FROM weplay_join_requests WHERE id=? AND owner=? AND run_id=? AND consumed=0 AND expires_at>?")
      .bind(hostReply[2],owner,hostReply[1],now).first();
    if(!current||current.envelope!==encoded)fail(409);
    return reply({ok:true});
  }
  if(path==="/v1/directory/join"&&request.method==="POST"){
    await rate(env,request,now);const value=await input(request,4096);
    if(!exact(value,["format","game_id","content_id","network_revision","public_key","challenge","token"])||
       value.format!=="weplay-join-request-v1"||!isHex(value.game_id,32)||!isHex(value.content_id,64)||
       value.network_revision!==7||!b64(value.public_key,500,700)||!isHex(value.challenge,32)||!b64(value.token,43,43))fail(400);
    // Require an actual RSA3072 public key; malformed inputs never enter host mailboxes.
    try{const binary=Uint8Array.from(atob(value.public_key.replaceAll("-","+").replaceAll("_","/")+"=".repeat((4-value.public_key.length%4)%4)),c=>c.charCodeAt(0));
      const key=await crypto.subtle.importKey("spki",binary,{name:"RSA-OAEP",hash:"SHA-256"},false,["encrypt"]);
      if(key.algorithm.modulusLength!==3072||hex(key.algorithm.publicExponent)!=="010001")fail(400);
    }catch{fail(400);}
    const row=(await directoryGames(env,now,games)).find(game=>game.id===value.game_id);
    if(!row)fail(404);
    if(row.state!=="open"||row.free_slots<1||row.content_id!==value.content_id||row.network_revision!==value.network_revision)fail(409);
    const id=hex(crypto.getRandomValues(new Uint8Array(16))),host=row._host,until=now+JOIN_TTL;
    await env.DB.prepare(`INSERT INTO weplay_join_requests
      (id,owner,run_id,session_id,game_id,token_hash,public_key,challenge,created_at,expires_at)
      SELECT ?,?,?,?,?,?,?,?,?,? WHERE
        (SELECT COUNT(*) FROM weplay_join_requests WHERE owner=? AND run_id=? AND session_id=? AND consumed=0 AND expires_at>?)<?
        AND (SELECT COUNT(*) FROM weplay_join_requests WHERE consumed=0 AND expires_at>?)<?`)
      .bind(id,host.owner,host.run_id,row._session,row.id,await sha(value.token),value.public_key,value.challenge,now,until,
            host.owner,host.run_id,row._session,now,row.free_slots,now,MAX_REQUESTS).run();
    if(!await env.DB.prepare("SELECT id FROM weplay_join_requests WHERE id=?").bind(id).first())fail(429);
    return reply({format:"weplay-join-ticket-v1",id,game_id:row.id,challenge:value.challenge,expires_at:until});
  }
  const poll=path.match(/^\/v1\/directory\/join\/([0-9a-f]{32})$/);
  if(poll){
    const row=await guest(env,request,poll[1],now);
    if(request.method==="DELETE"){
      await env.DB.prepare("UPDATE weplay_join_requests SET consumed=1,envelope=NULL WHERE id=? AND token_hash=?")
        .bind(row.id,row.token_hash).run();return reply({ok:true});
    }
    if(request.method!=="GET")fail(405);
    if(row.consumed)fail(410);
    return reply({format:"weplay-join-delivery-v1",id:row.id,game_id:row.game_id,challenge:row.challenge,
      expires_at:row.expires_at,envelope:row.envelope?JSON.parse(row.envelope):null});
  }
  fail(404);
}
