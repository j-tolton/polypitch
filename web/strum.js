// Reads every string of a strummed chord at once. Most strings are read from their own note. A string
// whose note is also an overtone of a lower string (in standard tuning the B and top E sit on overtones
// of the low E and A) is read from its whole series of overtones instead, each string allowed its own
// stretch, since that is what tells it apart. Shared by the app and its tests.
(function(root){
'use strict';
const hz=m=>440*2**((m-69)/12);
const KMAX=8,FMAX=3000,STRETCH=[0,2e-5,4e-5,7e-5,1e-4,1.4e-4,1.8e-4],PEAK_REACH=70,SERIES_REACH=100,CLASH_CENTS=120;
// A string counts as heard well above the background, and not far below the loudest string.
const HEARD_OVER_FLOOR=12,HEARD_UNDER_TOP=150;
function fft(re,im){
 const n=re.length;
 for(let i=1,j=0;i<n;i++){let b=n>>1;for(;j&b;b>>=1)j^=b;j^=b;if(i<j){let t=re[i];re[i]=re[j];re[j]=t;t=im[i];im[i]=im[j];im[j]=t}}
 for(let len=2;len<=n;len<<=1){
  const a=-2*Math.PI/len,wr=Math.cos(a),wi=Math.sin(a),half=len>>1;
  for(let i=0;i<n;i+=len){let cr=1,ci=0;for(let j=0;j<half;j++){const p=i+j,q=p+half,vr=re[q]*cr-im[q]*ci,vi=re[q]*ci+im[q]*cr;re[q]=re[p]-vr;im[q]=im[p]-vi;re[p]+=vr;im[p]+=vi;const t=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=t}}
 }
}
function partials(f0,B){const out=[];for(let k=1;k<=KMAX&&k*f0<=FMAX;k++)out.push(k*f0*Math.sqrt(1+B*k*k));return out}
// Which strings' notes lie within a semitone of an overtone of a lower string.
function clashes(notes){return notes.map(m=>notes.some(o=>o<m-.5&&[2,3,4,5,6].some(k=>Math.abs(1200*Math.log2(k*hz(o)/hz(m)))<CLASH_CENTS)))}
// x: the samples to read, all of them under one window. notes: each string's target as a MIDI number,
// fractional to allow for the reference pitch. Returns each string's pitch, its cents from the target,
// and whether it was heard at all.
function read(x,sr,notes){
 const N=x.length&~1,h=N>>1;let pad=1;while(pad<N*3.5)pad<<=1;
 const re=new Float64Array(pad),im=new Float64Array(pad);
 // Centred on time zero, so a steady partial's peak is symmetric.
 for(let n=-h;n<h;n++)re[(n+pad)%pad]=x[h+n]*(.5+.5*Math.cos(Math.PI*n/h));
 fft(re,im);
 const df=sr/pad,M=new Float64Array(pad>>1);for(let i=0;i<M.length;i++)M[i]=Math.hypot(re[i],im[i]);
 const mag=f=>{const b=f/df,i=Math.floor(b),t=b-i;return i<0||i+1>=M.length?0:M[i]*(1-t)+M[i+1]*t};
 const lm=i=>Math.log(M[i]+1e-12);
 const bg=[];for(let f=60;f<2000;f+=df*7)bg.push(mag(f));bg.sort((a,b)=>a-b);const floor=bg[bg.length>>1]||1e-12;
 const clash=clashes(notes);
 const out=notes.map((m,s)=>{
  const f=hz(m);let est=f,edge=false;
  // An overtone no other string has one near.
  const own=p=>!notes.some((o,t)=>t!==s&&o!==m&&[1,2,3,4,5,6,7,8].some(j=>Math.abs(1200*Math.log2(j*hz(o)/p))<40));
  if(!clash[s]){
   // The tallest peak near the note and near each of its first clean overtones, a parabola through each
   // for the exact place, and the stronger ones count for more. A quiet string's own note can be weaker
   // than its overtones, so they steady it.
   let sum=0,weight=0;
   for(let k=1;k<=3;k++){
    const fk=k*f*Math.sqrt(1+5e-5*k*k);if(k>1&&(!own(fk)||fk>FMAX))continue;
    let top=-1,at=fk;for(let c=-PEAK_REACH;c<=PEAK_REACH;c+=.5){const g=fk*2**(c/1200),v=mag(g);if(v>top){top=v;at=g}}
    const i=Math.round(at/df),a=lm(i-1),b=lm(i),c=lm(i+1),off=(a-c)/(2*(a-2*b+c)),cents=1200*Math.log2((i+(Number.isFinite(off)&&Math.abs(off)<1?off:0))*df/fk);
    // Pinned to the edge of the search means nothing was found there.
    if(Math.abs(cents)>PEAK_REACH-2){if(k===1)edge=true;continue}
    sum+=cents*top*top;weight+=top*top;
   }
   if(weight)est=f*2**(sum/weight/1200);
  }else{
   // The pitch, and stretch, whose overtones stand out most taken together.
   let best=-Infinity;
   for(const B of STRETCH){
    const score=g=>{let v=0;for(const p of partials(g,B))v+=Math.log(mag(p)+1e-12);return v};
    let bc=0,bv=-Infinity;
    for(let c=-SERIES_REACH;c<=SERIES_REACH;c++){const v=score(f*2**(c/1200));if(v>bv){bv=v;bc=c}}
    for(let st=.5;st>.02;st/=2)for(const c of [bc-st,bc+st]){const v=score(f*2**(c/1200));if(v>bv){bv=v;bc=c}}
    if(bv>best){best=bv;est=f*2**(bc/1200)}
   }
  }
  // Heard means its note, or one of its first overtones that no other string shares, is there. (Every
  // overtone of an unplayed top E belongs to the low strings, so it can't be told apart from silence.)
  let level=0;
  if(!edge)partials(est,5e-5).slice(0,3).forEach((p,k)=>{if(!k||own(p))level=Math.max(level,mag(p))});
  return {target:m,hz:est,cents:1200*Math.log2(est/f),level:level/floor};
 });
 const loudest=Math.max(...out.map(o=>o.level));
 for(const o of out)o.heard=o.level>=HEARD_OVER_FLOOR&&o.level>=loudest/HEARD_UNDER_TOP;
 return out;
}
// Follows strums as the sound arrives. A strum is the level doubling within a sixth of a second, or a
// slow strum still building; the strings are left a moment to settle (they start sharp), then read again
// and again while they ring. Each string shows the middle of its last three readings, so one stray
// reading can't jump it about, and a reading that hears fewer than half the strings is passed over.
const BLOCK=512,GATE=.002,SETTLE=.25,FIRST=.5,WIN=.75,EVERY=.25,MAX_RING=12;
function listener(sr){
 const len=Math.round(sr*1.5),buf=new Float32Array(len),back=Math.round(.15*sr/BLOCK);
 let write=0,total=0,e=0,cnt=0,levels=[],onset=-1,peak=0,next=0,seen=null;
 function block(rms,notes){
  levels.push(rms);if(levels.length>back+4)levels.shift();
  const before=levels.length>back?levels[0]:0,since=onset<0?Infinity:(total-onset)/sr;
  if(rms>GATE&&since>.3&&(rms>before*2||(onset>=0&&rms>peak*1.5))){onset=total;peak=rms;next=total+Math.round(sr*(SETTLE+FIRST));seen=null;return {strum:true}}
  if(onset<0)return null;
  peak=Math.max(peak,rms);
  if(since>MAX_RING||rms<peak*.02){onset=-1;return {done:true}}
  if(total<next)return null;next=total+Math.round(sr*EVERY);
  const n=Math.min(Math.round(sr*WIN),total-onset-Math.round(sr*SETTLE))&~1,x=new Float32Array(n);
  for(let i=0;i<n;i++)x[i]=buf[(write-n+i+len)%len];
  const r=read(x,sr,notes);
  if(r.filter(o=>o.heard).length*2<r.length)return null;
  if(!seen||seen.length!==r.length)seen=r.map(()=>[]);
  return {strings:r.map((o,i)=>{
   const h=seen[i];h.push(o.heard?o.cents:null);if(h.length>3)h.shift();
   const got=h.filter(c=>c!=null).sort((a,b)=>a-b);
   return {target:o.target,heard:got.length*2>h.length||(h.length===1&&o.heard),cents:got.length?got[got.length>>1]:o.cents};
  })};
 }
 return {
  // Returns the newest event in these samples: a strum starting, a reading, or the strings rung out.
  feed(a,notes){
   let out=null;
   for(let i=0;i<a.length;i++){const v=a[i];buf[write]=v;write=(write+1)%len;total++;e+=v*v;if(++cnt===BLOCK){const r=block(Math.sqrt(e/BLOCK),notes);e=cnt=0;if(r&&!(out&&out.strings&&!r.strings))out=r}}
   return out;
  }
 };
}
root.PolyStrum={read,clashes,listener};
if(typeof module!=='undefined')module.exports=root.PolyStrum;
})(typeof self!=='undefined'?self:this);
