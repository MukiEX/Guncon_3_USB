// Pull the homography helpers out of web/index.html and check them against a known mapping
import fs from 'fs';
const html = fs.readFileSync(new URL('../web/index.html', import.meta.url), 'utf8');
const js = html.slice(html.indexOf('// ---------- homography'), html.indexOf('// ---------- fullscreen overlay'));
const { homography, ap } = new Function(js + '; return {homography, ap};')();

let worst = 0;
for (let trial = 0; trial < 500; trial++) {
  // A plausible raw->screen projective map: raw spans roughly +-20000, slightly skewed / keystoned
  const sx = 1 / (20000 + Math.random() * 15000), sy = -1 / (15000 + Math.random() * 10000);
  const Ht = [[sx, (Math.random() - .5) * 2e-6, .5 + (Math.random() - .5) * .2],
              [(Math.random() - .5) * 2e-6, sy, .5 + (Math.random() - .5) * .2],
              [(Math.random() - .5) * 4e-6, (Math.random() - .5) * 4e-6, 1]];
  const Hinv = (() => { // invert numerically via homography() on exact points
    const pts = [[0.1,0.1],[0.9,0.1],[0.9,0.9],[0.1,0.9],[0.5,0.5]];
    return pts; })();
  // generate raw points by inverting Ht at the screen targets: solve with fixed-point iteration via 3x3 inverse
  const inv = m => { const [a,b,c]=m[0],[d,e,f]=m[1],[g,h,i]=m[2]; const A=e*i-f*h,B=-(d*i-f*g),C=d*h-e*g,det=a*A+b*B+c*C;
    return [[A/det,-(b*i-c*h)/det,(b*f-c*e)/det],[B/det,(a*i-c*g)/det,-(a*f-c*d)/det],[C/det,-(a*h-b*g)/det,(a*e-b*d)/det]]; };
  const Hi = inv(Ht);
  const n = trial % 2 ? 9 : 5;
  const scr = n === 9 ? [0.1,0.5,0.9].flatMap(y => [0.1,0.5,0.9].map(x => [x,y])) : Hinv;
  const raw = scr.map(p => ap(Hi, p).map(v => Math.round(v + (Math.random() - .5) * 20))); // integer raw + noise
  const H = homography(raw, scr);
  if (!H) throw new Error('no solution');
  // compare across the screen
  for (let k = 0; k < 50; k++) {
    const p = [Math.random(), Math.random()];
    const r = ap(Hi, p);
    const q = ap(H, r);
    // float32 firmware evaluation
    const f = Math.fround, h = H.flat().slice(0, 8).map(v => f(+v.toPrecision(9)));
    const w = f(f(f(h[6]*r[0]) + f(h[7]*r[1])) + 1);
    const u = f(f(f(f(h[0]*r[0]) + f(h[1]*r[1])) + h[2]) / w);
    worst = Math.max(worst, Math.hypot(q[0] - p[0], q[1] - p[1]), Math.abs(u - q[0]));
  }
}
console.log('worst normalized error (incl. noise):', worst.toFixed(5), worst < 0.01 ? 'OK' : 'FAIL');
process.exit(worst < 0.01 ? 0 : 1);
