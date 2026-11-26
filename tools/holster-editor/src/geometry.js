/* SPDX-License-Identifier: GPL-3.0-only */
// Reference coordinates: right, back, up (metres). Orthographic views, no perspective.
// Source skeleton is right, FORWARD, up; convert its world basis below. See FORMAT.md.
(function (root, factory) {
  const api = factory();
  if (typeof module === 'object' && module.exports) module.exports = api;
  else root.HolsterGeometry = api;
})(globalThis, () => {
  'use strict';
  const frames = {
  "pelvis": {
    "origin": [
      0,
      0.0093891,
      0.968155
    ],
    "rotation": [
      0,
      0,
      -1,
      0.0,
      1.0,
      0,
      1.0,
      -0.0,
      0
    ]
  },
  "chest": {
    "origin": [
      0,
      0.0301989,
      1.2925516
    ],
    "rotation": [
      0,
      0,
      -1,
      -0.1825425,
      0.983198,
      0,
      0.983198,
      0.1825425,
      0
    ]
  },
  "right_upper_arm": {
    "origin": [
      0.1963387,
      -0.0426637,
      1.465648
    ],
    "rotation": [
      0.8800016,
      0.049624,
      -0.4723716,
      -0.0399402,
      -0.9832735,
      -0.1777022,
      -0.4732887,
      0.1752449,
      -0.8633002
    ]
  },
  "right_forearm": {
    "origin": [
      0.4146253,
      -0.0525709,
      1.3482477
    ],
    "rotation": [
      0.844155,
      0.2535108,
      -0.4723716,
      0.1905045,
      -0.9654688,
      -0.1777022,
      -0.5011094,
      0.0600194,
      -0.8633002
    ]
  },
  "right_thigh": {
    "origin": [
      0.0997215,
      0.0093892,
      0.968155
    ],
    "rotation": [
      -0.0072024,
      -0.0093097,
      0.9999308,
      0.0454339,
      0.998921,
      0.0096276,
      -0.9989414,
      0.0455001,
      -0.0067716
    ]
  },
  "right_shin": {
    "origin": [
      0.0964325,
      0.0301365,
      0.5119898
    ],
    "rotation": [
      -0.00533,
      -0.0104946,
      0.9999308,
      -0.1425826,
      0.9897361,
      0.0096276,
      -0.9897686,
      -0.1425214,
      -0.0067716
    ]
  }
};
  const joints = {
  "head": [
    0.0,
    0.0057433,
    1.6116523
  ],
  "neck": [
    1e-07,
    -0.0130255,
    1.5253366
  ],
  "right_hand": [
    0.6332445,
    -0.0032341,
    1.2184703
  ],
  "right_toe": [0.1168718, 0.1371308, 0.0169413],
  "right_foot": [
    0.0942197,
    -0.0290597,
    0.101066
  ]
};
  const dot = (a, b) => a.reduce((v, x, i) => v + x * b[i], 0);
  const norm = v => { const n = Math.hypot(...v); return v.map(x => x / n); };
  const cross = (a, b) => [a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0]];
  const multiply = (r, v) => [0, 1, 2].map(i => dot(r.slice(i*3, i*3+3), v));
  const transpose = r => [0,3,6,1,4,7,2,5,8].map(i => r[i]);
  const reflect = p => [-p[0], p[1], p[2]];
  function opposite(anchor) {
    return anchor.startsWith('right_') ? anchor.replace('right_', 'left_') : anchor.startsWith('left_') ? anchor.replace('left_', 'right_') : anchor;
  }
  // Pouch bone IDs use the torso chain, independently of the holster anchor list.
  const lowerSpine = {
    origin: [0, .0323561, 1.0566253],
    rotation: [0, 0, -1, .0180342, .9998374, 0, .9998375, -.0180342, 0]
  };
  const middleSpine = {
    origin: [0, .0341797, 1.1634306],
    rotation: [0, 0, -1, -.0293736, .9995685, 0, .9995686, .0293735, 0]
  };
  const referenceFrames = {...frames, lowerSpine, middleSpine};
  for (const [key, f] of Object.entries(referenceFrames)) {
    // Orthonormalize rounded reference data; transforms remain invertible.
    const x = norm([f.rotation[0], f.rotation[3], f.rotation[6]]);
    const z = norm(cross(x, [f.rotation[1], f.rotation[4], f.rotation[7]]));
    const y = cross(z, x); f.rotation = [0,1,2].flatMap(i => [x[i],y[i],z[i]]);
    if (key.startsWith('right_')) {
      frames[opposite(key)] = {origin: reflect(f.origin), rotation: f.rotation.map((v,i) => v * (i < 3 ? -1 : 1) * (i % 3 === 2 ? -1 : 1))};
    }
  }
  // NIF +Y points toward the toes. The UI uses +Y for BACK, so reflect the
  // world basis AFTER orthonormalizing the source frames. This changes their
  // determinant, not their inverse (transpose). Never reflect bone-local Y.
  const toPreview = p => [p[0], -p[1], p[2]];
  for (const [key, p] of Object.entries(joints)) joints[key] = toPreview(p);
  for (const [key, f] of Object.entries({...frames, lowerSpine, middleSpine})) {
    f.origin = toPreview(f.origin);
    f.rotation = f.rotation.map((v, i) => i >= 3 && i < 6 ? -v : v);
    joints[key] = f.origin;
  }
  for (const key of ['right_hand', 'right_foot', 'right_toe']) joints[opposite(key)] = reflect(joints[key]);
  // 1.1.8 stores head-following offsets as forward, right, up from the neck.
  // Set the neutral preview frame after converting the skeleton to right/back/up.
  frames.head = {origin:[0,joints.neck[1],joints.neck[2]], rotation:[0,1,0,-1,0,0,0,0,1]};
  const referenceEyeHeight = 1.67;
  function world(slot, eyeHeight = referenceEyeHeight) {
    if (slot.anchor === 'auto') {
      const [forward, right, up] = slot.position;
      return [right, -forward, eyeHeight + up];
    }
    const f = frames[slot.anchor]; if (!f) return null;
    return multiply(f.rotation, slot.offset).map((v,i) => v + f.origin[i]);
  }
  function local(anchor, p) {
    const f = frames[anchor]; if (!f) throw new Error('Choose an explicit anchor to position a holster.');
    return multiply(transpose(f.rotation), p.map((v,i) => v - f.origin[i]));
  }
  const pouchFrames = [frames.pelvis, lowerSpine, middleSpine, frames.chest];
  const pouchLabels = ['Pelvis', 'Lower spine', 'Middle spine', 'Upper spine'];
  const unitsPerMetre = 70;
  function pouchWorld(pouch) {
    if (pouch.bone === null) return [pouch.body[1], -pouch.body[0], pouch.body[3]];
    const frame = pouchFrames[pouch.bone];
    return multiply(frame.rotation, pouch.offset).map((v, i) => frame.origin[i] + v / unitsPerMetre);
  }
  // Preserve the preview centre when moving a legacy pouch to the new slot format.
  function pouchSpot(pouch, eyeHeight) {
    const anchor = pouch.bone === null ? 'auto' : pouch.bone === 0 ? 'pelvis' : 'chest';
    const point = pouchWorld(pouch);
    return {id:'pouch', anchor, offset:anchor === 'auto' ? [0,0,0] : local(anchor,point),
      position:[pouch.body[0],pouch.body[1],anchor === 'auto' ? point[2] - (eyeHeight ?? referenceEyeHeight) : -pouch.body[2]], radius:pouch.radius ?? .15};
  }
  function pouchLocal(bone, point) {
    const frame = pouchFrames[bone];
    return multiply(transpose(frame.rotation), point.map((v,i)=>(v-frame.origin[i])*unitsPerMetre));
  }
  // Native orientation records store a row-major matrix. Convert each column
  // between torso frames, just as we convert a direction without translation.
  function reanchorPouch(pouch, bone) {
    if (bone === pouch.bone) return pouch;
    const point = pouchWorld(pouch);
    if (bone === null) {
      const [right, back, up] = point;
      const eyeHeight = pouch.body[2] + pouch.body[3];
      const result = {...pouch, body: [-back, right, eyeHeight - up, up], bone: null, offset: null};
      delete result.rotation;
      return result;
    }
    const next = pouchFrames[bone], inverse = transpose(next.rotation);
    const offset = pouchLocal(bone, point);
    // A body-relative pouch has no saved rotation. Start it upright in the
    // reference body's native right/forward/up basis.
    const axes = pouch.bone === null ? [[1,0,0],[0,-1,0],[0,0,1]] :
      [0,1,2].map(c => multiply(pouchFrames[pouch.bone].rotation, [0,1,2].map(r => pouch.rotation[r*3+c])));
    const rotation = transpose(axes.flatMap(axis => multiply(inverse, axis)));
    return {...pouch, bone, offset, rotation};
  }
  function shiftPouch(pouch, delta) {
    // Both records describe the same move. Retain calibration differences
    // between the fallback and the skeleton, including eye vs. floor height.
    const [right, back, up] = delta;
    const body = pouch.body.map((v, i) => v + [-back, right, -up, up][i]);
    let offset = pouch.offset;
    if (pouch.bone !== null) {
      const localDelta = multiply(transpose(pouchFrames[pouch.bone].rotation), delta);
      offset = offset.map((v, i) => v + localDelta[i] * unitsPerMetre);
    }
    return {...pouch, body, offset};
  }
  // Preserve the hidden dimension when dragging in either orthographic view.
  function move(slot, view, horizontal, vertical) {
    if (slot.anchor === 'auto') return slot.offset.slice();
    const p = world(slot); if (!p) return slot.offset.slice();
    p[view === 'side' ? 1 : 0] -= horizontal; p[2] += vertical;
    return local(slot.anchor, p);
  }
  function moveSlot(slot, view, horizontal, vertical) {
    if (slot.anchor !== 'auto') return {...slot, offset: move(slot, view, horizontal, vertical)};
    const [forward, right, up] = slot.position;
    return {...slot, position: [forward + (view === 'side' ? horizontal : 0),
      right - (view === 'front' ? horizontal : 0), up + vertical]};
  }
  function mirror(slot, targetId) {
    if (!Number.isInteger(targetId) || targetId < 1 || targetId > 8 || targetId === slot.id) throw new Error('Choose a different destination slot.');
    // Bilateral bone frames mirror local Z, NOT local X. Central anchors also use Z laterally.
    return {...slot, id: targetId, anchor: opposite(slot.anchor), offset: slot.anchor === 'auto' ? [0,0,0] : slot.offset.map((v,i) => i === (slot.anchor === 'head' ? 1 : 2) ? -v : v), position: [slot.position[0],-slot.position[1],slot.position[2]]};
  }
  function repairBodyPlacement(slot) {
    // Explicit recovery for pre-1.4 editor exports. Leave arms and Automatic
    // untouched. Only move centres behind the reference body's centre plane;
    // repeating this operation therefore cannot send a repaired slot back.
    if (!['pelvis','chest','left_thigh','right_thigh','left_shin','right_shin'].includes(slot.anchor)) return slot;
    const p = world(slot);
    if (p[1] <= 1e-6) return slot;
    return {...slot, offset: local(slot.anchor, [p[0], -p[1], p[2]])};
  }
  const SCALE = 230;
  const project = (p, view = 'front') => [220 - p[view === 'side' ? 1 : 0] * SCALE, 490 - p[2] * SCALE];
  return {frames, joints, pouchWorld, pouchSpot, pouchLocal, shiftPouch, reanchorPouch, pouchFrames, pouchLabels, referenceEyeHeight, world, moveSlot, local, move, mirror, opposite, reflect, repairBodyPlacement, project, SCALE};
});
