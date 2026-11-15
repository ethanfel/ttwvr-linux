// SPDX-License-Identifier: GPL-3.0-only
const {test} = require('node:test');
const assert = require('node:assert/strict');
const H = require('../src/state.js');
const fixture = () => H.demo().replace('[SavedHeight]', '; untouched preamble\r\n[SavedHeight]').replace('[SavedWristBoy]', '[Unrelated]\r\nthing=leave = this alone ; #\r\nname=Étoile ☢\r\n[SavedWristBoy]');
const text = bytes => H.decode(bytes).text;
const section = (input, name) => input.match(new RegExp(`\\[${name}\\][\\s\\S]*?(?=\\r?\\n\\[|$)`))?.[0];
test('eight slots, hotkey numbers, anchors and enabled flags load', () => {
  const d = H.parse(fixture()); assert.equal(d.model.slots.length, 8); assert.equal(d.model.slots[0].anchor, 'right_upper_arm');
  assert.deepEqual(d.model.slots.filter(s => s.enabled).map(s => s.id), [1,2,4,5]); assert.equal(d.model.slots[0].radius, .065);
});
for (const newline of ['\r\n', '\n', '\r']) for (const encoding of ['utf-8', 'utf-16le', 'utf-16be']) {
  test(`no-op preserves exact bytes: ${encoding}, ${JSON.stringify(newline)}`, () => {
    const bom = encoding === 'utf-16le' ? [255,254] : encoding === 'utf-16be' ? [254,255] : [239,187,191];
    const bytes = H.encode(fixture().replaceAll('\r\n', newline), encoding, new Uint8Array(bom));
    assert.deepEqual(H.serialize(H.parse(bytes)), bytes);
  });
}
test('unmodified number spelling, comments, unrelated sections and CRLF survive an anchor change', () => {
  const input = fixture().replace('9=bone 1 right_upper_arm 0.1000 0.0000 0.0000', ' 9 = bone 1 right_upper_arm 0.1000 0.0000 0.0000 ; arm comment');
  const d = H.parse(input); d.model.slots[0].anchor = 'left_forearm'; const out = text(H.serialize(d));
  assert.equal(out, input.replace('bone 1 right_upper_arm', 'bone 1 left_forearm'));
});
test('bone records are added/removed and list keys renumbered without confusing keys with slot IDs', () => {
  const d = H.parse(fixture()); d.model.slots[0].anchor = 'auto'; d.model.slots[2].anchor = 'chest'; d.model.slots[2].offset = [.01,-.02,.03];
  const out = text(H.serialize(d)), reloaded = H.parse(out);
  assert.equal(reloaded.model.slots[0].anchor, 'auto'); assert.equal(reloaded.model.slots[2].anchor, 'chest');
  assert.deepEqual(reloaded.model.slots[2].offset, [.01,-.02,.03]);
  assert.deepEqual(reloaded.sections.savedslots.records.map(r => r.key), Array.from({length:12},(_,i)=>i+1));
  for (const s of ['SavedHeight','Unrelated','SavedWristBoy','SavedSlotsOn']) assert.equal(section(out,s),section(fixture(),s));
});
test('radius and positions can change without changing the bone offset', () => {
  const d=H.parse(fixture());d.model.slots[0].radius=.12;d.model.slots[0].position[0]=-.1234;
  const r=H.parse(H.serialize(d));assert.equal(r.model.slots[0].radius,.12);assert.equal(r.model.slots[0].position[0],-.1234);assert.deepEqual(r.model.slots[0].offset,[.1,0,0]);
});
test('flags and visibility change independently of positions', () => {
  const d=H.parse(fixture());d.model.slots[7].enabled=true;d.model.show=false;const out=text(H.serialize(d));
  assert.equal(section(out,'SavedSlots'),section(fixture(),'SavedSlots'));assert.match(out,/on 1 1 0 1 1 0 0 1/);assert.match(out,/show 0/);
});
test('unknown extension records and comments survive structural edits', () => {
  const input=fixture().replace('9=bone', '; section comment\r\n9=bone').replace('[SavedSlotsOn]','13=future record exactly\r\n[SavedSlotsOn]');
  const d=H.parse(input);d.model.slots[0].anchor='auto';const out=text(H.serialize(d));
  assert.match(out,/12=future record exactly/);assert.match(out,/; section comment\r\n/);
});
test('last section without final newline permits new anchors', () => {
  let input=fixture();const saved=section(input,'SavedSlots');input=input.replace(saved+'\r\n','')+'\r\n'+saved;
  const d=H.parse(input);d.model.slots[7].anchor='left_shin';const out=H.parse(H.serialize(d));assert.equal(out.model.slots[7].anchor,'left_shin');
});
test('UTF-16 export preserves encoding and unrelated Unicode', () => {
  for (const [encoding,bom] of [['utf-16le',[255,254]],['utf-16be',[254,255]],['utf-8',[239,187,191]]]) {
    const d=H.parse(H.encode(fixture(),encoding,new Uint8Array(bom)));d.model.slots[6].anchor='right_thigh';const bytes=H.serialize(d);
    assert.deepEqual([...bytes.slice(0,bom.length)],bom);assert.match(text(bytes),/Étoile ☢/);assert.equal(H.parse(bytes).model.slots[6].anchor,'right_thigh');
  }
});
test('all ten explicit anchors round-trip', () => {
  for (const a of H.ANCHORS.filter(a=>a.id!=='auto')) { const d=H.parse(fixture());d.model.slots[7].anchor=a.id;assert.equal(H.parse(H.serialize(d)).model.slots[7].anchor,a.id); }
});
test('reject missing, duplicate, malformed or incompatible holster records', () => {
  const cases=[['[Other]\nx=1','No [SavedSlots]'],[fixture()+'[SavedSlots]\r\n','Duplicate'],[fixture().replace('8=8 0.2500 0.2250 -0.5000 0.0650\r\n',''),'Missing positions'],[fixture().replace('right_upper_arm','unknown_bone'),'Unsupported anchor'],[fixture().replace('0.1000','NaN'),'finite'],[fixture().replace('2=show 1','2=show maybe'),'visibility'],[fixture().replace('1=on 1 1 0 1 1 0 0 0','1=on 1 0'),'flags'],[fixture().replace('8=8','8=7'),'more than one position'],[fixture().replace('9=bone','8=bone'),'Duplicate record key']];
  for(const [input,message] of cases) assert.throws(()=>H.parse(input),e=>e.message.includes(message),message);
  assert.throws(()=>H.parse(new Uint8Array([0xff,0xff,0xff])),/encoding/);
  assert.throws(()=>H.parse(new Uint8Array(4*1024*1024+1)),/larger/);
});
test('reject invalid edited values; original document remains unchanged', () => {
  const d=H.parse(fixture());const original=d.bytes.slice();
  d.model.slots[0].offset[0]=Infinity;assert.throws(()=>H.serialize(d),/finite/);
  d.model=H.clone(d.original);d.model.slots[0].radius=.46;assert.throws(()=>H.serialize(d),/5 and 45/);
  assert.deepEqual(d.bytes,original);
});
test('high precision edits survive as the same game float32 values', () => {
  const d=H.parse(fixture());d.model.slots[1].offset=[1/3,-1e-12,123456.789];
  const r=H.parse(H.serialize(d));r.model.slots[1].offset.forEach((v,i)=>assert.equal(Math.fround(v),Math.fround(d.model.slots[1].offset[i])));
});
