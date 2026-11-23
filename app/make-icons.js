// Draws the PolyPitch icon (the same picture as web/icon-512.png) at every size an Android app needs,
// using headless Edge, and writes it into each res/ folder given.
//   node make-icons.js capacitor/android/app/src/main/res twa/app/src/main/res
// For each density it writes:
//   mipmap-*/ic_launcher.png             the whole icon, for launchers without adaptive icons
//   mipmap-*/ic_launcher_round.png       the same, cut to a circle
//   mipmap-*/ic_launcher_background.png  adaptive icon back layer: the dark ground and three note lines
//   mipmap-*/ic_launcher_foreground.png  adaptive icon front layer: the trace, shrunk to sit inside the safe circle
//   mipmap-*/ic_launcher_monochrome.png  the trace in one colour, for Android 13+ themed icons
//   drawable-*/splash.png                the whole icon, for the launch screen
const http=require('http'),fs=require('fs'),path=require('path'),{spawn}=require('child_process'),os=require('os');
const OUTS=process.argv.slice(2).map(p=>path.resolve(p));
if(!OUTS.length){console.log('usage: node make-icons.js <res dir>...');process.exit(1)}
const EDGE='C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',PORT=8152;
const DENS={mdpi:1,hdpi:1.5,xhdpi:2,xxhdpi:3,xxxhdpi:4};
const page=`<!doctype html><body><script>
// Draws onto a square canvas of S pixels. Layers: 'bg' ground, 'lines' note lines, 'trace' the sung line.
// s shrinks the picture about the centre (1 = as in the web icon). mono draws the trace in plain white.
function draw(S,{bg=true,lines=true,trace=true,s=1,round=false,mono=false}){
 const c=document.createElement('canvas');c.width=c.height=S;const g=c.getContext('2d'),k=S/512;g.scale(k,k);
 if(round){g.beginPath();g.arc(256,256,256,0,2*Math.PI);g.clip()}
 if(bg){g.fillStyle='#070b0e';g.fillRect(0,0,512,512)}
 g.translate(256,256);g.scale(s,s);g.translate(-256,-256);
 if(lines){g.strokeStyle='#2a3a44';g.lineWidth=4;for(const y of [154,256,358]){g.beginPath();g.moveTo(-512,y);g.lineTo(1024,y);g.stroke()}}
 if(!trace)return c.toDataURL('image/png');
 const X0=72,X1=262,X2=440,Y0=346,Y=256,pts=[];
 for(let x=X0;x<=X2;x+=1){let y;
  if(x<=X1){const p=(x-X0)/(X1-X0);y=Y+(Y0-Y)*(1-p)**2.2}
  else{const p=(x-X1)/(X2-X1),amp=13*Math.min(1,p*1.6);y=Y-amp*Math.sin(2*Math.PI*(x-X1)/88)}
  pts.push([x,y])}
 const f=x=>(x-X0)/(X2-X0);
 const grad=(a,blue,green)=>{const gr=g.createLinearGradient(X0,0,X2,0);
  gr.addColorStop(0,'rgba('+blue+',0)');gr.addColorStop(f(X0+34),'rgba('+blue+','+a+')');
  gr.addColorStop(f(X1-40),'rgba('+blue+','+a+')');gr.addColorStop(f(X1+14),'rgba('+green+','+a+')');gr.addColorStop(1,'rgba('+green+','+a+')');return gr};
 const stroke=(w,style)=>{g.save();g.lineCap='round';g.lineJoin='round';g.lineWidth=w;g.strokeStyle=style;
  g.beginPath();pts.forEach(([x,y],i)=>i?g.lineTo(x,y):g.moveTo(x,y));g.stroke();g.restore()};
 if(mono){stroke(16,grad(1,'255,255,255','255,255,255'));return c.toDataURL('image/png')}
 g.save();g.filter='blur('+(10*k*s)+'px)';stroke(24,grad(.6,'150,190,255','76,235,125'));g.restore();
 stroke(10,grad(1,'150,190,255','76,235,125'));
 stroke(3.5,grad(.75,'235,245,255','225,255,232'));
 return c.toDataURL('image/png');
}
// Adaptive icons are 108dp with only the middle 66dp circle sure to show, so the trace is shrunk to fit it.
const FG=0.66,out={};
for(const [d,m] of Object.entries(${JSON.stringify(DENS)})){
 out['mipmap-'+d+'/ic_launcher.png']=draw(48*m,{});
 out['mipmap-'+d+'/ic_launcher_round.png']=draw(48*m,{round:true});
 out['mipmap-'+d+'/ic_launcher_background.png']=draw(108*m,{trace:false,s:FG});
 out['mipmap-'+d+'/ic_launcher_foreground.png']=draw(108*m,{bg:false,lines:false,s:FG});
 out['mipmap-'+d+'/ic_launcher_monochrome.png']=draw(108*m,{bg:false,lines:false,s:FG,mono:true});
 out['drawable-'+d+'/splash.png']=draw(160*m,{bg:false,lines:false,s:FG*1.25});
}
fetch('/save',{method:'POST',body:JSON.stringify(out)});
<\/script>`;
let edge;
http.createServer((q,s)=>{
 if(q.url==='/'){s.writeHead(200,{'Content-Type':'text/html'});return s.end(page)}
 if(q.url==='/save'){const c=[];q.on('data',d=>c.push(d));q.on('end',()=>{const o=JSON.parse(Buffer.concat(c));
  for(const res of OUTS)for(const [name,url] of Object.entries(o)){const f=path.join(res,name);fs.mkdirSync(path.dirname(f),{recursive:true});fs.writeFileSync(f,Buffer.from(url.split(',')[1],'base64'))}
  console.log('wrote',Object.keys(o).length,'images into',OUTS.join(', '));s.end('ok');edge.kill();process.exit(0)});return}
 s.writeHead(404);s.end();
}).listen(PORT,'127.0.0.1',()=>{
 edge=spawn(EDGE,['--headless=new','--user-data-dir='+path.join(os.tmpdir(),'polypitch-icons-edge'),`http://127.0.0.1:${PORT}/`]);
 setTimeout(()=>{console.log('timeout');edge.kill();process.exit(1)},30000);
});
