/* SPDX-License-Identifier: GPL-3.0-only */
(() => {
  'use strict';
  const H = globalThis.HolsterState, G = globalThis.HolsterGeometry, P = globalThis.HolsterPresets, $ = id => document.getElementById(id);
  const svgNS = 'http://www.w3.org/2000/svg';
  const labels = Object.fromEntries(H.ANCHORS.map(a => [a.id, a.label]));
  let doc = null, model = null, selected = 1, filename = '', history = [], historyIndex = 0, exported = null, loading = 0, demoMode = false;
  let view = 'front', mirrorTarget = 2, drag = null, presetId = 'wasteland_rig';
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
  const pouchId = 'pouch';
  const pouchInputs = ['pouch-right', 'pouch-forward', 'pouch-height'];
  const activeRow = (m, id) => id === pouchId ? m?.pouch : m?.slots[id - 1];
  const rowWorld = (row, id) => id === pouchId ? G.pouchWorld(row) : G.world(row, doc?.eyeHeight ?? G.referenceEyeHeight);
  function assignRow(m, id, row) {
    if (id === pouchId) m.pouch = row;
    else m.slots[id - 1] = row;
  }
  function moveRow(row, id, horizontal, vertical) {
    if (id !== pouchId) return G.moveSlot(row, view, horizontal, vertical);
    return G.shiftPouch(row, view === 'front' ? [-horizontal, 0, vertical] : [0, -horizontal, vertical]);
  }
  function selectRow(id) {
    if (errors.size) { notify('Correct the highlighted value before switching equipment.', 'error'); return; }
    if (drafts.size) commit(() => {});
    selected = id;
    if (activeRow(model, id)?.anchor === 'auto') $('fallback-details').open = true;
    render();
  }
  $('select-pouch').addEventListener('click', () => selectRow(pouchId));
  $('place-pouch').addEventListener('click', () => {
    if (!doc || model.pouch || doc.pouchIssue) return;
    // A proposed front-waist position, created only on an explicit click.
    const eyes = doc.eyeHeight ?? G.referenceEyeHeight;
    const height = eyes * 2 / 3;
    commit(m => { m.pouch = {body: [.18, -.12, eyes - height, height], bone: null, offset: null, radius: null}; });
  });
  function numberDisplay(n) { return String(Number((n * 100).toFixed(5))); }
  function render() {
    const focus = document.activeElement;
    const focusSlot = focus?.dataset?.slot;
    const s = model?.slots[selected - 1];
    $('slot-list').replaceChildren();
    for (let id = 1; id <= 8; id++) {
      const row = model?.slots[id - 1];
      const button = element('button', {type: 'button', class: `slot${id === selected ? ' selected' : ''}`, 'data-slot': id, 'aria-pressed': String(id === selected), 'aria-label': `Holster ${id}${row ? `, ${labels[row.anchor]}, ${row.enabled ? 'enabled' : 'disabled'}` : ''}`});
      button.disabled = !row;
      button.append(element('span', {class: 'slot-num'}, String(id).padStart(2, '0')));
      const text = element('span', {class: 'slot-text'});
      text.append(element('span', {class: 'slot-label'}, row ? labels[row.anchor] : `Holster ${id}`), element('span', {class: 'slot-detail'}, row ? `${row.enabled ? 'ON' : 'OFF'}${row.anchor === 'auto' ? ' / AUTO-ASSIGN' : ' / BONE ANCHOR'}` : 'NO CONFIGURATION'));
      button.append(text, element('i', {class: `slot-dot${row?.enabled ? ' on' : ''}`, 'aria-hidden': 'true'}));
      button.addEventListener('click', () => selectRow(id));
      $('slot-list').append(button);
    }
    if (focusSlot) document.querySelector(`[data-slot="${focusSlot}"]`)?.focus({preventScroll: true});
    $('enabled-count').textContent = model ? `${model.slots.filter(x => x.enabled).length} / 8` : '— / 8';
    const pouchSelected = selected === pouchId;
    $('select-pouch').disabled = !doc;
    $('select-pouch').setAttribute('aria-pressed', String(pouchSelected));
    $('pouch-panel').hidden = !pouchSelected;
    $('holster-heading').hidden = $('editor-form').hidden = pouchSelected;
    $('editor-title').textContent = pouchSelected ? 'AMMO POUCH' : 'SLOT DETAILS';
    $('reset-slot').textContent = pouchSelected ? 'Reset pouch' : 'Reset slot';
    $('open-presets').disabled = !!model && !model.slots.length;
    if (pouchSelected) {
      const pouch = model.pouch;
      $('place-pouch').hidden = !!pouch || !!doc.pouchIssue;
      $('pouch-fields').hidden = !pouch;
      $('pouch-anchor').textContent = doc.pouchIssue ? doc.pouchIssue + ' Pouch editing is unavailable for this file.' :
        !pouch ? 'This file has no saved ammo pouch. Start at the front of the waist and adjust the placement.' :
        pouch.bone === null ? 'Uses the saved body-relative position.' : 'Attached to ' + G.pouchLabels[pouch.bone].toLowerCase() + '.';
      if (pouch) displayPouch(pouch);
    }
    $('map-empty').hidden = !!doc;
    renderPreview();
    const targetSelect = $('mirror-target');
    if (mirrorTarget === selected) mirrorTarget = selected === 1 ? 2 : 1;
    targetSelect.replaceChildren();
    for (const row of model?.slots || []) {
      if (row.id === selected) continue;
      targetSelect.append(element('option', {value: row.id}, `${String(row.id).padStart(2,'0')} · ${labels[row.anchor]} · ${row.enabled ? 'ON' : 'OFF'}`));
    }
    targetSelect.value = String(mirrorTarget);
    $('mirror-slot').disabled = !doc || !!errors.size;
    $('mirror-slot').textContent = `Mirror ${String(selected).padStart(2,'0')} → ${String(mirrorTarget).padStart(2,'0')}`;
    $('mirror-description').textContent = s ? `${labels[s.anchor]} → ${labels[G.opposite(s.anchor)]}. Replaces slot ${mirrorTarget} (position, radius, enabled). Undo restores it; later edits stay independent.` : 'Copy to the opposite side in a different slot.';
    $('selected-number').textContent = String(selected).padStart(2, '0');
    $('selected-name').textContent = s ? labels[s.anchor] : 'Awaiting file';
    $('enabled').disabled = !s; $('enabled').checked = !!s?.enabled;
    $('editor-fields').disabled = !s;
    $('anchor').value = s?.anchor || 'auto';
    $('radius').value = s ? numberDisplay(s.radius) : '6.5'; $('radius-slider').value = $('radius').value;
    for (const [i, id] of ['offset-x', 'offset-y', 'offset-z'].entries()) {
      $(id).value = s ? numberDisplay(s.offset[i]) : '0'; $(id).disabled = !s || s.anchor === 'auto';
    }
    for (const [i, id] of ['position-f', 'position-r', 'position-u'].entries()) $(id).value = s ? numberDisplay(s.position[i]) : '0';
    $('offset-fields').classList.toggle('disabled', !s || s.anchor === 'auto');
    $('anchor-help').textContent = s?.anchor === 'auto' ? 'The game chooses a nearby bone from the saved body-relative position.' : 'Click a body joint to change this anchor.';
    $('offset-help').textContent = s?.anchor === 'auto' ? 'Move the saved position on the map or enter its body-relative coordinates below.' : 'Drag the preview to position this holster. X/Y/Z follow the chosen bone.';
    $('map-caption').textContent = s ? s.anchor === 'auto' ? `HOLSTER ${selected} / AUTOMATIC · SAVED POSITION` : `HOLSTER ${selected} / ${labels[s.anchor].toUpperCase()}${s.enabled ? '' : ' · DISABLED'}` : 'Choose a slot, then click its body anchor.';
    $('show').checked = model ? model.show : true;
    if (pouchSelected) $('map-caption').textContent = 'AMMO POUCH';
    $('reset-slot').disabled = !doc || H.same(activeRow(model, selected), activeRow(doc.original, selected));
    for (const [id, value] of drafts) { $(id).value = value; if (errors.has(id)) $(id).setAttribute('aria-invalid', 'true'); }
    updateStatus();
  }
  function updateStatus() {
    const count = changedCount(), changed = model && !H.same(model, doc.original);
    $('export').disabled = !doc || demoMode || !!errors.size;
    $('mirror-slot').disabled = !doc || !!errors.size;
    const repairCount = model?.slots.filter(s => G.repairBodyPlacement(s) !== s).length || 0;
    $('repair-body').disabled = !repairCount || !!errors.size;
    $('repair-body').textContent = `Repair old body placement${repairCount ? ` (${repairCount} slots)` : ''}`;
    $('export').title = demoMode ? 'Load your own state file to export changes' : 'Download a copy of TTWVR.state';
    $('undo').disabled = historyIndex <= 0; $('redo').disabled = historyIndex >= history.length - 1;
    $('revert').disabled = !changed && !errors.size;
    const changes = [];
    if (count) changes.push(`${count} HOLSTER${count === 1 ? '' : 'S'}`);
    if (model && !H.same(model.pouch, doc.original.pouch)) changes.push('AMMO POUCH');
    if (model && model.show !== doc.original.show) changes.push('VISIBILITY');
    $('change-status').textContent = !doc ? 'NO FILE LOADED' : errors.size ? 'CORRECT THE HIGHLIGHTED VALUE' : changed ? `${changes.join(' + ')} MODIFIED${dirty() ? '' : ' · COPY EXPORTED'}` : 'CONFIGURATION LOADED · NO CHANGES';
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
    if (drafts.size) assignRow(next, selected, rowWithDrafts(activeRow(next, selected)));
    change(next);
    try { H.validate(next); }
    catch (e) { notify(e.message, 'error'); return; }
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
    drag = null; $('body-map').setAttribute('viewBox', '0 0 440 540');
    doc = parsed; demoMode = isDemo; model = H.clone(doc.model); filename = name; selected = model.slots.length ? 1 : pouchId; history = [H.clone(model)]; historyIndex = 0; exported = null;
    clearErrors(); $('filename').textContent = name; $('file-kind').textContent = isDemo ? 'DEMONSTRATION FILE' : 'CONFIGURATION FILE';
    $('fallback-details').open = activeRow(model, selected)?.anchor === 'auto'; render();
    notify(isDemo ? 'Sample mode: try the controls, then load your own state file to export changes.' : `Loaded ${name}. Select a holster or the ammo pouch.`, 'success');
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
  // Preview uses a reference skeleton only; it never changes calibration data.
  function displayPouch(pouch) {
    const [right, back, height] = G.pouchWorld(pouch);
    [right, -back, height].forEach((v, i) => { $(pouchInputs[i]).value = numberDisplay(v); });
  }
  function rowWithDrafts(row) {
    if (!row || errors.size || !drafts.size) return row;
    if (selected === pouchId) {
      const point = G.pouchWorld(row);
      const delta = pouchInputs.map((id, i) => drafts.has(id) ? Number(drafts.get(id)) / 100 * (i === 1 ? -1 : 1) - point[i] : 0);
      return G.shiftPouch(row, delta);
    }
    const copy = H.clone(row);
    for (const [id, value] of drafts) numericFields.get(id).mutate(copy, Number(value) / 100);
    return copy;
  }
  function previewSlot() {
    return drag ? drag.slot : rowWithDrafts(activeRow(model, selected));
  }
  function project(point) { return G.project(point, view); }
  function drawBody(body = $('reference-body'), projection = view) {
    body.replaceChildren();
    const projected = p => G.project(p, projection);
    const shape = points => points.map((p,i) => `${i ? 'L' : 'M'}${projected(p).join(' ')}`).join(' ');
    const j = G.joints;
    const path = (points, width, className) => body.append(svg('path', {d: shape(points), 'stroke-width': width * G.SCALE, class: className}));
    // Original vector schematic, no mesh assets. Capsules follow the reference joints.
    const limb = (a,b,width) => {
      path([j[a],j[b]], width + .008, 'body-outline');
      path([j[a],j[b]], width, 'body-limb');
      path([j[a],j[b]], .003, 'body-bone');
    };
    for (const side of ['left','right']) {
      limb(`${side}_thigh`,`${side}_shin`,.13);
      limb(`${side}_shin`,`${side}_foot`,.095);
      limb(`${side}_upper_arm`,`${side}_forearm`,.10);
      limb(`${side}_forearm`,`${side}_hand`,.075);
      path([j[`${side}_foot`],j[`${side}_toe`]],.08,'body-limb');
    }
    if (projection === 'front') {
      const torso = ['right_upper_arm','chest','left_upper_arm','left_thigh','pelvis','right_thigh'].map(k=>j[k]);
      // Shoulder-to-hip silhouette, with a short neck.
      torso[1] = j.neck;
      body.append(svg('path',{d:shape(torso)+'Z',class:'body-torso'}));
    } else {
      path([j.neck,j.chest,j.pelvis],.22,'body-outline');
      path([j.neck,j.chest,j.pelvis],.21,'body-limb');
    }
    path([j.neck,j.head],.09,'body-limb');
    path([j.pelvis,j.chest,j.neck],.003,'body-bone');
    const head = projected([j.head[0],j.head[1],j.head[2]+.055]);
    body.append(svg('ellipse',{cx:head[0],cy:head[1],rx:.084*G.SCALE,ry:.118*G.SCALE,class:'body-torso'}));
    if (projection === 'side') body.append(svg('path',{d:`M${head[0]+17} ${head[1]-6}l9 8-9 3`,class:'body-torso'}));
    // A waist band makes the belt row legible in front and side projections.
    const band=projection==='front'?[[-.18,-.12,1.025],[.18,-.12,1.025],[.18,-.12,.975],[-.18,-.12,.975]]:[[0,-.12,1.025],[0,.12,1.025],[0,.12,.975],[0,-.12,.975]];
    body.append(svg('path',{d:shape(band)+'Z',class:'body-belt'}));
    if(projection==='front')body.append(svg('path',{d:shape([[-.025,-.125,1.025],[.025,-.125,1.025],[.025,-.125,.975],[-.025,-.125,.975]])+'Z',class:'body-buckle'}));
    const floor = svg('text',{x:18,y:506,class:'map-direction'}); floor.textContent='REFERENCE / ~1.8 M'; body.append(floor);
  }
  function renderPreview() {
    drawBody();
    const current = previewSlot();
    $('direction-left').textContent = view === 'front' ? 'YOUR RIGHT' : 'BEHIND';
    $('direction-right').textContent = view === 'front' ? 'YOUR LEFT' : 'IN FRONT';
    $('anchor-points').replaceChildren();
    const anchors = Object.entries(G.frames).sort(([a],[b]) => Number(a === current?.anchor) - Number(b === current?.anchor));
    for (const [anchor, frame] of selected === pouchId ? [] : anchors) {
      const [x,y] = project(frame.origin);
      const g = svg('g',{class:`anchor-point${anchor === current?.anchor ? ' selected' : ''}`, 'data-anchor':anchor, role:'button', tabindex:doc ? '0' : '-1', 'aria-label':`Assign ${labels[anchor]} anchor`, 'aria-disabled':String(!doc), 'aria-pressed':String(anchor === current?.anchor)});
      const title=svg('title',{}); title.textContent=labels[anchor]; g.append(title);
      g.append(svg('circle',{cx:x,cy:y,r:7,class:'ring'}), svg('circle',{cx:x,cy:y,r:2,class:'core'}),svg('circle',{cx:x,cy:y,r:11,class:'hit-area'}));
      const assign = () => { if (doc && !errors.size) commit(m => { m.slots[selected-1].anchor=anchor; }); };
      g.addEventListener('click',assign);
      g.addEventListener('keydown',e => { if (['Enter',' '].includes(e.key)) { e.preventDefault();assign();document.querySelector(`[data-anchor="${anchor}"]`)?.focus({preventScroll:true}); } });
      $('anchor-points').append(g);
    }
    $('slot-markers').replaceChildren(); $('preview-handles').replaceChildren();
    const ids = model?.slots.map(s => s.id) || [];
    if (model?.pouch) ids.push(pouchId);
    ids.sort((a, b) => Number(a === selected) - Number(b === selected));
    for (const id of ids) {
      const chosen = id === selected, isPouch = id === pouchId;
      const row = chosen ? current : activeRow(model, id);
      const world = rowWorld(row, id);
      if (!world || (!chosen && !$('show-other').checked)) continue;
      const name = isPouch ? 'Ammo pouch' : 'Holster ' + id;
      const [x,y]=project(world), radius=(row.radius ?? 0)*G.SCALE;
      // Non-finite SVG geometry is never emitted, even for extreme but valid float32 state values.
      if (![x,y,radius].every(Number.isFinite)) continue;
      const g=svg('g',{class:`holster-volume${chosen ? ' selected' : ''}${row.enabled === false ? ' off' : ''}`, 'data-preview-slot':id});
      if (chosen && !isPouch && row.anchor !== 'auto') {
        const [ax,ay]=project(G.frames[row.anchor].origin);
        g.append(svg('path',{d:`M${ax} ${ay}L${x} ${y}`,class:'offset-line'}));
      }
      if (radius > 0) g.append(svg('circle',{cx:x,cy:y,r:radius,class:'grab-sphere'}));
      const centre=svg('g',{class:'holster-centre',role:'button',tabindex:'0','data-handle':'move','aria-label':`${name}. Drag to position; arrow keys move 1 centimetre, Shift 5.`});
      const title=svg('title',{});title.textContent=isPouch ? name : `${name} · ${labels[row.anchor]} · ${numberDisplay(row.radius)} cm radius`;centre.append(title);
      centre.append(svg('circle',{cx:x,cy:y,r:12})); const text=svg('text',{x,y});text.textContent=isPouch ? 'P' : String(id).padStart(2,'0');centre.append(text);
      centre.addEventListener('pointerdown',e=>beginDrag(e,id,'move'));
      centre.addEventListener('keydown',e=>keyboardMove(e,id));
      g.append(centre); $('slot-markers').append(g);
      if (chosen && !isPouch) {
        const h=svg('rect',{x:x+radius-6,y:y-6,width:12,height:12,class:'radius-handle',role:'button',tabindex:'0','data-handle':'radius','aria-label':'Grab radius. Drag to resize; arrow keys adjust 1 centimetre, Shift 5.'});
        h.addEventListener('pointerdown',e=>beginDrag(e,id,'radius'));
        h.addEventListener('keydown',e=>keyboardMove(e,id,'radius'));
        $('preview-handles').append(h);
      }
    }
    $('preview-measure').textContent = current?.radius != null ? `R ${numberDisplay(current.radius)} CM · Ø ${numberDisplay(current.radius*2)} CM` : 'RADIUS —';
    $('fit-preview').disabled = !current;
    $('preview-hint').textContent = current?.anchor === 'auto' ? 'Automatic · drag the saved position. TTWVR chooses the nearest bone in VR.' : 'Drag centre to move · square handle to resize. Use Side for depth. Arrow keys: 1 cm; Shift: 5 cm.';
    if (selected === pouchId) $('preview-hint').textContent = current ? 'Drag P to move the ammo pouch. Use Side for depth. Arrow keys move 1 cm, or 5 cm with Shift.' : '';
    const p=current && rowWorld(current, selected);
    if (p) {
      const [x,y]=project(p), box=$('body-map').viewBox.baseVal;
      if (x<box.x || y<box.y || x>box.x+box.width || y>box.y+box.height) $('preview-hint').textContent='Placement is outside this view. Use Fit selected to see it, then drag it closer.';
    }
  }
  function renderPreset() {
    const pipBoySide=$('preset-pipboy').value;
    const profile=P.list(pipBoySide).find(p=>p.id===presetId);
    $('preset-pipboy').disabled=!profile.usesForearm;
    const side=$('preset-side').value, forward=Number($('preset-forward').value);
    const placements=P.slots(presetId,side,forward,pipBoySide), metrics=P.metrics(placements);
    $('preset-name').textContent=`${profile.name} · ${profile.count} holsters`;
    $('preset-description').textContent=profile.description;
    $('preset-note').textContent=profile.note;
    $('preset-pairs').replaceChildren();
    profile.rows.forEach(row=>$('preset-pairs').append(element('li',{},`${row.ids.join(' / ')} · ${row.label} · ${row.radius} cm radius`)));
    for(const button of document.querySelectorAll('[data-preset]'))button.setAttribute('aria-pressed',String(button.dataset.preset===presetId));
    for(const direction of ['front','side']) {
      const drawing=$(direction==='front'?'preset-front':'preset-side-view');drawing.replaceChildren();
      const body=svg('g',{});drawing.append(body);drawBody(body,direction);
      const groups=new Map();
      for(const row of placements) {
        const [x,y]=G.project(G.world(row),direction);
        const key=direction==='side'?`${x.toFixed(5)},${y.toFixed(5)},${row.radius}`:String(row.id);
        if(groups.has(key))groups.get(key).ids.push(row.id);
        else groups.set(key,{x,y,radius:row.radius,ids:[row.id]});
      }
      for(const {x,y,radius,ids} of groups.values()) {
        const group=svg('g',{class:'holster-volume selected preset-volume','data-preset-slot':ids.join(',')});
        group.append(svg('circle',{cx:x,cy:y,r:radius*G.SCALE,class:'grab-sphere'}));
        const label=svg('g',{class:'holster-centre'});label.append(svg('circle',{cx:x,cy:y,r:ids.length>1?15:12}));
        const text=svg('text',{x,y});text.textContent=ids.length>1?ids.join('/'):String(ids[0]).padStart(2,'0');label.append(text);group.append(label);drawing.append(group);
      }
    }
    const radius=metrics.minRadius===metrics.maxRadius ? `${numberDisplay(metrics.minRadius)} cm` : `${numberDisplay(metrics.minRadius)}–${numberDisplay(metrics.maxRadius)} cm`;
    $('preset-metrics').textContent=`Radii ${radius} · nearest sphere gap ${(metrics.gap*100).toFixed(1)} cm in this pose`;
    $('apply-preset').disabled=!doc || !!errors.size;
    $('apply-preset').textContent=`Apply ${profile.name}`;
    $('preset-impact').textContent=!doc ? 'Load your TTWVR.state file before applying a layout.' : errors.size ? 'Close this preview and correct the highlighted value before applying.' : `Updates slots 1–${profile.count}.${profile.count<8 ? ` Disables ${profile.count+1}–8, retaining their positions.` : ''} One Undo restores your layout. Calibration and visibility stay unchanged.`;
  }
  for(const profile of P.list()) {
    const button=element('button',{type:'button','data-preset':profile.id,'aria-pressed':String(profile.id===presetId)});
    button.append(element('span',{class:'preset-card-title'},profile.name),element('span',{class:'preset-card-tag'},`${profile.tag} · ${profile.count} SLOTS`));
    button.addEventListener('click',()=>{presetId=profile.id;renderPreset();});$('preset-list').append(button);
  }
  $('open-presets').addEventListener('click',()=>{renderPreset();$('presets').showModal();});
  $('close-presets').addEventListener('click',()=>$('presets').close());
  $('preset-side').addEventListener('change',renderPreset);
  $('preset-forward').addEventListener('change',renderPreset);
  $('preset-pipboy').addEventListener('change',renderPreset);
  $('apply-preset').addEventListener('click',()=>{
    if(!doc || errors.size)return;
    const profile=P.list().find(p=>p.id===presetId);
    commit(m=>Object.assign(m,P.apply(m,presetId,$('preset-side').value,Number($('preset-forward').value),$('preset-pipboy').value)));
    $('presets').close();$('body-map').setAttribute('viewBox','0 0 440 540');
    selected=1;$('show-other').checked=true;render();
    notify(`${profile.name} applied: ${profile.count} enabled holsters. Undo restores your previous layout. Check the fit in VR before relying on it.`, 'success');
  });
  function mousePoint(e) {
    const p=new DOMPoint(e.clientX,e.clientY).matrixTransform($('body-map').getScreenCTM().inverse());
    return [p.x,p.y];
  }
  function beginDrag(e,id,kind) {
    if (e.button !== 0 || !doc || errors.size) return;
    e.preventDefault();
    if (id !== selected) { if (drafts.size) commit(()=>{}); selected=id; render(); }
    const row=previewSlot(); if (!row || !rowWorld(row, id)) return;
    drag={kind,point:mousePoint(e),start:H.clone(row),slot:H.clone(row),pointer:e.pointerId};
    $('body-map').setPointerCapture(e.pointerId);
  }
  function keyboardMove(e,id,kind='move') {
    const delta={ArrowLeft:[-1,0],ArrowRight:[1,0],ArrowUp:[0,1],ArrowDown:[0,-1]}[e.key];
    if (['Enter',' '].includes(e.key)) { e.preventDefault(); if (!errors.size) { if(drafts.size)commit(()=>{});selected=id;render(); } return; }
    if (!delta || errors.size || !doc) return;
    e.preventDefault();e.stopPropagation();
    if (selected!==id) { if(drafts.size)commit(()=>{});selected=id; }
    const step=e.shiftKey ? .05 : .01;
    commit(m=>{const row=activeRow(m, id); if(kind==='radius')row.radius=Math.max(.05,Math.min(.45,row.radius+(delta[0]+delta[1])*step)); else assignRow(m, id, moveRow(row, id, delta[0]*step, delta[1]*step));});
    const selector=kind==='radius' ? '.radius-handle' : `[data-preview-slot="${id}"] .holster-centre`;
    document.querySelector(selector)?.focus({preventScroll:true});
  }
  $('body-map').addEventListener('pointermove',e=>{
    if (!drag || e.pointerId!==drag.pointer) return;
    const p=mousePoint(e);
    if (drag.kind==='move') drag.slot=moveRow(drag.start,selected,(p[0]-drag.point[0])/G.SCALE,(drag.point[1]-p[1])/G.SCALE);
    else { const centre=project(rowWorld(drag.slot, selected)); drag.slot.radius=Math.max(.05,Math.min(.45,Math.hypot(p[0]-centre[0],p[1]-centre[1])/G.SCALE)); }
    const row = drag.slot;
    if (selected === pouchId) displayPouch(row);
    else {
      for (const [i,id] of ['position-f','position-r','position-u'].entries()) $(id).value = numberDisplay(row.position[i]);
      for(const [i,id] of ['offset-x','offset-y','offset-z'].entries())$(id).value=numberDisplay(row.offset[i]);
      $('radius').value=numberDisplay(row.radius);$('radius-slider').value=numberDisplay(row.radius);
    }
    renderPreview();
  });
  function endDrag(cancel=false) {
    if(!drag)return;
    const {slot,pointer}=drag;drag=null;
    if($('body-map').hasPointerCapture(pointer))$('body-map').releasePointerCapture(pointer);
    if(cancel)render();else commit(m => assignRow(m, selected, slot));
  }
  $('body-map').addEventListener('pointerup',()=>endDrag());
  $('body-map').addEventListener('pointercancel',()=>endDrag(true));
  $('body-map').addEventListener('lostpointercapture',()=>{if(drag)endDrag(true);});
  document.addEventListener('keydown',e=>{if(e.key==='Escape' && drag){e.preventDefault();endDrag(true);}});
  for(const v of ['front','side'])$(`view-${v}`).addEventListener('click',()=>{
    endDrag(true);view=v;$('body-map').setAttribute('viewBox','0 0 440 540');
    for(const choice of ['front','side'])$(`view-${choice}`).setAttribute('aria-pressed',String(v===choice));
    renderPreview();
  });
  $('show-other').addEventListener('change',renderPreview);
  $('fit-preview').addEventListener('click',()=>{
    const row=previewSlot(),p=row && rowWorld(row, selected);if(!p)return;
    const [x,y]=project(p), r=(row.radius ?? 0)*G.SCALE+25;
    const width=Math.max(440,2*r),height=width*540/440;
    $('body-map').setAttribute('viewBox',`${x-width/2} ${y-height/2} ${width} ${height}`);renderPreview();
  });
  $('mirror-target').addEventListener('change',e=>{mirrorTarget=Number(e.target.value);render();});
  $('mirror-slot').addEventListener('click',()=>{
    if(!doc || errors.size)return;
    commit(m=>{m.slots[mirrorTarget-1]=G.mirror(m.slots[selected-1],mirrorTarget);});
    notify(`Mirrored holster ${selected} to ${mirrorTarget} (${labels[model.slots[mirrorTarget-1].anchor]}). Undo restores the previous destination.`, 'success');
  });
  $('enabled').addEventListener('change', e => commit(m => { m.slots[selected - 1].enabled = e.target.checked; }));
  $('anchor').addEventListener('change', e => commit(m => { m.slots[selected - 1].anchor = e.target.value; }));
  $('show').addEventListener('change', e => commit(m => { m.show = e.target.checked; }));
  function numeric(id, mutate, min = -Infinity, max = Infinity) {
    const el = $(id);
    function valid(n) {
      const factor = pouchInputs.includes(id) && model?.pouch?.bone != null ? 70 : 1;
      return Number.isFinite(n) && n >= min && n <= max && Number.isFinite(Math.fround(n / 100 * factor));
    }
    numericFields.set(id, {mutate, min, max});
    el.addEventListener('input', () => {
      drafts.set(id, el.value);
      const n = el.valueAsNumber;
      if (!valid(n)) {
        errors.set(id, true); el.setAttribute('aria-invalid', 'true'); updateStatus();
      } else { errors.delete(id); el.removeAttribute('aria-invalid'); updateStatus(); }
      if (id === 'radius' && !errors.has(id)) $('radius-slider').value = el.value;
      renderPreview();
    });
    el.addEventListener('change', () => {
      const n = el.valueAsNumber;
      if (!valid(n)) {
        errors.set(id, true); el.setAttribute('aria-invalid', 'true'); updateStatus();
        notify(id === 'radius' ? 'Grab radius must be between 5 and 45 cm.' : 'Enter a finite number for the highlighted position.', 'error'); return;
      }
      drafts.set(id, el.value);
      commit(() => {});
    });
  }
  pouchInputs.forEach(id => numeric(id, null));
  numeric('radius', (s, v) => { s.radius = v; }, 5, 45);
  ['offset-x', 'offset-y', 'offset-z'].forEach((id, i) => numeric(id, (s, v) => { s.offset[i] = v; }));
  ['position-f', 'position-r', 'position-u'].forEach((id, i) => numeric(id, (s, v) => { s.position[i] = v; }));
  $('radius-slider').addEventListener('input', e => { $('radius').value = e.target.value; $('radius').dispatchEvent(new Event('input')); });
  $('radius-slider').addEventListener('change', e => commit(m => { m.slots[selected - 1].radius = Number(e.target.value) / 100; }));
  $('reset-slot').addEventListener('click', () => commit(m => assignRow(m, selected, H.clone(activeRow(doc.original, selected))), true));
  function travel(delta) {
    const next = historyIndex + delta;
    if (next < 0 || next >= history.length) return;
    clearErrors(); historyIndex = next; model = H.clone(history[historyIndex]); render(); notify(delta < 0 ? 'Change undone.' : 'Change restored.');
  }
  $('undo').addEventListener('click', () => travel(-1)); $('redo').addEventListener('click', () => travel(1));
  $('revert').addEventListener('click', () => { if (confirm('Revert all changes to the loaded file?')) commit(m => Object.assign(m, H.clone(doc.original)), true); });
  $('export').addEventListener('click', () => {
    if (!doc || demoMode || errors.size) return;
    if (drafts.size) { commit(() => {}); if (drafts.size) return; }
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
  $('repair-body').addEventListener('click', () => {
    if (!doc || errors.size) return;
    let count = 0;
    commit(m => { m.slots = m.slots.map(s => { const fixed = G.repairBodyPlacement(s); if (fixed !== s) count++; return fixed; }); });
    $('instructions').close(); $('view-side').click();
    notify(`Moved ${count} body holster${count === 1 ? '' : 's'} from behind to in front of the reference body. Arm placements are unchanged. Review, then export; Undo restores the layout.`, 'success');
  });
  $('help').addEventListener('click', () => $('instructions').showModal());
  $('close-help').addEventListener('click', () => $('instructions').close());
  $('instructions').addEventListener('click', e => { if (e.target === $('instructions')) {
    const r = $('instructions').getBoundingClientRect();
    if (e.clientX < r.left || e.clientX > r.right || e.clientY < r.top || e.clientY > r.bottom) $('instructions').close();
  } });
  $('editor-form').addEventListener('submit', e => e.preventDefault());
  document.addEventListener('keydown', e => {
    if (['INPUT', 'SELECT', 'TEXTAREA'].includes(e.target.tagName) || $('instructions').open || $('presets').open) return;
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') { e.preventDefault(); travel(e.shiftKey ? 1 : -1); }
    if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') { e.preventDefault(); travel(1); }
  });
  window.addEventListener('beforeunload', e => { if (!demoMode && (dirty() || errors.size)) { e.preventDefault(); e.returnValue = ''; } });
  render();
})();
