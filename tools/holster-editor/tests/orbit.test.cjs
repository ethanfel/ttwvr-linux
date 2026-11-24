// SPDX-License-Identifier: GPL-3.0-only
const test=require('node:test');
const assert=require('node:assert/strict');
const G=require('../src/geometry.js'), O=require('../src/orbit.js');
const H=require('../src/state.js'), P=require('../src/presets.js');
const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-8,`${a} != ${b}`));
test('screen-space gizmo motion recovers the world-axis displacement after orbit',()=>{
  for(const yaw of [-2,-.3,.6,1.8,3.1]) for(const pitch of [-.5,.3,.75]) {
    const c=O.camera(yaw,pitch), from=[.15,-.24,1.13], a=O.project(from,c);
    for(const {vector} of O.axes) {
      const delta=O.times(vector,.12), b=O.project(O.add(from,delta),c);
      if(Math.hypot(b[0]-a[0],b[1]-a[1])>.12*G.SCALE*.2)
        near(O.axisDelta(vector,b[0]-a[0],b[1]-a[1],c),delta);
    }
    const b=O.project(O.add(from,O.planeDelta(13,-17,c)),c);
    near(b,[a[0]+13,a[1]-17]);
  }
  near(O.axisDelta([0,-1,0],10,10,O.camera(0,0)),[0,0,0]);
});
test('gizmos translate anchored equipment and automatic fallbacks in their saved frames',()=>{
  const d=H.parse(H.demo()), shift=[.03,-.07,.10];
  for(const s of d.model.slots) near(G.world(O.move(s,false,shift),1.91),O.add(G.world(s,1.91),shift));
  for(const bone of [null,0,1,2,3]) {
    const p=G.reanchorPouch(d.model.pouch,bone), result=O.move(p,true,shift);
    near(G.pouchWorld(result),O.add(G.pouchWorld(p),shift));
    near(result.body,O.add(p.body,[.07,.03,-.10,.10]));
    assert.equal(result.bone,bone);assert.deepEqual(result.rotation,p.rotation);
  }
});
test('every pouch preview matches its exported anchor and fallback with clear space around all hotkeys',()=>{
  const doc=H.parse(H.demo());
  for(const profile of P.list()) for(const side of ['left','right']) for(const forward of [0,5,10]) for(const pip of ['left','right']) {
    const eyeHeight=1.82, options={eyeHeight,pouchSide:side};
    const model=P.apply(doc.model,profile.id,'right',forward,pip,options);
    const preview=P.ammoPouch(profile.id,forward,side,doc.model.pouch,eyeHeight);
    assert.deepEqual(model.pouch,preview);
    assert.deepEqual(P.apply(model,profile.id,'right',forward,pip,options),model);
    const world=G.pouchWorld(preview);
    near(world,[preview.body[1],-preview.body[0],preview.body[3]]);
    assert.ok(world[1]<0 && (side==='left'?world[0]<0:world[0]>0));
    assert.equal(preview.body[2]+preview.body[3],eyeHeight);
    assert.ok(P.metrics(model.slots.filter(s=>s.enabled),preview).gap>.015,profile.id);
    assert.deepEqual(H.parse(H.serialize(doc,model)).model.pouch,preview);
  }
});
test('preset handles absent, pouch-only, and unrecognized pouch sections',()=>{
  const sample=H.demo(), noPouch=sample.split('[SavedPouch]')[0];
  const missing=H.parse(noPouch), created=P.apply(missing.model,'gun_belt');
  assert.deepEqual(H.parse(H.serialize(missing,created)).model.pouch,created.pouch);
  const only=H.parse('[SavedPouch]'+sample.split('[SavedPouch]')[1]);
  const pouchOnly=P.apply(only.model,'field_rig');
  const bytes=H.serialize(only,pouchOnly);
  assert.deepEqual(H.parse(bytes).model,pouchOnly);assert.equal(pouchOnly.slots.length,0);
  const broken=H.parse(noPouch+'[SavedPouch]\r\n1=anchor 7 unknown\r\n');
  assert.ok(broken.pouchIssue);
  const output=H.parse(H.serialize(broken,P.apply(broken.model,'gun_belt','right',0,'right',{includePouch:false})));
  assert.equal(output.pouchIssue,broken.pouchIssue);assert.ok(output.text.includes('1=anchor 7 unknown'));
});

test('seated desk pose joins the limbs at full length and plants the soles',()=>{
  for(const side of ['right','left']){
    for(const [parent,child] of [['upper_arm','forearm'],['thigh','shin'],['shin','foot']]){
      const a=side+'_'+parent, b=side+'_'+child;
      near(O.seatPoint(G.joints[b],a),O.seatPoint(G.joints[b],b));
      const length=(p,q)=>Math.hypot(...p.map((x,i)=>x-q[i]));
      near([length(O.seatPoint(G.joints[a],a),O.seatPoint(G.joints[b],b))],[length(G.joints[a],G.joints[b])]);
    }
    const posed=part=>O.seatPoint(G.joints[side+'_'+part],side+'_'+part);
    const hip=posed('thigh'), knee=posed('shin'), ankle=posed('foot');
    assert.ok(hip[2]>.50 && hip[2]<.62 && hip[1]-knee[1]>.40 && Math.abs(hip[2]-knee[2])<.06);
    assert.ok(Math.abs(ankle[1]-knee[1])<.08 && knee[2]-ankle[2]>.39);
    near([ankle[2]],[G.joints[side+'_foot'][2]]);
    near(O.seatPoint(G.joints[side+'_upper_arm']),posed('upper_arm'));
  }
});
test('seated moves round-trip through every bone, automatic and pouch fallback',()=>{
  const doc=H.parse(H.demo()), shift=[.027,-.053,.039];
  for(const anchor of [...Object.keys(G.frames),'auto']) {
    const slot={...doc.model.slots[0],anchor,offset:[.045,-.07,.03]};
    const start=O.equipmentWorld(slot,false,'seated',1.22);
    const moved=O.move(slot,false,shift,'seated');
    near(O.equipmentWorld(moved,false,'seated',1.22),O.add(start,shift));
    near(O.move(moved,false,O.times(shift,-1),'seated').offset,slot.offset);
    if(anchor==='auto') near(start,O.seatPoint(G.world(slot,G.referenceEyeHeight)));
  }
  for(const bone of [null,0,1,2,3]){
    const pouch=G.reanchorPouch(doc.model.pouch,bone), start=O.equipmentWorld(pouch,true,'seated');
    near(O.equipmentWorld(O.move(pouch,true,shift,'seated'),true,'seated'),O.add(start,shift));
  }
});

test('body intersections depend on 3D depth, joint pose, weight and saved radius',()=>{
  for(const cam of [O.camera(0,0),O.camera(Math.PI/2,0),O.camera(-1.8,.4)]) {
    const hit=(p,r,kg=75,pose='standing')=>O.overlapPath(p,r,cam,kg,pose);
    assert.ok(hit([0,0,1.12],.035));
    for(const y of [-.60,.60]) assert.equal(hit([0,y,1.12],.07),'');
    assert.equal(hit([.215,-.015,1.12],.04,45),'');
    assert.ok(hit([.215,-.015,1.12],.04,180));
    assert.equal(hit([.13,-.25,.53],.035),'');
    assert.ok(hit([.13,-.25,.53],.035,75,'seated'));
    assert.equal(hit([0,-.28,.033],.02,75,'seated'),''); // Chair caster
    assert.equal(hit([0,0,1.12],null),'');
  }
});
