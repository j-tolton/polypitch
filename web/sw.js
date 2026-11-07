// Keeps the app working offline. While online it always prefers the newest deployment, so no version
// needs bumping here. The cache only answers when the network fails or takes too long.
const CACHE='polypitch',WAIT_MS=2500,SLOW_MS=10000;let slowUntil=0;
const FILES=['./','polypitch.js','polypitch.wasm','audio-worklet.js','manifest.webmanifest','icon-192.png','icon-512.png'];
self.addEventListener('install',e=>{e.waitUntil(caches.open(CACHE).then(c=>c.addAll(FILES.map(f=>new Request(f,{cache:'no-cache'})))).then(()=>self.skipWaiting()))});
self.addEventListener('activate',e=>{e.waitUntil(caches.keys().then(keys=>Promise.all(keys.filter(k=>k!==CACHE).map(k=>caches.delete(k)))).then(()=>self.clients.claim()))});
self.addEventListener('fetch',e=>{
 const q=e.request;if(q.method!=='GET'||new URL(q.url).origin!==location.origin)return;
 // Every page address is the one page.
 const key=q.mode==='navigate'?new Request('./'):q;
 e.respondWith((async()=>{
  const cache=await caches.open(CACHE),saved=await cache.match(key,{ignoreSearch:true});
  const fresh=fetch(q).then(r=>{if(r.ok)cache.put(key,r.clone());return r});
  if(!saved)return fresh;
  // A weak signal shouldn't leave you waiting: fall back, while the download still updates the cache.
  // Once one file has been too slow the rest don't wait their turn, or a page load adds up the delays.
  e.waitUntil(fresh.catch(()=>{}));
  if(Date.now()<slowUntil)return saved;
  let timer;
  const late=new Promise(done=>{timer=setTimeout(()=>{slowUntil=Date.now()+SLOW_MS;done(saved)},WAIT_MS)});
  const arrived=fresh.catch(()=>saved).finally(()=>clearTimeout(timer));
  return Promise.race([arrived,late]);
 })());
});
