// SPDX-License-Identifier: GPL-3.0-only
const {test} = require('node:test');
const assert = require('node:assert/strict');
const G = require('../src/geometry.js');
const H = require('../src/state.js');
const near = (a,b,epsilon=1e-8) => {assert.equal(a.length,b.length);a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<epsilon,`${a} != ${b}`));};
const sample = anchor => ({id:1,anchor,offset:[.1679,.2,.0862],position:[.25,.3,-.45],radius:.204,enabled:true});
test('reference frames are orthonormal; local/world inverse works on every anchor',()=>{
 for(const anchor of Object.keys(G.frames)) {
  const slot=sample(anchor);near(G.local(anchor,G.world(slot)),slot.offset);
  const r=G.frames[anchor].rotation;
  for(let i=0;i<3;i++)for(let j=0;j<3;j++)assert.ok(Math.abs([0,1,2].reduce((s,k)=>s+r[k*3+i]*r[k*3+j],0)-(i===j?1:0))<1e-10);
 }
});
test('all bone pairs and central anchors mirror in body space, including nonzero three-axis offsets',()=>{
 for(const anchor of Object.keys(G.frames)) {
  const source=sample(anchor),copy=G.mirror(source,8);
  near(G.world(copy),G.reflect(G.world(source)));
  near(copy.position,[.25,-.3,-.45]); assert.equal(copy.radius,.204);assert.equal(copy.enabled,true);assert.equal(copy.id,8);
  assert.deepEqual(G.mirror(copy,1),source);
 }
});
test('upper-arm offset along bone runs outward and downward on both sides',()=>{
 const right={...sample('right_upper_arm'),offset:[.1,0,0]},left=G.mirror(right,2);
 const rp=G.world(right),lp=G.world(left);
 assert.ok(rp[0]>G.frames.right_upper_arm.origin[0]);assert.ok(lp[0]<G.frames.left_upper_arm.origin[0]);
 assert.ok(rp[2]<G.frames.right_upper_arm.origin[2]);assert.ok(lp[2]<G.frames.left_upper_arm.origin[2]);
 near(rp.map((x,i)=>x-G.frames.right_upper_arm.origin[i]),[.087983,-.003999,-.04736],.0001);
});
test('front and side dragging changes only the two displayed dimensions',()=>{
 for(const anchor of Object.keys(G.frames))for(const view of ['front','side']) {
  const row=sample(anchor),before=G.world(row),moved={...row,offset:G.move(row,view,.15,-.08)},after=G.world(moved);
  const expected=before.slice();expected[view==='front'?0:1]-=.15;expected[2]-=.08;near(after,expected);
  const a=G.project(before,view),b=G.project(after,view);near(b,[a[0]+.15*G.SCALE,a[1]+.08*G.SCALE]);
 }
});
test('automatic anchors mirror fallback coordinates without inventing a bone binding',()=>{
 const row=sample('auto'),copy=G.mirror(row,2);
 assert.equal(copy.anchor,'auto');assert.equal(G.world(copy),null);assert.deepEqual(copy.offset,[0,0,0]);assert.deepEqual(copy.position,[.25,-.3,-.45]);
});
test('mirror preserves source, replaces a different destination and serializes all eight slots',()=>{
 const doc=H.parse(H.demo()),model=H.clone(doc.model);model.slots[0]=sample('right_upper_arm');
 const before=H.clone(model.slots[0]);model.slots[1]=G.mirror(model.slots[0],2);
 const reloaded=H.parse(H.serialize(doc,model)).model;
 assert.deepEqual(model.slots[0],before);assert.equal(reloaded.slots[1].anchor,'left_upper_arm');near(reloaded.slots[1].offset,[.1679,.2,-.0862]);
 assert.deepEqual(reloaded.slots.slice(2),doc.model.slots.slice(2));
 assert.throws(()=>G.mirror(before,1));assert.throws(()=>G.mirror(before,9));assert.throws(()=>G.mirror(before,2.5));
});
