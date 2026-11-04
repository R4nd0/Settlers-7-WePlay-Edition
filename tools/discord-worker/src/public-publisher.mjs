import {directoryRate,DirectoryRefusal} from "./directory.mjs";

const digest=async value=>Array.from(new Uint8Array(await crypto.subtle.digest("SHA-256",new TextEncoder().encode(value))),x=>x.toString(16).padStart(2,"0")).join("");
export const PUBLIC_TTL=300;

export async function registerPublicPublisher(request,env,now,readBody){
  if(request.method!=="POST")throw new DirectoryRefusal(405);
  const raw=await readBody(request,512);let value;
  try{value=JSON.parse(raw);}catch{throw new DirectoryRefusal(400);}
  if(!value||Array.isArray(value)||Object.keys(value).sort().join(",")!=="format,token"||
     value.format!=="weplay-public-host-v1"||typeof value.token!=="string"||!/^[A-Za-z0-9_-]{43}$/.test(value.token))
    throw new DirectoryRefusal(400);
  const tokenHash=await digest(value.token),owner=await digest("public-host:"+value.token);
  // Retries with the same in-memory secret do not create another identity.
  const existing=await env.DB.prepare(`SELECT p.owner FROM weplay_publishers p JOIN weplay_public_publishers a ON a.owner=p.owner
    WHERE p.owner=? AND p.token_hash=? AND a.expires_at>?`).bind(owner,tokenHash,now).first();
  if(existing)return {ok:true};
  await directoryRate(env,request,now,"public-host",60,6);
  await env.DB.batch([
    env.DB.prepare(`INSERT INTO weplay_publishers(owner,token_hash,last_interaction,issued_at)
      SELECT ?,?,'public',? WHERE (SELECT COUNT(*) FROM weplay_public_publishers)<64
      ON CONFLICT(owner) DO NOTHING`).bind(owner,tokenHash,now),
    env.DB.prepare(`INSERT INTO weplay_public_publishers(owner,expires_at)
      SELECT ?,? WHERE EXISTS(SELECT 1 FROM weplay_publishers WHERE owner=? AND token_hash=? AND last_interaction='public')
      ON CONFLICT(owner) DO UPDATE SET expires_at=excluded.expires_at`).bind(owner,now+PUBLIC_TTL,owner,tokenHash)
  ]);
  const created=await env.DB.prepare("SELECT expires_at FROM weplay_public_publishers WHERE owner=?").bind(owner).first();
  if(!created||created.expires_at<=now)throw new DirectoryRefusal(429);
  return {ok:true};
}

export async function expirePublicPublishers(env,now){
  await env.DB.prepare(`DELETE FROM weplay_publishers WHERE owner IN
    (SELECT owner FROM weplay_public_publishers WHERE expires_at<=?)`).bind(now).run();
}
