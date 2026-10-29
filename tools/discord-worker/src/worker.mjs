const encoder = new TextEncoder();
const MARKER = "WePlay · Automatische Multiplayer-Übersicht";
const TTL = 180;
const MAX_PUBLISHERS = 256;
const MAX_ACTIVE = 16;
const json = (value, status = 200) => new Response(JSON.stringify(value), {status,
  headers:{"Content-Type":"application/json; charset=utf-8","Cache-Control":"no-store"}});
const hex = bytes => Array.from(new Uint8Array(bytes), x => x.toString(16).padStart(2,"0")).join("");
const bytes = text => Uint8Array.from(text.match(/../g) || [], x => parseInt(x,16));
const b64 = value => btoa(String.fromCharCode(...new Uint8Array(value))).replaceAll("+","-").replaceAll("/","_").replace(/=+$/,"");
const hash = async text => hex(await crypto.subtle.digest("SHA-256",encoder.encode(text)));
const id = text => typeof text === "string" && /^[1-9][0-9]{16,19}$/.test(text) && BigInt(text) <= 18446744073709551615n;
const exact = (value, keys) => value && typeof value === "object" && !Array.isArray(value) &&
  Object.keys(value).length === keys.length && keys.every(key => Object.hasOwn(value,key));
class Refusal extends Error { constructor(status=400){super("request_refused");this.status=status;} }
const parse = raw => {try{return JSON.parse(raw);}catch{throw new Refusal();}};

export function origin(env) {
  if (!/^https:\/\/weplay-multiplayer\.[a-z0-9-]+\.workers\.dev$/.test(env.PUBLIC_ORIGIN || "")) throw new Refusal(503);
  return env.PUBLIC_ORIGIN;
}

async function body(request, limit) {
  if (Number(request.headers.get("Content-Length")) > limit) throw new Refusal(413);
  if (!request.body) return "";
  const reader=request.body.getReader();const chunks=[];let size=0;
  try {
    while(true){const part=await reader.read();if(part.done)break;size+=part.value.length;
      if(size>limit){await reader.cancel();throw new Refusal(413);}chunks.push(part.value);}
  } finally {reader.releaseLock();}
  const result=new Uint8Array(size);let offset=0;for(const chunk of chunks){result.set(chunk,offset);offset+=chunk.length;}
  return new TextDecoder("utf-8",{fatal:true}).decode(result);
}

async function verifiedInteraction(request, env, now) {
  const signature=request.headers.get("X-Signature-Ed25519") || "";
  const timestamp=request.headers.get("X-Signature-Timestamp") || "";
  if (!/^[0-9a-f]{128}$/i.test(signature) || !/^[0-9]{10,12}$/.test(timestamp) ||
      Math.abs(now-Number(timestamp))>300 || !/^[0-9a-f]{64}$/i.test(env.DISCORD_PUBLIC_KEY || "")) throw new Refusal(401);
  const raw=await body(request,65536);
  const key=await crypto.subtle.importKey("raw",bytes(env.DISCORD_PUBLIC_KEY),"Ed25519",false,["verify"]);
  if (!await crypto.subtle.verify("Ed25519",key,bytes(signature),encoder.encode(timestamp+raw))) throw new Refusal(401);
  return parse(raw);
}

async function hmac(env, text) {
  if (!/^[A-Za-z0-9_-]{43}$/.test(env.PUBLISHER_MASTER_KEY || "")) throw new Refusal(503);
  const raw=Uint8Array.from(atob(env.PUBLISHER_MASTER_KEY.replaceAll("-","+").replaceAll("_","/")+"="), c=>c.charCodeAt(0));
  const key=await crypto.subtle.importKey("raw",raw,{name:"HMAC",hash:"SHA-256"},false,["sign"]);
  return new Uint8Array(await crypto.subtle.sign("HMAC",key,encoder.encode(text)));
}

export function games(value) {
  if (!Array.isArray(value) || value.length>8) throw new Refusal();
  const seen=new Set();
  return value.map(game=>{
    if (!exact(game,["id","title","state","players","capacity","free_slots"]) ||
        typeof game.id!=="string" || !/^[0-9a-f]{8}$/.test(game.id) || game.id==="00000000" || seen.has(game.id) ||
        typeof game.title!=="string" || game.title.length<1 || game.title.length>48 || game.title!==game.title.trim() ||
        /[\p{Cc}\p{Cf}]/u.test(game.title) || /@|:\/\/|weplay1\.|discord\.gg/i.test(game.title) ||
        ![game.players,game.capacity,game.free_slots].every(Number.isInteger) ||
        game.capacity<1 || game.capacity>4 || game.players<1 || game.players>game.capacity ||
        game.free_slots<0 || game.free_slots>game.capacity-game.players ||
        !["open","full","started"].includes(game.state) ||
        (game.state==="open" ? game.free_slots===0 : game.free_slots!==0)) throw new Refusal();
    seen.add(game.id);
    return {id:game.id,title:game.title,state:game.state,players:game.players,capacity:game.capacity,free_slots:game.free_slots};
  });
}

