// SPDX-License-Identifier: GPL-3.0-only
const {test}=require('node:test');
const assert=require('node:assert/strict');
const H=require('../src/state.js');
const G=require('../src/geometry.js');
const P=require('../src/presets.js');
const close=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-10,`${a} != ${b}`));
test('seven distinct layouts include forward-facing limb and torso choices',()=>{
 assert.deepEqual(P.list().map(p=>[p.id,p.count]),[['front_reach',6],['upper_body',6],['torso_front',4],['arms_only',4],['large_targets',4],['thigh_kit',4],['full_kit',8]]);
 assert.equal(new Set(P.list().map(p=>JSON.stringify(P.slots(p.id)))).size,7);
 const copy=P.list();copy[0].pairs[0].radius=42;assert.equal(P.list()[0].pairs[0].radius,8);
});
test('all variants stay in front and keep grab spheres separated',()=>{
 for(const p of P.list())for(const side of ['right','left'])for(const forward of [0,5,10]) {
  const slots=P.slots(p.id,side,forward);
  assert.equal(slots.length,p.count);assert.ok(P.metrics(slots).gap>.06);
  slots.forEach((s,i)=>{
   assert.equal(s.id,i+1);assert.equal(s.enabled,true);assert.match(s.anchor,/^((right|left)_(upper_arm|forearm|thigh)|chest|pelvis)$/);
   if(!['upper_body','torso_front'].includes(p.id))assert.match(s.anchor,/^(right|left)_(upper_arm|forearm|thigh)$/);
   assert.ok(s.radius>=.05 && s.radius<=.45);
   // Conservative reference front plane. This does not claim live collision validation.
   const world=G.world(s);assert.ok(world[1]+s.radius<-.10);
   assert.ok(world[2]>=.67); // no shin/ankle targets
  });
 }
});
test('slot order swaps sides and additional forward reach preserves height and lateral position',()=>{
 for(const p of P.list()) {
  const right=P.slots(p.id),left=P.slots(p.id,'left'),forward=P.slots(p.id,'right',10);
  right.forEach((slot,i)=>{
   const a=G.world(slot),b=G.world(left[i]);close(b,G.reflect(a));
   close(G.world(forward[i]),[a[0],a[1]-.1,a[2]]);
   if(i%2===0){assert.ok(a[0]>0);assert.ok(b[0]<0);close(G.world(right[i+1]),G.reflect(a));}
  });
 }
});
test('apply is atomic data transformation: preserves input, fallbacks, visibility and unused placements',()=>{
 const source=H.parse(H.demo()).model;source.show=false;const snapshot=H.clone(source);
 for(const p of P.list()) {
  const next=P.apply(source,p.id,'left',5);H.validate(next);
  assert.deepEqual(source,snapshot);assert.equal(next.show,false);
  assert.equal(next.slots.filter(s=>s.enabled).length,p.count);
  next.slots.forEach((s,i)=>{
   assert.deepEqual(s.position,source.slots[i].position);
   if(i>=p.count)assert.deepEqual(s,{...source.slots[i],enabled:false});
  });
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
  assert.match(read.text,/\[Keep\]\r\nsetting=original calibration ☢\r\n/);
  assert.deepEqual(read.model,model);
 }
});
test('invalid preset options are rejected without changing the source state',()=>{
 const model=H.parse(H.demo()).model,snapshot=H.clone(model);
 for(const args of [['unknown'],['front_reach','up'],['arms_only','right',-5],['full_kit','left',Infinity]])assert.throws(()=>P.apply(model,...args));
 assert.deepEqual(model,snapshot);
});

test('torso options explicitly retain chest and pelvis bindings with targets in front',()=>{
 const torso=P.slots('torso_front');assert.deepEqual(torso.map(s=>s.anchor),['chest','chest','pelvis','pelvis']);
 for(const s of torso){const p=G.world(s);assert.ok(p[1]<-.25);assert.ok(s.radius>=.08);}
 assert.deepEqual(P.slots('upper_body').map(s=>s.anchor),['right_upper_arm','left_upper_arm','chest','chest','pelvis','pelvis']);
});
