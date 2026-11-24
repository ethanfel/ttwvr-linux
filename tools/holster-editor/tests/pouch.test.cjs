// SPDX-License-Identifier: GPL-3.0-only
const {test} = require('node:test');
const assert = require('node:assert/strict');
const H = require('../src/state.js');
const G = require('../src/geometry.js');
const near = (actual, expected) => actual.forEach((v, i) => assert.ok(Math.abs(v - expected[i]) < 1e-8));
const noPouch = H.demo().split('[SavedPouch]')[0];

test('all four pouch bones move in body axes and preserve the saved rotation and radius', () => {
  for (let bone = 0; bone < 4; bone++) {
    const input = H.demo().replace('anchor 3 -6 13 8', 'anchor ' + bone + ' -6  13   8')
      .replace('1 0 0 0 1 0 0 0 1', '1.0  0 0 0 1.000 0 0 0 1 ; keep angle');
    const d = H.parse(input), start = d.model.pouch;
    const delta = [-.02, -.04, .08];
    d.model.pouch = G.shiftPouch(start, delta);
    const output = H.decode(H.serialize(d)).text;
    assert.ok(output.startsWith(input.split('[SavedPouch]')[0]));
    assert.ok(output.includes('1.0  0 0 0 1.000 0 0 0 1 ; keep angle'));
    assert.ok(output.includes('3=radius 0.1125'));
    const saved = H.parse(output).model.pouch;
    near(G.pouchWorld(saved), G.pouchWorld(start).map((v, i) => v + delta[i]));
    near(saved.body, [.22, -.14, .39, 1.31]);
  }
});

test('pouch-only and fallback-only files retain their encoding, comments and other records', () => {
  const text = '[SavedPouch]\r\n1 = .1 -0.20 .56 1.10 ; waist\r\n2=future flag';
  const bytes = H.encode(text, 'utf-16be', Uint8Array.of(254, 255));
  const d = H.parse(bytes);
  assert.deepEqual(d.model.slots, []);
  assert.deepEqual(H.serialize(d), bytes);
  near(G.pouchWorld(d.model.pouch), [-.2, -.1, 1.1]);
  d.model.pouch = G.shiftPouch(d.model.pouch, [.2, 0, 0]);
  const output = H.serialize(d);
  assert.equal(H.decode(output).encoding, 'utf-16be');
  assert.equal(H.decode(output).text, text.replace('-0.20', '0'));
});

test('an absent pouch stays absent until explicitly placed', () => {
  for (const input of [noPouch, noPouch.trimEnd()]) {
    const d = H.parse(input);
    assert.equal(d.model.pouch, null);
    assert.deepEqual(H.serialize(d), new TextEncoder().encode(input));
    d.model.pouch = {body: [.2, -.1, .55, 1.1], bone: null, offset: null, radius: null};
    const output = H.decode(H.serialize(d)).text;
    assert.ok(output.startsWith(input));
    assert.equal(output.endsWith('\r\n'), input.endsWith('\r\n'));
    assert.deepEqual(H.parse(output).model.pouch, d.model.pouch);
  }
});

test('an empty pouch section is filled in place', () => {
  const input = noPouch.replace('[SavedWristBoy]', '[SavedPouch]\r\n; pouch goes here\r\n[SavedWristBoy]');
  const d = H.parse(input);
  assert.equal(d.pouchIssue, '');
  d.model.pouch = {body: [.2, -.1, .55, 1.1], bone: null, offset: null, radius: null};
  assert.equal(H.decode(H.serialize(d)).text, input.replace('[SavedWristBoy]', '1=0.2 -0.1 0.55 1.1\r\n[SavedWristBoy]'));
});

test('unsupported pouch data does not prevent a regular holster edit', () => {
  for (const section of ['1=0 0 0 1\n2=anchor 8 0 0 0 1 0 0 0 1 0 0 0 1',
    '1=0 0 0 NaN', '1=0 0 0 1\n2=0 0 0 2', '1=0 0 0 1\n[SavedPouch]\n1=1 1 1 1']) {
    const d = H.parse(noPouch + '[SavedPouch]\n' + section);
    assert.ok(d.pouchIssue);
    assert.equal(d.model.pouch, null);
    d.model.slots[0].radius = .2;
    assert.ok(H.decode(H.serialize(d)).text.endsWith('[SavedPouch]\n' + section));
  }
});

test('automatic placement uses eye height and keeps its saved binding on move', () => {
  const d = H.parse(H.demo()), slot = d.model.slots[2];
  assert.equal(d.eyeHeight, 1.7);
  near(G.world(slot, d.eyeHeight), [.075, -.25, 1.35]);
  const front = G.moveSlot(slot, 'front', .02, -.03);
  const side = G.moveSlot(front, 'side', .07, .10);
  near(side.position, [.32, .055, -.28]);
  d.model.slots[2] = side;
  const saved = H.parse(H.serialize(d));
  assert.equal(saved.model.slots[2].anchor, 'auto');
  assert.equal(saved.refs.bones[3], undefined);
  near(G.world(saved.model.slots[2], saved.eyeHeight), [.055, -.32, 1.42]);
  assert.equal(H.parse(H.demo().replace('Head=1.700', 'Head=0.000')).eyeHeight, null);
  near(G.world(slot), [.075, -.25, G.referenceEyeHeight - .35]);
});
