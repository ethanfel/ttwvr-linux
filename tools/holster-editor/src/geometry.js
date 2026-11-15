/* SPDX-License-Identifier: GPL-3.0-only */
// Reference coordinates: right, back, up (metres). Orthographic views, no perspective.
// Frame orientation checked against TTWVR 1.1.6's supplied skeleton. See FORMAT.md.
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
  for (const [key, f] of Object.entries(frames)) {
    // Orthonormalize rounded reference data; transforms remain invertible.
    const x = norm([f.rotation[0], f.rotation[3], f.rotation[6]]);
    const z = norm(cross(x, [f.rotation[1], f.rotation[4], f.rotation[7]]));
    const y = cross(z, x); f.rotation = [0,1,2].flatMap(i => [x[i],y[i],z[i]]);
    if (key.startsWith('right_')) {
      frames[opposite(key)] = {origin: reflect(f.origin), rotation: f.rotation.map((v,i) => v * (i < 3 ? -1 : 1) * (i % 3 === 2 ? -1 : 1))};
    }
  }
  for (const [key, f] of Object.entries(frames)) joints[key] = f.origin;
  for (const key of ['right_hand', 'right_foot']) joints[opposite(key)] = reflect(joints[key]);
  function world(slot) {
    const f = frames[slot.anchor]; if (!f) return null;
    return multiply(f.rotation, slot.offset).map((v,i) => v + f.origin[i]);
  }
  function local(anchor, p) {
    const f = frames[anchor]; if (!f) throw new Error('Choose an explicit anchor to position a holster.');
    return multiply(transpose(f.rotation), p.map((v,i) => v - f.origin[i]));
  }
  // Preserve the hidden dimension when dragging in either orthographic view.
  function move(slot, view, horizontal, vertical) {
    const p = world(slot); if (!p) return slot.offset.slice();
    p[view === 'side' ? 1 : 0] -= horizontal; p[2] += vertical;
    return local(slot.anchor, p);
  }
  function mirror(slot, targetId) {
    if (!Number.isInteger(targetId) || targetId < 1 || targetId > 8 || targetId === slot.id) throw new Error('Choose a different destination slot.');
    // Bilateral bone frames mirror local Z, NOT local X. Central anchors also use Z laterally.
    return {...slot, id: targetId, anchor: opposite(slot.anchor), offset: slot.anchor === 'auto' ? [0,0,0] : [slot.offset[0],slot.offset[1],-slot.offset[2]], position: [slot.position[0],-slot.position[1],slot.position[2]]};
  }
  const SCALE = 230;
  const project = (p, view = 'front') => [220 - p[view === 'side' ? 1 : 0] * SCALE, 490 - p[2] * SCALE];
  return {frames, joints, world, local, move, mirror, opposite, reflect, project, SCALE};
});
