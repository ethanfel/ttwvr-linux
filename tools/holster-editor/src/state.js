/* SPDX-License-Identifier: GPL-3.0-only */
/* Lossless TTWVR state editor. No filesystem or network access. */
(function (root) {
  'use strict';
  const ANCHORS = Object.freeze([
    ['auto', 'Automatic'], ['pelvis', 'Pelvis'], ['chest', 'Chest'],
    ['left_upper_arm', 'Left upper arm'], ['right_upper_arm', 'Right upper arm'],
    ['left_forearm', 'Left forearm'], ['right_forearm', 'Right forearm'],
    ['left_thigh', 'Left thigh'], ['right_thigh', 'Right thigh'],
    ['left_shin', 'Left shin'], ['right_shin', 'Right shin']
  ].map(([id, label]) => Object.freeze({id, label})));
  const names = new Set(ANCHORS.map(a => a.id));
  const numberPattern = /^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i;
  const clone = value => JSON.parse(JSON.stringify(value));
  function number(value, label) {
    if (!numberPattern.test(String(value)) || !Number.isFinite(Number(value)) || !Number.isFinite(Math.fround(Number(value)))) {
      throw new Error(`${label} must be a finite number.`);
    }
    return Number(value);
  }
  function decode(bytes) {
    if (!(bytes instanceof Uint8Array)) bytes = new Uint8Array(bytes);
    if (bytes.length > 4 * 1024 * 1024) throw new Error('This file is larger than 4 MB. Choose the TTWVR.state configuration file.');
    let encoding = 'utf-8', bom = 0;
    if (bytes[0] === 0xff && bytes[1] === 0xfe) { encoding = 'utf-16le'; bom = 2; }
    else if (bytes[0] === 0xfe && bytes[1] === 0xff) { encoding = 'utf-16be'; bom = 2; }
    else if (bytes[0] === 0xef && bytes[1] === 0xbb && bytes[2] === 0xbf) bom = 3;
    let text;
    try { text = new TextDecoder(encoding, {fatal: true, ignoreBOM: true}).decode(bytes.subarray(bom)); }
    catch { throw new Error('Unsupported text encoding. Use the original UTF-8 or UTF-16 TTWVR.state file.'); }
    if (text.includes('\0')) throw new Error('This looks like a binary file. Choose TTWVR.state, not a game save.');
    return {text, encoding, bom: bytes.slice(0, bom)};
  }
  function encode(text, encoding, bom) {
    let payload;
    if (encoding === 'utf-8') payload = new TextEncoder().encode(text);
    else {
      payload = new Uint8Array(text.length * 2);
      const view = new DataView(payload.buffer);
      for (let i = 0; i < text.length; i++) view.setUint16(i * 2, text.charCodeAt(i), encoding === 'utf-16le');
    }
    const out = new Uint8Array(bom.length + payload.length); out.set(bom); out.set(payload, bom.length); return out;
  }
  function readPouch(section) {
    const refs = {};
    const pouch = {body: null, bone: null, offset: null, radius: null};
    for (const r of section?.records || []) {
      const t = r.value.split(/\s+/);
      let field;
      if (t[0] === 'anchor') {
        field = 'anchor';
        if (t.length !== 14 || !/^[0-3]$/.test(t[1])) throw new Error('Unrecognized ammo pouch anchor.');
        pouch.bone = Number(t[1]);
        pouch.offset = t.slice(2, 5).map(v => number(v, 'Pouch translation'));
        pouch.rotation = t.slice(5).map(v => number(v, 'Pouch orientation'));
      } else if (t[0] === 'radius') {
        field = 'radius';
        if (t.length !== 2) throw new Error('Unrecognized ammo pouch radius.');
        pouch.radius = number(t[1], 'Pouch radius');
      } else if (/^[+\-.\d]/.test(t[0])) {
        field = 'body';
        if (t.length !== 4) throw new Error('Unrecognized ammo pouch position.');
        pouch.body = t.map(v => number(v, 'Pouch position'));
      } else continue;
      if (refs[field]) throw new Error('Duplicate ammo pouch record.');
      refs[field] = r;
    }
    if (section?.records.length && !pouch.body) throw new Error('No saved ammo pouch position.');
    return {pouch: pouch.body ? pouch : null, refs};
  }
  function parse(input) {
    const bytes = typeof input === 'string' ? new TextEncoder().encode(input) : new Uint8Array(input);
    const {text, encoding, bom} = decode(bytes);
    const lines = (text.match(/[^\r\n]*(?:\r\n|\n|\r|$)/g) || []).filter(s => s !== '').map(raw => {
      const eol = raw.match(/(?:\r\n|\n|\r)$/)?.[0] || ''; return {body: raw.slice(0, raw.length - eol.length), eol};
    });
    const newline = lines.find(l => l.eol)?.eol || '\r\n';
    const sections = {}; let section = null, pouchIssue = '', eyeHeight = null;
    lines.forEach((line, i) => {
      const header = line.body.match(/^\s*\[([^\]]+)\]\s*(?:[;#].*)?$/);
      if (header) {
        if (section) section.end = i;
        const name = header[1].trim().toLowerCase();
        section = {name, start: i, end: lines.length, records: []};
        if (name === 'savedslots' || name === 'savedslotson' || name === 'savedpouch') {
          if (name === 'savedpouch' && sections[name]) { pouchIssue = 'Duplicate ammo pouch section.'; return; }
          if (sections[name]) throw new Error(`Duplicate [${header[1]}] section. Resolve the duplicate before editing.`);
          sections[name] = section;
        }
      } else if (section) {
        if (section.name === 'savedheight') {
          const head = line.body.match(/^\s*Head\s*=\s*([^;#\s]+)\s*(?:[;#].*)?$/i);
          if (head && numberPattern.test(head[1])) {
            const height = Number(head[1]);
            if (height > 0 && Number.isFinite(Math.fround(height))) eyeHeight = height;
          }
        }
        const entry = line.body.match(/^(\s*)(\d+)(\s*=\s*)(.*)$/);
        if (entry) {
          const parts = entry[4].match(/^(.*?)(\s+[;#].*)?$/);
          section.records.push({line: i, key: Number(entry[2]), indent: entry[1], equals: entry[3], value: parts[1].trim(), suffix: parts[2] || ''});
        } else if (line.body.trim() && !/^\s*[;#]/.test(line.body) && Object.values(sections).includes(section)) {
          if (section === sections.savedpouch) { pouchIssue = 'Unrecognized ammo pouch record.'; return; }
          throw new Error(`Unrecognized entry on line ${i + 1}. Holster records must use numbered INI keys.`);
        }
      }
    });
    let pouch = null, pouchRefs = {};
    if (!pouchIssue) {
      try {
        assertKeys(sections.savedpouch);
        ({pouch, refs: pouchRefs} = readPouch(sections.savedpouch));
      } catch (e) { pouchIssue = e.message; }
    }
    const pouchOnly = !!pouch && !sections.savedslots && !sections.savedslotson;
    if (!pouchOnly && !sections.savedslots) throw new Error('No [SavedSlots] section found. Open TTWVR.state after placing a holster in VR and quitting the game.');
    const slots = Array.from({length: pouchOnly ? 0 : 8}, (_, i) => ({id: i + 1, position: null, radius: null, anchor: 'auto', offset: [0, 0, 0], enabled: false}));
    const refs = {positions: {}, bones: {}, on: null, show: null, pouch: pouchRefs};
    function assertKeys(s) {
      const keys = new Set();
      for (const r of s?.records || []) {
        if (keys.has(r.key)) throw new Error(`Duplicate record key ${r.key} on line ${r.line + 1}.`);
        keys.add(r.key);
      }
    }
    assertKeys(sections.savedslots); assertKeys(sections.savedslotson);
    for (const r of sections.savedslots?.records || []) {
      const t = r.value.split(/\s+/);
      if (t[0] === 'bone') {
        if (t.length !== 6 || !/^[1-8]$/.test(t[1])) throw new Error(`Invalid bone record on line ${r.line + 1}.`);
        const id = Number(t[1]);
        if (!names.has(t[2]) || t[2] === 'auto') throw new Error(`Unsupported anchor "${t[2]}" for holster ${id}. This editor supports the 1.1.5/1.1.6 format.`);
        if (refs.bones[id]) throw new Error(`Holster ${id} has more than one bone record.`);
        refs.bones[id] = r; slots[id - 1].anchor = t[2]; slots[id - 1].offset = t.slice(3).map(v => number(v, `Holster ${id} offset`));
      } else if (/^[+-]?\d/.test(t[0])) {
        if (t.length !== 5 || !/^[1-8]$/.test(t[0])) throw new Error(`Invalid position record on line ${r.line + 1}.`);
        const id = Number(t[0]);
        if (refs.positions[id]) throw new Error(`Holster ${id} has more than one position record.`);
        refs.positions[id] = r; slots[id - 1].position = t.slice(1, 4).map(v => number(v, `Holster ${id} position`));
        slots[id - 1].radius = number(t[4], `Holster ${id} radius`);
      }
      // Unknown extension records are retained verbatim.
    }
    const missing = slots.filter(s => s.position === null).map(s => s.id);
    if (missing.length) throw new Error(`Missing positions for holsters ${missing.join(', ')}. Let TTWVR save all eight holsters before editing this file.`);
    let show = null;
    for (const r of sections.savedslotson?.records || []) {
      const t = r.value.split(/\s+/);
      if (t[0] === 'on') {
        if (refs.on || t.length !== 9 || t.slice(1).some(v => !/^[+-]?\d+$/.test(v))) throw new Error('Invalid or duplicate holster enabled flags.');
        refs.on = r; slots.forEach((s, i) => { s.enabled = Number(t[i + 1]) !== 0; });
      } else if (t[0] === 'show') {
        if (refs.show || t.length !== 2 || !/^[+-]?\d+$/.test(t[1])) throw new Error('Invalid or duplicate holster visibility flag.');
        refs.show = r; show = Number(t[1]) !== 0;
      }
    }
    if (!pouchOnly && (!refs.on || !refs.show)) throw new Error('Missing [SavedSlotsOn] enabled/visibility flags. Toggle a holster in VR and quit so TTWVR writes the complete configuration.');
    const model = {slots, show, pouch};
    return {bytes: bytes.slice(), text, lines, newline, encoding, bom, sections, refs, pouchIssue, eyeHeight, original: clone(model), model};
  }
  function validate(model) {
    if (!model || !Array.isArray(model.slots) || !(model.slots.length === 8 && typeof model.show === 'boolean' || model.slots.length === 0 && model.show === null && model.pouch)) throw new Error('A configuration must contain eight holsters and a visibility flag.');
    if (model.pouch) {
      const {body, bone, offset, rotation, radius} = model.pouch;
      if (radius !== null) number(radius, 'Pouch radius');
      if (!Array.isArray(body) || body.length !== 4) throw new Error('Invalid ammo pouch position.');
      body.forEach(v => number(v, 'Pouch position'));
      if (bone !== null) {
        if (!Number.isInteger(bone) || bone < 0 || bone > 3 || !Array.isArray(offset) || offset.length !== 3) throw new Error('Invalid ammo pouch attachment.');
        offset.forEach(v => number(v, 'Pouch translation'));
        if (!Array.isArray(rotation) || rotation.length !== 9) throw new Error('Invalid ammo pouch orientation.');
        rotation.forEach(v => number(v, 'Pouch orientation'));
      }
    }
    model.slots.forEach((s, i) => {
      if (s.id !== i + 1 || typeof s.enabled !== 'boolean' || !names.has(s.anchor)) throw new Error(`Invalid holster ${i + 1}.`);
      for (const field of ['position', 'offset']) {
        if (!Array.isArray(s[field]) || s[field].length !== 3) throw new Error(`Invalid ${field} for holster ${s.id}.`);
        s[field].forEach(v => number(v, `Holster ${s.id} ${field}`));
      }
      number(s.radius, `Holster ${s.id} radius`);
    });
  }
  const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);
  function format(n) {
    number(n, 'Value');
    if (Object.is(n, -0)) return '0';
    // Keep the shortest decimal that round-trips the edited JavaScript value.
    // Rounding a double to nine digits can cross a float32 midpoint after a drag.
    // Unchanged records retain their original spelling in serialize().
    return String(n);
  }
  function serialize(doc, model = doc.model) {
    validate(model);
    if (same(model, doc.original)) return doc.bytes.slice();
    const replacements = new Map(); const additions = [];
    const payload = (r, value) => `${r.indent}${r.key}${r.equals}${value}${r.suffix}`;
    model.slots.forEach((s, i) => {
      const old = doc.original.slots[i], r = doc.refs.positions[s.id], bone = doc.refs.bones[s.id];
      if (!same(s.position, old.position) || s.radius !== old.radius) {
        if (s.radius !== old.radius && (s.radius < 0.05 || s.radius > 0.45)) throw new Error(`Holster ${s.id} radius must be between 5 and 45 cm.`);
        // Preserve the spelling of every unchanged number in the position row.
        const tokens = r.value.split(/\s+/);
        s.position.forEach((v, j) => { if (v !== old.position[j]) tokens[j + 1] = format(v); });
        if (s.radius !== old.radius) tokens[4] = format(s.radius);
        replacements.set(r.line, payload(r, tokens.join(' ')));
      }
      if (s.anchor !== old.anchor || (s.anchor !== 'auto' && !same(s.offset, old.offset))) {
        if (s.anchor === 'auto') { if (bone) replacements.set(bone.line, null); }
        else if (bone) {
          const tokens = bone.value.split(/\s+/); tokens[2] = s.anchor;
          s.offset.forEach((v, j) => { if (v !== old.offset[j]) tokens[j + 3] = format(v); });
          replacements.set(bone.line, payload(bone, tokens.join(' ')));
        } else additions.push(`bone ${s.id} ${s.anchor} ${s.offset.map(format).join(' ')}`);
      }
    });
    if (model.slots.some((s, i) => s.enabled !== doc.original.slots[i].enabled)) {
      replacements.set(doc.refs.on.line, payload(doc.refs.on, `on ${model.slots.map(s => +s.enabled).join(' ')}`));
    }
    if (model.show !== doc.original.show) replacements.set(doc.refs.show.line, payload(doc.refs.show, `show ${+model.show}`));
    const pouch = model.pouch, oldPouch = doc.original.pouch;
    const pouchSection = doc.sections.savedpouch;
    const pouchAdditions = [];
    let pouchRemoved = false;
    if (pouch && !same(pouch, oldPouch)) {
      if (doc.pouchIssue) throw new Error('Cannot replace an unknown ammo pouch layout.');
      if (pouch.radius !== null && pouch.radius !== oldPouch?.radius && (pouch.radius < .05 || pouch.radius > .45))
        throw new Error('Ammo pouch radius must be between 5 and 45 cm.');
      const anchorValues = p => p && p.bone !== null ? [p.bone, ...p.offset, ...p.rotation] : null;
      const editRecord = (field, prefix, previous, values) => {
        const record = doc.refs.pouch[field];
        if (same(previous, values)) return;
        if (!values) {
          replacements.set(record.line, record.suffix ? record.indent + record.suffix.trimStart() : null);
          pouchRemoved = true;
        } else if (!record) {
          pouchAdditions.push(prefix + values.map(format).join(' '));
        } else {
          // Preserve whitespace, inline comments and each untouched number.
          let index = prefix ? -2 : -1;
          const line = doc.lines[record.line].body.replace(/^(\s*\d+\s*=)(.*)$/, (_, key, rest) =>
            key + rest.replace(/\S+/g, word => {
              ++index;
              return index >= 0 && index < values.length && values[index] !== previous[index] ? format(values[index]) : word;
            }));
          replacements.set(record.line, line);
        }
      };
      editRecord('body', '', oldPouch?.body ?? null, pouch.body);
      editRecord('anchor', 'anchor ', anchorValues(oldPouch), anchorValues(pouch));
      editRecord('radius', 'radius ', oldPouch?.radius != null ? [oldPouch.radius] : null, pouch.radius !== null ? [pouch.radius] : null);
    }
    const section = doc.sections.savedslots;
    const structural = additions.length > 0 || model.slots.some(s => doc.refs.bones[s.id] && s.anchor === 'auto');
    const pouchStructural = pouchAdditions.length > 0 || pouchRemoved;
    let out = '', nextKey = 1, pouchKey = 1;
    for (let i = 0; i <= doc.lines.length; i++) {
      if (additions.length && i === section.end) {
        if (out && !/[\r\n]$/.test(out)) out += doc.newline;
        for (const value of additions) out += `${nextKey++}=${value}${doc.newline}`;
      }
      if (pouchAdditions.length && i === (pouchSection?.end ?? doc.lines.length)) {
        if (out && !/[\r\n]$/.test(out)) out += doc.newline;
        if (!pouchSection) out += '[SavedPouch]' + doc.newline;
        out += pouchAdditions.map(value => (pouchKey++) + '=' + value).join(doc.newline);
        if (i < doc.lines.length || doc.lines.at(-1)?.eol) out += doc.newline;
      }
      if (i === doc.lines.length) break;
      let body = replacements.has(i) ? replacements.get(i) : doc.lines[i].body;
      if (body === null) continue;
      if (section && structural && i > section.start && i < section.end && /^\s*\d+\s*=/.test(body)) body = body.replace(/^(\s*)\d+(\s*=)/, `$1${nextKey++}$2`);
      if (pouchSection && pouchStructural && i > pouchSection.start && i < pouchSection.end && /^\s*\d+\s*=/.test(body))
        body = body.replace(/^(\s*)\d+(\s*=)/, `$1${pouchKey++}$2`);
      out += body + doc.lines[i].eol;
    }
    if (pouchRemoved && pouchSection.end === doc.lines.length && !doc.lines.at(-1).eol)
      out = out.replace(/(?:\r\n|\n|\r)$/, '');
    const bytes = encode(out, doc.encoding, doc.bom);
    // Verify that exported bytes reload as the same editable state.
    const expected = clone(model); expected.slots.forEach(s => { if (s.anchor === 'auto') s.offset = [0, 0, 0]; });
    const actual = parse(bytes).model;
    actual.slots.forEach((s, i) => {
      for (const field of ['position', 'offset']) s[field].forEach((v, j) => {
        if (Math.fround(v) !== Math.fround(expected.slots[i][field][j])) throw new Error(`Export verification failed for holster ${s.id} ${field}[${j}].`);
      });
      if (Math.fround(s.radius) !== Math.fround(expected.slots[i].radius) || s.anchor !== expected.slots[i].anchor || s.enabled !== expected.slots[i].enabled) throw new Error('Export verification failed for a holster.');
    });
    if (!same(actual.pouch, expected.pouch)) throw new Error('Export verification failed for the ammo pouch.');
    if (actual.show !== expected.show) throw new Error('Export verification failed for visibility.');
    return bytes;
  }
  function demo() {
    const rows = ['; Demonstration layout. Not a copy of a player configuration.', '[SavedHeight]', 'Head=1.700', 'Hips=0.000', '[SavedSlots]'];
    for (let id = 1; id <= 8; id++) rows.push(`${id}=${id} 0.2500 ${[-0.225, -0.075, 0.075, 0.225][(id - 1) % 4].toFixed(4)} ${id <= 4 ? '-0.3500' : '-0.5000'} 0.0650`);
    rows.push('9=bone 1 right_upper_arm 0.1000 0.0000 0.0000', '10=bone 2 left_upper_arm 0.1000 0.0000 0.0000', '11=bone 4 pelvis 0.1000 0.0000 0.1200', '12=bone 5 pelvis -0.1000 0.0000 0.1200', '[SavedSlotsOn]', '1=on 1 1 0 1 1 0 0 0', '2=show 1', '[SavedWristBoy]', '1=hud', '[SavedPouch]', '1=0.18 -0.12 0.47 1.23', '2=anchor 3 -6 13 8 1 0 0 0 1 0 0 0 1', '3=radius 0.1125');
    return rows.join('\r\n') + '\r\n';
  }
  const api = {ANCHORS, parse, serialize, validate, clone, same, decode, encode, demo};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.HolsterState = api;
})(globalThis);
