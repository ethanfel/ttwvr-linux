/* SPDX-License-Identifier: GPL-3.0-only */
(() => {
  'use strict';
  const H = globalThis.HolsterState, $ = id => document.getElementById(id);
  const svgNS = 'http://www.w3.org/2000/svg';
  const points = {
    pelvis: [220, 266, 294, 279], chest: [220, 179, 282, 189],
    right_upper_arm: [142, 162, 118, 140], left_upper_arm: [298, 162, 322, 140],
    right_forearm: [116, 236, 92, 218], left_forearm: [324, 236, 348, 218],
    right_thigh: [195, 337, 140, 344], left_thigh: [245, 337, 300, 344],
    right_shin: [195, 432, 140, 439], left_shin: [245, 432, 300, 439]
  };
  const labels = Object.fromEntries(H.ANCHORS.map(a => [a.id, a.label]));
  let doc = null, model = null, selected = 1, filename = '', history = [], historyIndex = 0, exported = null, loading = 0, demoMode = false;
  const errors = new Map(), drafts = new Map(), numericFields = new Map();
  const dirty = () => model && (drafts.size > 0 || !H.same(model, exported || doc.original));
  const changedCount = () => model ? model.slots.filter((s, i) => !H.same(s, doc.original.slots[i])).length : 0;
  function notify(text, type = '') { $('notice').textContent = text; $('notice').className = `notice ${type}`; }
  function element(tag, attrs = {}, text = '') {
    const el = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs)) el.setAttribute(key, value);
    el.textContent = text; return el;
  }
  function svg(tag, attrs) {
    const el = document.createElementNS(svgNS, tag);
    for (const [key, value] of Object.entries(attrs)) el.setAttribute(key, value);
    return el;
  }
  for (const a of H.ANCHORS) $('anchor').append(element('option', {value: a.id}, a.label));
  for (const [anchor, [x, y, tx, ty]] of Object.entries(points)) {
    const g = svg('g', {class: 'anchor-point', 'data-anchor': anchor, role: 'button', tabindex: '-1', 'aria-label': `Assign ${labels[anchor]} anchor`, 'aria-disabled': 'true'});
    const title = svg('title', {}); title.textContent = labels[anchor]; g.append(title);
    g.append(svg('path', {class: 'leader', d: `M${x} ${y}L${tx} ${ty - 3}`}));
    const label = svg('text', {x: tx, y: ty - 7, class: 'anchor-label', 'text-anchor': tx < 220 ? 'end' : 'start'});
    label.textContent = labels[anchor].toUpperCase().replace('RIGHT ', 'R. ').replace('LEFT ', 'L. '); g.append(label);
    g.append(svg('circle', {cx: x, cy: y, r: 10, class: 'ring'}), svg('circle', {cx: x, cy: y, r: 2.4, class: 'core'}), svg('circle', {cx: x, cy: y, r: 21, class: 'hit-area'}));
    const assign = () => { if (doc && !errors.size) commit(m => { m.slots[selected - 1].anchor = anchor; }); };
    g.addEventListener('click', assign);
    g.addEventListener('keydown', e => { if (['Enter', ' '].includes(e.key)) { e.preventDefault(); assign(); } });
    $('anchor-points').append(g);
  }
  function numberDisplay(n) { return String(Number((n * 100).toFixed(5))); }
  function render() {
    const focus = document.activeElement;
    const focusSlot = focus?.dataset?.slot;
    const s = model?.slots[selected - 1];
    $('slot-list').replaceChildren();
    for (let id = 1; id <= 8; id++) {
      const row = model?.slots[id - 1];
      const button = element('button', {type: 'button', class: `slot${id === selected ? ' selected' : ''}`, 'data-slot': id, 'aria-pressed': String(id === selected), 'aria-label': `Holster ${id}${row ? `, ${labels[row.anchor]}, ${row.enabled ? 'enabled' : 'disabled'}` : ''}`});
      button.disabled = !doc;
      button.append(element('span', {class: 'slot-num'}, String(id).padStart(2, '0')));
      const text = element('span', {class: 'slot-text'});
      text.append(element('span', {class: 'slot-label'}, row ? labels[row.anchor] : `Holster ${id}`), element('span', {class: 'slot-detail'}, row ? `${row.enabled ? 'ON' : 'OFF'}${row.anchor === 'auto' ? ' / AUTO-ASSIGN' : ' / BONE ANCHOR'}` : 'NO CONFIGURATION'));
      button.append(text, element('i', {class: `slot-dot${row?.enabled ? ' on' : ''}`, 'aria-hidden': 'true'}));
      button.addEventListener('click', () => {
        if (errors.size) { notify('Correct the highlighted value before switching slots.', 'error'); return; }
        selected = id; render();
      });
      $('slot-list').append(button);
    }
    if (focusSlot) document.querySelector(`[data-slot="${focusSlot}"]`)?.focus({preventScroll: true});
    $('enabled-count').textContent = model ? `${model.slots.filter(x => x.enabled).length} / 8` : '— / 8';
    $('map-empty').hidden = !!doc;
    for (const point of document.querySelectorAll('.anchor-point')) {
      const active = s?.anchor === point.dataset.anchor;
      point.classList.toggle('selected', !!active); point.setAttribute('aria-pressed', String(!!active));
      point.setAttribute('aria-disabled', String(!doc)); point.setAttribute('tabindex', doc ? '0' : '-1');
    }
    $('slot-markers').replaceChildren();
    for (const anchor of Object.keys(points)) {
      const rows = model?.slots.filter(x => x.anchor === anchor) || [], [x, y] = points[anchor];
      rows.forEach((row, i) => {
        const angle = (i / Math.max(rows.length, 4)) * Math.PI * 2 - Math.PI / 2;
        const px = x + 26 * Math.cos(angle), py = y + 26 * Math.sin(angle);
        const g = svg('g', {class: `slot-marker${row.id === selected ? ' selected' : ''}${row.enabled ? '' : ' off'}`, 'pointer-events': 'none'});
        g.append(svg('path', {d: `M${x} ${y}L${px} ${py}`}), svg('circle', {cx: px, cy: py, r: 8}));
        const t = svg('text', {x: px, y: py}); t.textContent = row.id; g.append(t); $('slot-markers').append(g);
      });
    }
    $('selected-number').textContent = String(selected).padStart(2, '0');
    $('selected-name').textContent = s ? labels[s.anchor] : 'Awaiting file';
    $('enabled').disabled = !doc; $('enabled').checked = !!s?.enabled;
    $('editor-fields').disabled = !doc;
    $('anchor').value = s?.anchor || 'auto';
    $('radius').value = s ? numberDisplay(s.radius) : '6.5'; $('radius-slider').value = $('radius').value;
    for (const [i, id] of ['offset-x', 'offset-y', 'offset-z'].entries()) {
      $(id).value = s ? numberDisplay(s.offset[i]) : '0'; $(id).disabled = !doc || s.anchor === 'auto';
    }
    for (const [i, id] of ['position-f', 'position-r', 'position-u'].entries()) $(id).value = s ? numberDisplay(s.position[i]) : '0';
    $('offset-fields').classList.toggle('disabled', !doc || s.anchor === 'auto');
    $('anchor-help').textContent = s?.anchor === 'auto' ? 'The game chooses a nearby bone from the saved body-relative position.' : 'Click a body marker to change this anchor.';
    $('offset-help').textContent = s?.anchor === 'auto' ? 'Choose an explicit anchor to edit bone-local offsets.' : 'Axes follow the chosen bone. Changing the anchor keeps these offsets.';
    $('map-caption').textContent = s ? s.anchor === 'auto' ? `HOLSTER ${selected} / AUTOMATIC · choose a body anchor` : `HOLSTER ${selected} / ${labels[s.anchor].toUpperCase()}${s.enabled ? '' : ' · DISABLED'}` : 'Choose a slot, then click its body anchor.';
    $('show').checked = model ? model.show : true;
    $('reset-slot').disabled = !s || H.same(s, doc.original.slots[selected - 1]);
    for (const [id, value] of drafts) { $(id).value = value; if (errors.has(id)) $(id).setAttribute('aria-invalid', 'true'); }
    updateStatus();
  }
  function updateStatus() {
    const count = changedCount(), changed = model && !H.same(model, doc.original);
    $('export').disabled = !doc || demoMode || !!errors.size;
    $('export').title = demoMode ? 'Load your own state file to export changes' : 'Download a copy of TTWVR.state';
    $('undo').disabled = historyIndex <= 0; $('redo').disabled = historyIndex >= history.length - 1;
    $('revert').disabled = !changed && !errors.size;
    const extra = model && model.show !== doc.original.show ? ' + VISIBILITY' : '';
    $('change-status').textContent = !doc ? 'NO FILE LOADED' : errors.size ? 'CORRECT THE HIGHLIGHTED VALUE' : changed ? `${count} HOLSTER${count === 1 ? '' : 'S'} MODIFIED${extra}${dirty() ? '' : ' · COPY EXPORTED'}` : 'CONFIGURATION LOADED · NO CHANGES';
    $('status-light').classList.toggle('changed', !!dirty() || !!errors.size);
    $('file-badge').textContent = !doc ? 'STANDBY' : errors.size ? 'CHECK INPUT' : dirty() ? 'EDITED' : exported ? 'EXPORTED' : 'LOADED';
    $('file-badge').classList.toggle('changed', !!dirty() || !!errors.size);
  }
  function clearErrors() { for (const id of errors.keys()) $(id).removeAttribute('aria-invalid'); errors.clear(); drafts.clear(); }
  function commit(change, discardDrafts = false) {
    if (!doc) return;
    if (discardDrafts) clearErrors();
    if (errors.size) { notify('Correct the highlighted value before changing other settings.', 'error'); render(); return; }
    const next = H.clone(model);
    for (const [id, value] of drafts) numericFields.get(id).mutate(next.slots[selected - 1], Number(value) / 100);
    change(next); H.validate(next);
    if (!H.same(next, model)) {
      history = history.slice(0, historyIndex + 1); history.push(H.clone(next));
      if (history.length > 100) history.shift();
      historyIndex = history.length - 1; model = next;
      notify('Changes stay in this tab until you export a copy.');
    }
    clearErrors(); render();
  }
  function confirmReplace() { return demoMode || !(dirty() || errors.size) || confirm('Replace the current edits? Export a copy first if you want to keep them.'); }
  function install(bytes, name, isDemo = false) {
    const parsed = H.parse(bytes);
    doc = parsed; demoMode = isDemo; model = H.clone(doc.model); filename = name; selected = 1; history = [H.clone(model)]; historyIndex = 0; exported = null;
    clearErrors(); $('filename').textContent = name; $('file-kind').textContent = isDemo ? 'DEMONSTRATION FILE' : 'CONFIGURATION FILE';
    $('fallback-details').open = false; render();
    notify(isDemo ? 'Sample mode: try the controls, then load your own state file to export changes.' : `Loaded ${name}. Select a numbered slot, then choose an anchor.`, 'success');
  }
  async function loadFile(file) {
    if (!file) return;
    const request = ++loading;
    try {
      if (file.size > 4 * 1024 * 1024) throw new Error('This file is larger than 4 MB. Choose TTWVR.state.');
      const bytes = new Uint8Array(await file.arrayBuffer());
      if (request !== loading) return;
      install(bytes, file.name);
    } catch (e) { if (request === loading) notify(`Could not load ${file.name}: ${e.message}${doc ? ' Your current configuration is still open.' : ''}`, 'error'); }
  }
  $('load').addEventListener('click', () => { if (confirmReplace()) { $('file-input').value = ''; $('file-input').click(); } });
  $('file-input').addEventListener('change', e => { loadFile(e.target.files[0]); });
  $('demo').addEventListener('click', () => { if (confirmReplace()) { ++loading; install(H.demo(), 'Sample TTWVR.state', true); } });
  document.addEventListener('dragover', e => { if ([...e.dataTransfer.types].includes('Files')) { e.preventDefault(); $('drop-zone').classList.add('dragover'); } });
  document.addEventListener('dragleave', e => { if (!e.relatedTarget) $('drop-zone').classList.remove('dragover'); });
  document.addEventListener('drop', e => {
    e.preventDefault(); $('drop-zone').classList.remove('dragover');
    if (e.dataTransfer.files.length !== 1) { notify('Load one TTWVR.state file at a time.', 'error'); return; }
    if (confirmReplace()) loadFile(e.dataTransfer.files[0]);
  });
  $('enabled').addEventListener('change', e => commit(m => { m.slots[selected - 1].enabled = e.target.checked; }));
  $('anchor').addEventListener('change', e => commit(m => { m.slots[selected - 1].anchor = e.target.value; }));
  $('show').addEventListener('change', e => commit(m => { m.show = e.target.checked; }));
  function numeric(id, mutate, min = -Infinity, max = Infinity) {
    const el = $(id);
    numericFields.set(id, {mutate, min, max});
    el.addEventListener('input', () => {
      drafts.set(id, el.value);
      const n = el.valueAsNumber;
      if (!Number.isFinite(n) || n < min || n > max || !Number.isFinite(Math.fround(n / 100))) {
        errors.set(id, true); el.setAttribute('aria-invalid', 'true'); updateStatus();
      } else { errors.delete(id); el.removeAttribute('aria-invalid'); updateStatus(); }
    });
    el.addEventListener('change', () => {
      const n = el.valueAsNumber;
      if (!Number.isFinite(n) || n < min || n > max || !Number.isFinite(Math.fround(n / 100))) {
        errors.set(id, true); el.setAttribute('aria-invalid', 'true'); updateStatus();
        notify(id === 'radius' ? 'Grab radius must be between 5 and 45 cm.' : 'Enter a finite number for the highlighted position.', 'error'); return;
      }
      drafts.set(id, el.value);
      commit(() => {});
    });
  }
  numeric('radius', (s, v) => { s.radius = v; }, 5, 45);
  ['offset-x', 'offset-y', 'offset-z'].forEach((id, i) => numeric(id, (s, v) => { s.offset[i] = v; }));
  ['position-f', 'position-r', 'position-u'].forEach((id, i) => numeric(id, (s, v) => { s.position[i] = v; }));
  $('radius-slider').addEventListener('input', e => { $('radius').value = e.target.value; $('radius').dispatchEvent(new Event('input')); });
  $('radius-slider').addEventListener('change', e => commit(m => { m.slots[selected - 1].radius = Number(e.target.value) / 100; }));
  $('reset-slot').addEventListener('click', () => commit(m => { m.slots[selected - 1] = H.clone(doc.original.slots[selected - 1]); }, true));
  function travel(delta) {
    const next = historyIndex + delta;
    if (next < 0 || next >= history.length) return;
    clearErrors(); historyIndex = next; model = H.clone(history[historyIndex]); render(); notify(delta < 0 ? 'Change undone.' : 'Change restored.');
  }
  $('undo').addEventListener('click', () => travel(-1)); $('redo').addEventListener('click', () => travel(1));
  $('revert').addEventListener('click', () => { if (confirm('Revert all changes to the loaded file?')) commit(m => Object.assign(m, H.clone(doc.original)), true); });
  $('export').addEventListener('click', () => {
    if (!doc || demoMode || errors.size) return;
    if (drafts.size) commit(() => {});
    try {
      const bytes = H.serialize(doc, model); const blob = new Blob([bytes], {type: 'application/octet-stream'});
      const url = URL.createObjectURL(blob); const link = element('a', {href: url, download: 'TTWVR.state'});
      document.body.append(link); link.click(); link.remove(); setTimeout(() => URL.revokeObjectURL(url), 15000);
      exported = H.clone(model); updateStatus();
      notify('Export requested: TTWVR.state. After the download finishes, replace the original with the game closed.', 'success');
    } catch (e) { notify(`Export stopped: ${e.message}`, 'error'); }
  });
  for (const color of ['green', 'amber']) $(color).addEventListener('click', () => {
    document.documentElement.dataset.theme = color;
    for (const c of ['green', 'amber']) $(c).setAttribute('aria-pressed', String(c === color));
    try { localStorage.setItem('ttwvr-holster-theme', color); } catch { /* File URLs may disallow storage. */ }
  });
  try { if (localStorage.getItem('ttwvr-holster-theme') === 'amber') $('amber').click(); } catch { /* optional preference */ }
  $('help').addEventListener('click', () => $('instructions').showModal());
  $('close-help').addEventListener('click', () => $('instructions').close());
  $('instructions').addEventListener('click', e => { if (e.target === $('instructions')) {
    const r = $('instructions').getBoundingClientRect();
    if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) $('instructions').close();
  } });
  $('editor-form').addEventListener('submit', e => e.preventDefault());
  document.addEventListener('keydown', e => {
    if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName) || $('instructions').open) return;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') { e.preventDefault(); travel(e.shiftKey ? 1 : -1); }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') { e.preventDefault(); travel(1); }
  });
  window.addEventListener('beforeunload', e => { if (!demoMode && (dirty() || errors.size)) { e.preventDefault(); e.returnValue = ''; } });
  render();
})();
