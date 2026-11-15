// SPDX-License-Identifier: GPL-3.0-only
const {test}=require('node:test');
const assert=require('node:assert/strict');
const H=require('../src/state.js');
const G=require('../src/geometry.js');
const P=require('../src/presets.js');
const close=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-10,`${a} != ${b}`));
const variants=function*(){for(const p of P.list())for(const side of ['right','left'])for(const forward of [0,5,10])for(const pipBoy of ['right','left'])yield {p,side,forward,pipBoy,slots:P.slots(p.id,side,forward,pipBoy)};};
test('seven gear layouts include a belt/vest default and one optional forearm utility',()=>{
 assert.deepEqual(P.list().map(p=>[p.id,p.count]),[['wasteland_rig',8],['gun_belt',4],['chest_rig',6],['travel_vest',6],['shoulder_rig',6],['field_rig',8],['utility_rig',8]]);
 assert.equal(new Set(P.list().map(p=>JSON.stringify(P.slots(p.id)))).size,7);
 const copy=P.list();copy[0].rows[0].radius=42;assert.equal(P.list()[0].rows[0].radius,6);
});
test('belt targets form a single-height arc with front pouches and side hip holsters',()=>{
 const s=P.slots('gun_belt'),front=G.world(s[0]),hip=G.world(s[2]);
 assert.ok(s.every(x=>x.anchor==='pelvis'));assert.ok(s.every(x=>Math.abs(G.world(x)[2]-1)<1e-10));
 assert.ok(front[0]<hip[0]);assert.ok(front[1]<hip[1]);
 close(front,[.095,-.19,1]);close(hip,[.25,-.075,1]);
 const all=P.slots('wasteland_rig');assert.deepEqual(all.map(s=>s.anchor),['pelvis','pelvis','pelvis','pelvis','chest','chest','chest','chest']);
 assert.ok(G.world(all[4])[2]>G.world(all[6])[2]);assert.ok(G.world(all[6])[2]>hip[2]);
});
test('every variant retains separate spheres, useful height and no duplicate arm targets',()=>{
 for(const {p,slots} of variants()) {
  assert.equal(slots.length,p.count);assert.ok(P.metrics(slots).gap>.0499);
  for(const anchor of ['left_upper_arm','right_upper_arm','left_forearm','right_forearm'])assert.ok(slots.filter(s=>s.anchor===anchor).length<=1);
  slots.forEach((s,i)=>{
   assert.equal(s.id,i+1);assert.equal(s.enabled,true);assert.match(s.anchor,/^((right|left)_(forearm|thigh)|chest|pelvis)$/);
   assert.ok(s.radius>=.05 && s.radius<=.45);assert.ok(G.world(s)[1]<0);assert.ok(G.world(s)[2]>=.7999);
  });
 }
});
// A conservative reference torso cylinder and capsule limbs, not a live avatar
// collision model. Unlike a single forward plane, this allows real side holsters.
function torsoDistance(p) {
 const x=Math.abs(p[0]),y=Math.abs(p[1]-.01),a=.18,b=.115;
 let lateral=0;
 if((x/a)**2+(y/b)**2>1) {
  const squared=t=>(x-a*Math.cos(t))**2+(y-b*Math.sin(t))**2;
  let lo=0,hi=Math.PI/2;
  for(let i=0;i<90;i++){const m1=lo+(hi-lo)/3,m2=hi-(hi-lo)/3;if(squared(m1)<squared(m2))hi=m2;else lo=m1;}
  lateral=Math.sqrt(squared((lo+hi)/2));
 }
 return Math.hypot(lateral,Math.max(.90-p[2],p[2]-1.48,0));
}
function segmentDistance(p,a,b) {
 const ab=b.map((v,i)=>v-a[i]),ap=p.map((v,i)=>v-a[i]);
 const dot=(u,v)=>u.reduce((s,x,i)=>s+x*v[i],0);
 const t=Math.max(0,Math.min(1,dot(ap,ab)/dot(ab,ab)));
 return Math.hypot(...p.map((v,i)=>v-a[i]-t*ab[i]));
}
test('grab spheres stay outside the reference torso and limb volumes, including side belt holsters',()=>{
 for(const {slots} of variants())for(const s of slots) {
  const p=G.world(s);assert.ok(torsoDistance(p)-s.radius>.01,`${s.anchor} intersects reference torso`);
  for(const side of ['right','left'])for(const [a,b,r] of [['upper_arm','forearm',.05],['forearm','hand',.0375],['thigh','shin',.065],['shin','foot',.0475]]) {
   assert.ok(segmentDistance(p,G.joints[`${side}_${a}`],G.joints[`${side}_${b}`])-r-s.radius>.01,`${s.anchor} intersects reference ${side} ${a}`);
  }
 }
});
test('pair order swaps only paired slots; forward spacing preserves height and lateral position',()=>{
 for(const p of P.list()) {
  const right=P.slots(p.id),left=P.slots(p.id,'left'),forward=P.slots(p.id,'right',10);
  for(const row of p.rows)for(const id of row.ids) {
   const a=G.world(right[id-1]),b=G.world(left[id-1]);
   close(b,row.ids.length===2?G.reflect(a):a);
   close(G.world(forward[id-1]),[a[0],a[1]-.1,a[2]]);
  }
 }
});
test('Pip-Boy forearm is reserved independently of pair order and forward spacing',()=>{
 for(const {p,slots,pipBoy} of variants()) {
  assert.ok(!slots.some(s=>s.anchor===`${pipBoy}_forearm`));
  const forearm=slots.filter(s=>s.anchor.endsWith('_forearm'));
  assert.equal(forearm.length,p.id==='utility_rig'?1:0);
  if(forearm.length){assert.equal(forearm[0].id,8);assert.equal(forearm[0].anchor,`${pipBoy==='right'?'left':'right'}_forearm`);}
 }
 assert.equal(P.slots('utility_rig')[7].anchor,'left_forearm');
 assert.equal(P.list('right').find(p=>p.id==='utility_rig').rows.at(-1).label,'Left forearm utility');
});
test('apply preserves input, fallbacks, visibility and unused placements',()=>{
 const source=H.parse(H.demo()).model;source.show=false;const snapshot=H.clone(source);
 for(const p of P.list()) {
  const next=P.apply(source,p.id,'left',5);H.validate(next);
  assert.deepEqual(source,snapshot);assert.equal(next.show,false);assert.equal(next.slots.filter(s=>s.enabled).length,p.count);
  next.slots.forEach((s,i)=>{assert.deepEqual(s.position,source.slots[i].position);if(i>=p.count)assert.deepEqual(s,{...source.slots[i],enabled:false});});
 }
});
test('each profile exports and reloads while retaining unrelated state and original bytes',()=>{
 for(const encoding of ['utf-8','utf-16le','utf-16be'])for(const p of P.list()) {
  const input=H.demo()+'[Keep]\r\nsetting=original calibration ☢\r\n';
  const bom=encoding==='utf-8'?[]:encoding==='utf-16le'?[255,254]:[254,255];
  const bytes=H.encode(input,encoding,new Uint8Array(bom)),d=H.parse(bytes);
  const model=P.apply(d.model,p.id),out=H.serialize(d,model),read=H.parse(out);
  assert.deepEqual(d.bytes,bytes);assert.equal(read.encoding,encoding);
  assert.match(read.text,/\[SavedHeight\]\r\nHead=1.700\r\nHips=0.000/);
  assert.match(read.text,/\[SavedWristBoy\]\r\n1=hud/);
  assert.match(read.text,/\[Keep\]\r\nsetting=original calibration ☢\r\n/);assert.deepEqual(read.model,model);
 }
});
test('invalid preset options are rejected without changing source state',()=>{
 const model=H.parse(H.demo()).model,snapshot=H.clone(model);
 for(const args of [['unknown'],['gun_belt','up'],['utility_rig','right',-5],['field_rig','left',Infinity],['utility_rig','right',0,'up']])assert.throws(()=>P.apply(model,...args));
 assert.deepEqual(model,snapshot);
});