async function connect(interaction,env,now) {
  const user=interaction.member?.user;
  if (!id(user?.id) || user.bot || !id(interaction.id)) throw new Refusal(403);
  const owner=hex(await hmac(env,"owner:"+user.id));
  const token=b64(await hmac(env,env.APPLICATION_ID+":"+env.GUILD_ID+":"+user.id+":"+interaction.id));
  const tokenHash=await hash(token);const event=interaction.id.padStart(20,"0");
  await env.DB.batch([
    env.DB.prepare("UPDATE weplay_reports SET closed=1,expires_at=? WHERE owner=? AND EXISTS(SELECT 1 FROM weplay_publishers WHERE owner=? AND token_hash<>? AND last_interaction<?)")
      .bind(now,owner,owner,tokenHash,event),
    env.DB.prepare(`INSERT INTO weplay_publishers(owner,token_hash,last_interaction,issued_at)
      SELECT ?,?,?,? WHERE (SELECT COUNT(*) FROM weplay_publishers)<? OR EXISTS(SELECT 1 FROM weplay_publishers WHERE owner=?)
      ON CONFLICT(owner) DO UPDATE SET token_hash=excluded.token_hash,last_interaction=excluded.last_interaction,issued_at=excluded.issued_at
      WHERE excluded.last_interaction>=weplay_publishers.last_interaction`).bind(owner,tokenHash,event,now,MAX_PUBLISHERS,owner)
  ]);
  const current=await env.DB.prepare("SELECT token_hash FROM weplay_publishers WHERE owner=?").bind(owner).first();
  if(current?.token_hash!==tokenHash)throw new Refusal(409);
  const code="weplay-connect1."+b64(encoder.encode(JSON.stringify({v:1,origin:origin(env),key:token})));
  return {type:4,data:{flags:64,allowed_mentions:{parse:[]},content:
    "Füge diesen privaten Code im WePlay-Starter unter ‚Mit Discord verbinden‘ ein. Er erlaubt nur das Melden deines Spielstatus. " +
    "Eine neue Verbindung ersetzt deine bisherige Berechtigung. Nicht öffentlich teilen.\n```\n"+code+"\n```"}};
}

