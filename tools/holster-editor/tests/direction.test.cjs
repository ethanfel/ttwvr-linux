// SPDX-License-Identifier: GPL-3.0-only
const {test}=require('node:test');
const assert=require('node:assert/strict');
const H=require('../src/state.js');
const G=require('../src/geometry.js');
const P=require('../src/presets.js');
const fixture=require('./direction-fixture.json');
const near=(a,b,eps=1e-6)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<eps,`${a} != ${b}`));
const legacy=()=>{
 const model=H.parse(H.demo()).model;
 for(const [i,anchor,offset] of [
  [0,'pelvis',[.031845,-.2493891,-.095]], [1,'pelvis',[.031845,-.2493891,.095]],
  [2,'pelvis',[.031845,-.1343891,-.25]], [3,'pelvis',[.031845,-.1343891,.25]],
  [6,'right_thigh',[.1586500525404712,-.19776947111127474,.09958688562839174]],
  [7,'left_thigh',[.1586500525404712,-.19776947111127474,-.09958688562839174]]
 ])Object.assign(model.slots[i],{anchor,offset});
 model.slots[4].anchor='right_forearm';model.slots[5].anchor='left_forearm';
 return model;
};
test('independent supplied-skeleton toes establish positive native Y as forward',()=>{
 for(const side of ['right','left']) {
  const foot=fixture.reference_nodes[`${side}_foot`],toe=fixture.reference_nodes[`${side}_toe`];
  assert.ok(toe[1]-foot[1]>11); // 11.63 game units forward, not a UI convention.
  for(const part of ['foot','toe']) {
   const p=fixture.reference_nodes[`${side}_${part}`];
   near(G.joints[`${side}_${part}`],[p[0]/70,-p[1]/70,p[2]/70]);
  }
  assert.ok(G.project(G.joints[`${side}_toe`],'side')[0]>G.project(G.joints[`${side}_foot`],'side')[0]);
 }
 assert.ok(G.world({anchor:'pelvis',offset:[0,.2,0]})[1]<G.frames.pelvis.origin[1]);
});
test('recorded gameplay pelvis positive Y agrees with toe heading, and belt presets extend forward',()=>{
 const r=fixture.runtime.pelvis_rotation;
 const toe=[0,1].map(i=>fixture.runtime.foot_to_toe.left[i]+fixture.runtime.foot_to_toe.right[i]);
 const length=Math.hypot(...toe),heading=toe.map(v=>v/length);
 const along=offset=>[0,1].reduce((sum,i)=>sum+heading[i]*offset.reduce((s,v,k)=>s+r[i*3+k]*v,0),0);
 assert.ok(along([0,1,0])>.99);
 assert.ok(along([.031845,-.2493891,-.095])<-.24); // Reproduces the reported old front pouch behind the body.
 for(const s of P.slots('gun_belt'))assert.ok(along(s.offset)>.06,`slot ${s.id} is behind the recorded pelvis`);
});
test('body repair restores intended old belt/thigh targets without changing arms, other fields or input',()=>{
 const m=legacy(),before=H.clone(m),fixed={...m,slots:m.slots.map(G.repairBodyPlacement)};
 assert.deepEqual(m,before);
 const targets=[[.095,-.24,1],[-.095,-.24,1],[.25,-.125,1],[-.25,-.125,1],null,null,[.2,-.18,.8],[-.2,-.18,.8]];
 fixed.slots.forEach((s,i)=>{
  if(targets[i])near(G.world(s),targets[i]);else assert.deepEqual(s,m.slots[i]);
  assert.deepEqual({...s,offset:m.slots[i].offset},m.slots[i]);
  assert.equal(G.repairBodyPlacement(s),s); // Idempotent: never flip a repaired slot back.
 });
});
test('repair handles tilted chest/shin frames, retains front/automatic/arm slots, and exports losslessly',()=>{
 for(const anchor of Object.keys(G.frames)) {
  const s={...legacy().slots[0],anchor,offset:G.local(anchor,[.15,.23,1.2]),enabled:false};
  const fixed=G.repairBodyPlacement(s);
  if(anchor.includes('arm') || anchor==='head')assert.equal(fixed,s);else near(G.world(fixed),[.15,-.23,1.2]);
  const front={...s,offset:G.local(anchor,[.15,-.23,1.2])};assert.equal(G.repairBodyPlacement(front),front);
 }
 const auto={...legacy().slots[0],anchor:'auto'};assert.equal(G.repairBodyPlacement(auto),auto);
 for(const enc of ['utf-8','utf-16le','utf-16be']) {
  const source=H.parse(H.encode(H.demo()+'[Unrelated]\r\nvalue=unchanged ☢\r\n',enc,new Uint8Array(enc==='utf-8'?[]:enc==='utf-16le'?[255,254]:[254,255])));
  const doc=H.parse(H.serialize(source,legacy()));
  const fixed={...doc.model,slots:doc.model.slots.map(G.repairBodyPlacement)};
  const out=H.parse(H.serialize(doc,fixed));assert.deepEqual(out.model,fixed);assert.equal(out.encoding,doc.encoding);
  const oldLines=doc.text.split(/\r?\n/),newLines=out.text.split(/\r?\n/);assert.equal(oldLines.length,newLines.length);
  oldLines.forEach((line,i)=>{if(line!==newLines[i])assert.match(line,/^\d+=bone (1|2|3|4|7|8) /);});
 }
});
