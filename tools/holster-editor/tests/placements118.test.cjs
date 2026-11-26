// SPDX-License-Identifier: GPL-3.0-only
const {test} = require('node:test');
const assert = require('node:assert/strict');
const H = require('../src/state.js'), G = require('../src/geometry.js'), P = require('../src/presets.js');
const spots = ['[SavedSpots]', '1=spot pouch .10 -.20 -.35 .0850 ; pouch',
  '2=bone pouch left_forearm .04 -.02 .09', '3=spot stim .18 .10 -.30 .125',
  '4=head stim .25 .12 -.27', '5=future placement keep me'].join('\r\n');
const fixture = H.demo().replace('bone 1 right_upper_arm 0.1000 0.0000 0.0000', 'head 1 .2 -.1 -.18') + spots;
test('1.1.8 recognizes head and named spots alongside a legacy pouch, retaining original bytes', () => {
  const d = H.parse(fixture);
  assert.equal(d.model.slots[0].anchor,'head');
  assert.deepEqual(d.model.slots[0].offset,[.2,-.1,-.18]);
  assert.equal(d.model.spots.pouch.anchor,'left_forearm');
  assert.equal(d.model.spots.stim.anchor,'head');
  assert.deepEqual(H.serialize(d),new TextEncoder().encode(fixture));
  d.model.spots.pouch.radius=.075;
  assert.equal(H.decode(H.serialize(d)).text,fixture.replace('.0850','0.075'));
});
test('each placement can switch between automatic, bone and head in either record order', () => {
  for (const id of [1,'pouch','stim']) for (const anchor of ['auto','left_thigh','head']) {
    const d=H.parse(fixture), row=id===1 ? d.model.slots[0] : d.model.spots[id];
    row.anchor=anchor; row.offset=[.011,-.098,.247]; row.radius=.13;
    const output=H.parse(H.serialize(d));
    const refs=id===1 ? output.refs : output.refs.spots;
    assert.equal(!!refs.bones[id],anchor==='left_thigh');
    assert.equal(!!refs.heads[id],anchor==='head');
    const saved=id===1?output.model.slots[0]:output.model.spots[id];
    assert.deepEqual(saved.offset,anchor==='auto'?[0,0,0]:row.offset);
    assert.equal(saved.anchor,anchor); assert.equal(saved.radius,.13);
    assert.ok(output.text.includes('future placement keep me'));
    assert.equal(output.model.pouch.radius,.1125);
  }
  for (const lines of [['2=head pouch 0 0 .2','3=bone pouch pelvis 0 0 .3'],
                        ['2=bone pouch pelvis 0 0 .3','3=head pouch 0 0 .2']]) {
    const d=H.parse(['[SavedSpots]','1=spot pouch .1 .2 .3 .1',...lines].join('\n'));
    assert.equal(d.model.spots.pouch.anchor,'pelvis');
    d.model.spots.pouch.radius=.16;
    assert.ok(H.decode(H.serialize(d)).text.endsWith(lines.join('\n')));
    d.model.spots.pouch.anchor='head';
    const out=H.parse(H.serialize(d));
    assert.equal(out.sections.savedspots.records.length,2);
    assert.equal(out.model.spots.pouch.anchor,'head');
  }
});
test('a named section can be extended or created on export without merging sections or changing calibration', () => {
  const d=H.parse('[SavedPouch]\n1=.1 .2 .4 1.2'); d.model.pouch.radius=.125;
  d.model.spots={pouch:G.pouchSpot(d.model.pouch,1.6)};
  const out=H.parse(H.serialize(d));
  assert.equal(out.sections.savedpouch.records.length,2);
  assert.equal(out.sections.savedspots.records.length,1);
  assert.deepEqual(out.model.spots,d.model.spots);
  const onlyStim=H.parse('[SavedSpots]\n1=spot stim .18 .15 -.3 .08\n2=head stim 0 0 0');
  assert.deepEqual(onlyStim.model.slots,[]);
  const equipped=P.apply(onlyStim.model,'gun_belt');
  assert.equal(equipped.pouch,null);
  assert.deepEqual(equipped.spots.stim,onlyStim.original.spots.stim);
  assert.equal(equipped.spots.pouch.anchor,'pelvis');
  assert.deepEqual(H.parse(H.serialize(onlyStim,equipped)).model,equipped);
});
test('head space uses forward, right, up from the neck and mirrors lateral distance', () => {
  const row=H.parse(fixture).model.slots[0], centre=G.world(row);
  const expected=[-.1,G.joints.neck[1]-.2,G.joints.neck[2]-.18];
  expected.forEach((v,i)=>assert.ok(Math.abs(v-centre[i])<1e-8));
  assert.deepEqual(G.mirror(row,2).offset,[.2,.1,-.18]);
});
test('named spots reject bad known records and retain unknown spot names', () => {
  for(const bad of ['head stim .3 .4','bone stim nose .1 .2 .3','spot stim .1 .2 nan .1','head stim 0 0 0']) {
    assert.throws(()=>H.parse(fixture+'\r\n6='+bad));
  }
  const d=H.parse(fixture+'\r\n6=spot future 1 2 3 4');
  d.model.spots.stim.anchor='auto';
  assert.match(H.decode(H.serialize(d)).text,/spot future 1 2 3 4$/);
});
