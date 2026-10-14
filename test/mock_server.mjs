// Mock of the adapter's HTTP API with simulated guns, for testing web/index.html in a browser.
// POST /mock/aim gun=&u=&v=&shoot=1 moves a simulated gun to a screen position (and fires).
import http from 'http';
import fs from 'fs';
const page = fs.readFileSync(new URL('../web/index.html', import.meta.url));
const inv = m => { const [a,b,c]=m[0],[d,e,f]=m[1],[g,h,i]=m[2]; const A=e*i-f*h,B=-(d*i-f*g),C=d*h-e*g,det=a*A+b*B+c*C;
  return [[A/det,-(b*i-c*h)/det,(b*f-c*e)/det],[B/det,(a*i-c*g)/det,-(a*f-c*d)/det],[C/det,-(a*h-b*g)/det,(a*e-b*d)/det]]; };
const ap = (T,p) => { const w=T[2][0]*p[0]+T[2][1]*p[1]+T[2][2]; return [(T[0][0]*p[0]+T[0][1]*p[1]+T[0][2])/w,(T[1][0]*p[0]+T[1][1]*p[1]+T[1][2])/w]; };
// true raw->screen map for each simulated gun (keystoned, y inverted)
const TRUE = [0,1].map(k => [[1/26000, 3e-7, .5 + k*.02],[2e-7, -1/19000, .48],[1.5e-6*(k?1:-1), 8e-7, 1]]);
const S = { fw:'0.1.0-mock', maxGuns:4, pads:4, aspectDefault:0, aspectCycle:3, offscreen:0, margin:8, smoothing:2, stickA:1, stickB:2,
  deadzone:40, hotkeys:1, monitorAspect:16/9, custom:[.125,0,.875,1], map:[1,2,3,4,5,10,9,12,13],
  calib:[0,1,2,3].map(()=>({valid:0,points:0,h:[0,0,0,0,0,0,0,0]})) };
let aspect = 0;
const guns = [0,1,2,3].map(i => ({ c: i < 2 ? 1 : 0, port: i+1, x:0, y:0, z:300, b:0, ax:128, ay:128, bx:128, by:128, ind:3, n:0, bad:0, shot:0, sx:0, sy:0 }));
function setAim(i,u,v){ const r = ap(inv(TRUE[i]), [u,v]); guns[i].x = Math.round(r[0]); guns[i].y = Math.round(r[1]); }
setAim(0,.3,.4); setAim(1,.7,.6);
function rect(a){ const m=S.monitorAspect, ar=[0,4/3,3/4,5/4,0][a]; if(a===4) return S.custom; if(!ar) return [0,0,1,1];
  if (ar<m){const w=ar/m; return [(1-w)/2,0,(1+w)/2,1]} const h=m/ar; return [0,(1-h)/2,1,(1+h)/2]; }
function out(i){ const g=guns[i], c=S.calib[i]; let u=(g.x+32768)/65535, v=(g.y+32768)/65535;
  if (c.valid){ const h=c.h; const w=h[6]*g.x+h[7]*g.y+1; u=(h[0]*g.x+h[1]*g.y+h[2])/w; v=(h[3]*g.x+h[4]*g.y+h[5])/w; }
  const r=rect(aspect); let gx=(u-r[0])/(r[2]-r[0]), gy=(v-r[1])/(r[3]-r[1]); const off=gx<-.08||gx>1.08||gy<-.08||gy>1.08;
  gx=Math.min(1,Math.max(0,gx)); gy=Math.min(1,Math.max(0,gy));
  return {cal:c.valid,u,v,gx,gy,off:+off,lx:Math.round(gx*65535-32768),ly:Math.round((1-gy)*65535-32768),xb:0}; }
const body = req => new Promise(r => { let d=''; req.on('data', c => d += c); req.on('end', () => r(new URLSearchParams(d))); });
const json = (res,o) => { res.writeHead(200,{'Content-Type':'application/json'}); res.end(JSON.stringify(o)); };
let t = 0;
setInterval(() => { t++; guns.forEach(g => { if (g.c) g.n++; }); guns[1].ax = 128 + Math.round(60*Math.sin(t/20)); }, 4);
http.createServer(async (req,res) => {
  const url = req.url.split('?')[0];
  if (url === '/' ) { res.writeHead(200,{'Content-Type':'text/html'}); return res.end(page); }
  if (url === '/api/state') return json(res,{aspect,rect:rect(aspect),ms:t*4,guns:guns.map((g,i)=>({...g,...out(i)})),usb:[{a:1,vid:0x05e3,pid:0x0608,hub:0,port:0,spd:0},{a:2,vid:0x0b9a,pid:0x0800,hub:1,port:1,spd:0}],usbEvents:2});
  if (url === '/api/settings' && req.method==='GET') return json(res,S);
  const p = await body(req);
  if (url === '/api/settings') { for (const [k,v] of p) { if (k.startsWith('map')) S.map[+k.slice(3)]=+v; else if (k.startsWith('custom')) S.custom[+k.slice(6)]=+v; else S[k]=+v; } return json(res,{ok:true}); }
  if (url === '/api/calib') { const g=+p.get('gun'); S.calib[g]={valid:1,points:+p.get('points'),h:[0,1,2,3,4,5,6,7].map(k=>+p.get('h'+k))}; S.monitorAspect=+p.get('monitorAspect'); return json(res,{ok:true}); }
  if (url === '/api/calib/clear') { S.calib[+p.get('gun')].valid=0; return json(res,{ok:true}); }
  if (url === '/api/aspect') { aspect=+p.get('a'); return json(res,{ok:true}); }
  if (url === '/mock/aim') { const i=+p.get('gun'); setAim(i,+p.get('u'),+p.get('v'));
    if (p.get('shoot')) { const g=guns[i]; g.sx=g.x; g.sy=g.y; g.shot++; g.b|=1; setTimeout(()=>g.b&=~1,60); }
    if (p.get('btn')) { const k=+p.get('btn'); guns[i].b|=1<<k; setTimeout(()=>guns[i].b&=~(1<<k),120); }
    return json(res,{ok:true}); }
  if (url === '/mock/true') return json(res,TRUE);
  json(res,{ok:true});
}).listen(8077, () => console.log('mock on 8077'));