export async function board(env,now) {
  const result=await env.DB.prepare("SELECT payload FROM weplay_reports WHERE closed=0 AND expires_at>? ORDER BY owner LIMIT 16").bind(now).all();
  const rows=[];for(const row of result.results){rows.push(...games(JSON.parse(row.payload)));}
  const rank={open:0,full:1,started:2};rows.sort((a,b)=>rank[a.state]-rank[b.state] || a.title.localeCompare(b.title) || a.id.localeCompare(b.id));
  const counts={open:0,full:0,started:0};for(const row of rows)counts[row.state]++;
  const names={open:"Offen",full:"Voll",started:"Gestartet / lädt"};
  const escape=text=>text.replace(/[\\`*_~|>\[\]]/g,"\\$&");
  return {title:"WePlay · Multiplayer",description:rows.length ?
      `**Offen: ${counts.open} · Voll: ${counts.full} · Gestartet/lädt: ${counts.started}**\n`+
      "Vom Launcher gemeldete öffentliche Spiele. ‚Gestartet/lädt‘ bestätigt keinen Simulationsfortschritt." :
      "Aktuell keine frisch gemeldeten öffentlichen Spiele. Private und nicht verbundene Launcher sind nicht enthalten.",
    fields:rows.slice(0,16).map(row=>({name:escape(row.title),value:
      `**${names[row.state]}** · Spieler ${row.players}/${row.capacity}`+
      (row.state==="open" ? ` · freie Plätze ${row.free_slots}` : ""),inline:false})),
    color:0x1e5ba4,footer:{text:MARKER+(rows.length>16 ? ` · ${rows.length-16} weitere Spiele` : "")}};
}

async function discord(env,path,options={}) {
  if(typeof env.BOT_TOKEN!=="string" || env.BOT_TOKEN.length<20 || env.BOT_TOKEN.length>512)throw new Refusal(503);
  const response=await fetch("https://discord.com/api/v10"+path,{...options,redirect:"error",signal:AbortSignal.timeout(8000),
    headers:{"Authorization":"Bot "+env.BOT_TOKEN,"Content-Type":"application/json","User-Agent":"WePlayDirectory/1.0"}});
  const raw=await response.text();if(raw.length>131072)throw new Refusal(503);
  let value;try{value=raw ? JSON.parse(raw) : {};}catch{throw new Refusal(503);}
  if(response.status===429){const error=new Refusal(503);error.retry=Math.min(3600,Math.max(5,Math.ceil(Number(value.retry_after)||60)));throw error;}
  if(!response.ok){const error=new Refusal(503);error.missing=response.status===404;throw error;}
  return value;
}

export async function updateBoard(env,now=Math.floor(Date.now()/1000)) {
  const lock=await env.DB.prepare("UPDATE weplay_board SET lease_until=? WHERE slot=1 AND lease_until<=? RETURNING message_id,digest")
    .bind(now+60,now).first();if(!lock)return;
  let retry=5;
  try {
    const embed=await board(env,now);const digest=await hash(JSON.stringify(embed));
    if(lock.digest===digest && lock.message_id)return;
    const app=await discord(env,"/oauth2/applications/@me");
    if(app.id!==env.APPLICATION_ID || !id(app.bot?.id))throw new Refusal(503);
    const channel=await discord(env,"/channels/"+env.CHANNEL_ID);
    if(channel.guild_id!==env.GUILD_ID || channel.type!==0)throw new Refusal(503);
    let messageId=lock.message_id;
    if(messageId){
      try {
        const previous=await discord(env,"/channels/"+env.CHANNEL_ID+"/messages/"+messageId);
        if(previous.author?.id!==app.bot.id || !previous.embeds?.[0]?.footer?.text?.startsWith(MARKER))throw new Refusal(503);
      } catch(error){if(error.missing)messageId=null;else throw error;}
    }
    const payload=JSON.stringify({content:"",embeds:[embed],allowed_mentions:{parse:[]}});
    const sent=await discord(env,"/channels/"+env.CHANNEL_ID+"/messages"+(messageId?"/"+messageId:""),
                             {method:messageId?"PATCH":"POST",body:payload});
    if(!id(sent.id))throw new Refusal(503);
    await env.DB.prepare("UPDATE weplay_board SET message_id=?,digest=? WHERE slot=1").bind(sent.id,digest).run();
  } catch(error){retry=error.retry || 30;}
  finally {await env.DB.prepare("UPDATE weplay_board SET lease_until=? WHERE slot=1").bind(now+retry).run();}
}

async function publisher(request,env) {
  const auth=request.headers.get("Authorization") || "";
  if(!/^Bearer [A-Za-z0-9_-]{43}$/.test(auth))throw new Refusal(401);
  const value=await env.DB.prepare("SELECT owner FROM weplay_publishers WHERE token_hash=?").bind(await hash(auth.slice(7))).first();
  if(!value)throw new Refusal(401);return value;
}

async function report(request,env,ctx,now) {
  const identity=await publisher(request,env);
  const value=parse(await body(request,8192));
  if(!exact(value,["format","run_id","sequence","games"]) || value.format!=="weplay-game-report-v1" ||
     typeof value.run_id!=="string" || !/^[0-9a-f]{32}$/.test(value.run_id) || !Number.isInteger(value.sequence) || value.sequence<0 || value.sequence>2147483647)throw new Refusal();
  const payload=JSON.stringify(games(value.games));
  if(request.method==="POST"){
    if(value.sequence!==0 || value.games.length)throw new Refusal();
    await env.DB.prepare(`INSERT INTO weplay_reports(owner,run_id,sequence,payload,expires_at,closed)
      SELECT ?,?,0,'[]',?,0 WHERE (SELECT COUNT(*) FROM weplay_reports WHERE closed=0 AND expires_at>? AND owner<>?)<?
      ON CONFLICT(owner) DO UPDATE SET run_id=excluded.run_id,
        sequence=CASE WHEN weplay_reports.run_id=excluded.run_id THEN weplay_reports.sequence ELSE 0 END,
        payload=CASE WHEN weplay_reports.run_id=excluded.run_id THEN weplay_reports.payload ELSE '[]' END,
        expires_at=excluded.expires_at,closed=0
      WHERE (weplay_reports.run_id<>excluded.run_id AND (weplay_reports.closed=1 OR weplay_reports.expires_at<=?))
        OR (weplay_reports.run_id=excluded.run_id AND weplay_reports.closed=0 AND weplay_reports.expires_at<=?)`)
      .bind(identity.owner,value.run_id,now+TTL,now,identity.owner,MAX_ACTIVE,now,now).run();
  } else if(request.method==="PUT"){
    await env.DB.prepare("UPDATE weplay_reports SET sequence=?,payload=?,expires_at=? WHERE owner=? AND run_id=? AND closed=0 AND expires_at>? AND sequence<?")
      .bind(value.sequence,payload,now+TTL,identity.owner,value.run_id,now,value.sequence).run();
  } else if(request.method==="DELETE"){
    if(value.games.length)throw new Refusal();
    await env.DB.prepare("UPDATE weplay_reports SET sequence=?,payload='[]',expires_at=?,closed=1 WHERE owner=? AND run_id=? AND sequence<?")
      .bind(value.sequence,now,identity.owner,value.run_id,value.sequence).run();
  } else throw new Refusal(405);
  const current=await env.DB.prepare("SELECT run_id,sequence,payload,closed,expires_at FROM weplay_reports WHERE owner=?").bind(identity.owner).first();
  if(!current || current.run_id!==value.run_id ||
     (request.method==="PUT" && (current.sequence!==value.sequence || current.payload!==payload || current.closed || current.expires_at<=now)) ||
     (request.method==="POST" && (current.closed || current.expires_at<=now)) || (request.method==="DELETE" && !current.closed))throw new Refusal(409);
  ctx.waitUntil(updateBoard(env,now));
  return json({ok:true});
}

export default {
  async fetch(request,env,ctx) {
    try {
      const url=new URL(request.url);const now=Math.floor(Date.now()/1000);
      if(url.origin!==origin(env) || url.search)throw new Refusal(400);
      if(url.pathname==="/health" && request.method==="GET"){
        await env.DB.prepare("SELECT slot FROM weplay_board WHERE slot=1").first();
        const ready=typeof env.BOT_TOKEN==="string" && env.BOT_TOKEN.length>=20 && env.BOT_TOKEN.length<=512 &&
          /^[A-Za-z0-9_-]{43}$/.test(env.PUBLISHER_MASTER_KEY||"") && /^[0-9a-f]{64}$/i.test(env.DISCORD_PUBLIC_KEY||"");
        return json({service:"weplay-directory",ready},ready?200:503);
      }
      if(url.pathname==="/v1/report")return await report(request,env,ctx,now);
      if(url.pathname==="/v1/publisher" && request.method==="GET"){await publisher(request,env);return json({ok:true});}
      if(url.pathname!=="/interactions" || request.method!=="POST")throw new Refusal(404);
      const interaction=await verifiedInteraction(request,env,now);
      if(interaction.type===1)return json({type:1});
      if(interaction.application_id!==env.APPLICATION_ID || interaction.guild_id!==env.GUILD_ID ||
         interaction.channel_id!==env.CHANNEL_ID || !interaction.member)throw new Refusal(403);
      if(interaction.type===3 && interaction.data?.custom_id==="weplay:connect")return json(await connect(interaction,env,now));
      if(interaction.type!==2)throw new Refusal();
      const common={allowed_mentions:{parse:[]}};
      if(interaction.data?.name==="spiele")return json({type:4,data:{...common,embeds:[await board(env,now)]}});
      if(interaction.data?.name==="weplay-status")return json({type:4,data:{...common,flags:64,
        content:"Der externe WePlay-Dienst ist erreichbar. Er benötigt keinen laufenden privaten Bot-PC. Spiele werden nur durch verbundene Launcher gemeldet."}});
      if(interaction.data?.name==="weplay-hilfe")return json({type:4,data:{...common,flags:64,
        content:"Verbinde deinen WePlay-Starter und aktiviere dort die öffentliche Spielstatus-Anzeige. Private Raumcodes bleiben privat. Meldungen verfallen nach drei Minuten ohne Lebenszeichen.",
        components:[{type:1,components:[{type:2,style:1,label:"Launcher verbinden",custom_id:"weplay:connect"}]}]}});
      throw new Refusal();
    } catch(error){return json({error:"request_refused"},error instanceof Refusal?error.status:503);}
  },
  async scheduled(controller,env,ctx){ctx.waitUntil(updateBoard(env));}
};
