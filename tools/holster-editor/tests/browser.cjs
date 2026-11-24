// SPDX-License-Identifier: GPL-3.0-only
// Native Chromium DevTools protocol: no npm packages or public browser profile.
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {spawn} = require('node:child_process');
const assert = require('node:assert/strict');
const H = require('../src/state.js');
const P = require('../src/presets.js');
const G = require('../src/geometry.js');
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
(async () => {
  const chrome = process.env.CHROME_BIN || '/opt/google/chrome/chrome';
  const temp = await fs.mkdtemp(path.join(os.tmpdir(), 'ttwvr-editor-browser-'));
  const downloads = path.join(temp, 'downloads'); await fs.mkdir(downloads);
  const input = path.join(temp, 'TTWVR.state'); await fs.writeFile(input, H.demo());
  const badInput = path.join(temp, 'bad.state'); await fs.writeFile(badInput, '[Wrong]\nhello=world\n');
  const browser = spawn(chrome, ['--headless=new', '--disable-gpu', '--disable-background-networking', '--disable-extensions', '--no-first-run', '--no-default-browser-check', '--remote-debugging-pipe', `--user-data-dir=${path.join(temp, 'profile')}`], {stdio: ['ignore', 'ignore', 'pipe', 'pipe', 'pipe']});
  let stderr = ''; browser.stderr.on('data', b => { stderr = (stderr + b.toString()).slice(-10000); });
  let id = 0, buffer = '', session; const pending = new Map(), errors = [], requests = [];
  const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
    const current = ++id; const timer = setTimeout(() => { pending.delete(current); reject(new Error(`Timeout: ${method}\n${stderr}`)); }, 20000); timer.unref();
    pending.set(current, {resolve, reject, timer}); browser.stdio[3].write(JSON.stringify({id: current, method, params, ...(sessionId ? {sessionId} : {})}) + '\0');
  });
  browser.stdio[4].on('data', data => {
    buffer += data.toString(); let split;
    while ((split = buffer.indexOf('\0')) !== -1) {
      const value = buffer.slice(0, split); buffer = buffer.slice(split + 1); if (!value) continue;
      const m = JSON.parse(value);
      if (m.id && pending.has(m.id)) { const p = pending.get(m.id); pending.delete(m.id); clearTimeout(p.timer); m.error ? p.reject(new Error(JSON.stringify(m.error))) : p.resolve(m.result); }
      if (m.method === 'Runtime.exceptionThrown') errors.push(m.params.exceptionDetails);
      if (m.method === 'Network.requestWillBeSent') requests.push(m.params.request.url);
    }
  });
  browser.on('error', err => { for (const p of pending.values()) { clearTimeout(p.timer); p.reject(err); } pending.clear(); });
  const cdp = (method, params = {}) => send(method, params, session);
  const evaluate = async expression => {
    const r = await cdp('Runtime.evaluate', {expression, returnByValue: true, awaitPromise: true, userGesture: true});
    if (r.exceptionDetails) throw new Error(JSON.stringify(r.exceptionDetails)); return r.result.value;
  };
  const waitFor = async (expression, name) => {
    for (let i = 0; i < 100; i++) { if (await evaluate(expression)) return; await pause(50); } throw new Error(`Timed out waiting for ${name}`);
  };
  const upload = async file => {
    await evaluate("document.querySelector('#file-input').value = ''");
    const {root} = await cdp('DOM.getDocument'); const {nodeId} = await cdp('DOM.querySelector', {nodeId: root.nodeId, selector: '#file-input'});
    await cdp('DOM.setFileInputFiles', {nodeId, files: [file]});
  };
  const click = selector => evaluate(`document.querySelector(${JSON.stringify(selector)}).click()`);
  let lastExport = 0;
  async function clickExport() {
    // Chromium can throttle bursts of automatic downloads. Exercise the
    // actual button with a pointer and keep this fixture below ten per second.
    const delay = 150 - (Date.now() - lastExport);
    if (delay > 0) await pause(delay);
    const point = await evaluate(`(() => {
      const button = document.querySelector('#export');
      button.scrollIntoView({block:'nearest'});
      const r = button.getBoundingClientRect();
      return {x:r.x + r.width / 2, y:r.y + r.height / 2};
    })()`);
    await cdp('Input.dispatchMouseEvent', {type:'mouseMoved', ...point});
    await cdp('Input.dispatchMouseEvent', {type:'mousePressed', ...point, button:'left', clickCount:1});
    await cdp('Input.dispatchMouseEvent', {type:'mouseReleased', ...point, button:'left', clickCount:1});
    lastExport = Date.now();
  }
  const change = (selector, value) => evaluate(`(() => { const e=document.querySelector(${JSON.stringify(selector)}); e.value=${JSON.stringify(value)}; e.dispatchEvent(new Event('input',{bubbles:true})); e.dispatchEvent(new Event('change',{bubbles:true})); })()`);
  const screenshot = async name => {
    if (!process.env.SCREENSHOT_DIR) return;
    await fs.mkdir(process.env.SCREENSHOT_DIR, {recursive: true});
    const {data} = await cdp('Page.captureScreenshot', {format: 'png', captureBeyondViewport: false}); await fs.writeFile(path.join(process.env.SCREENSHOT_DIR, name), Buffer.from(data, 'base64'));
  };
  try {
    const {targetId} = await send('Target.createTarget', {url: 'about:blank'}); ({sessionId: session} = await send('Target.attachToTarget', {targetId, flatten: true}));
    await cdp('Page.enable'); await cdp('Runtime.enable'); await cdp('Network.enable');
    await cdp('Emulation.setDeviceMetricsOverride', {width:1440,height:1250,deviceScaleFactor:1,mobile:false});
    await send('Browser.setDownloadBehavior', {behavior:'allow', downloadPath:downloads});
    await cdp('Page.navigate', {url:pathToFileURL(process.env.LEGACY_EDITOR ? path.resolve(process.env.LEGACY_EDITOR) : path.resolve(__dirname,'../index.html')).href});
    await waitFor('document.querySelectorAll(".slot").length === 8', 'initial UI');
    assert.equal(await evaluate('document.querySelector("#export").disabled'), true);
    if(!process.env.LEGACY_EDITOR) {
      await click('#open-presets');
      assert.equal(await evaluate('document.querySelector("#presets").open'),true);
      assert.equal(await evaluate('document.querySelectorAll("[data-preset]").length'),7);
      assert.equal(await evaluate('document.querySelector("#apply-preset").disabled'),true);
      await click('#close-presets');
    }
    await click('#demo'); assert.equal(await evaluate('document.querySelector("#export").disabled'), true);
    await upload(input); await waitFor('document.querySelector("#filename").textContent === "TTWVR.state"','state load');
    assert.equal(await evaluate('document.querySelector("#offset-x").value'), '10');
    await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    assert.deepEqual(await fs.readFile(path.join(downloads,'TTWVR.state')),await fs.readFile(input));
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    // This drag-generated coordinate failed in 1.1's nine-digit exporter.
    await change('#offset-x','34.28074171588672');
    await change('#offset-y','-3.40234804687308');
    await change('#offset-z','13.368773126694206');
    await clickExport();
    if (process.env.LEGACY_EDITOR) {
      assert.match(await evaluate('document.querySelector("#notice").textContent'),/Export verification failed for a position value/);
      assert.equal((await fs.readdir(downloads)).includes('TTWVR.state'),false);
      // Repair the actual old app in place; never reload its document or lose edits.
      await evaluate(await fs.readFile(path.resolve(__dirname,'../recover-export.js'),'utf8'));
    }
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    const recovered=H.parse(await fs.readFile(path.join(downloads,'TTWVR.state'))).model;
    assert.equal(Math.fround(recovered.slots[0].offset[1]),Math.fround(-.0340234804687308));
    assert.deepEqual(recovered.slots.slice(1),H.parse(H.demo()).model.slots.slice(1));
    assert.deepEqual(recovered.slots[0].position,H.parse(H.demo()).model.slots[0].position);
    assert.equal(await evaluate('document.querySelector("#offset-y").value'),'-3.40235');
    assert.deepEqual(await fs.readFile(input),Buffer.from(H.demo()));
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    if(process.env.LEGACY_EDITOR) {
      assert.equal(errors.length,0,JSON.stringify(errors));
      assert.equal(requests.some(u=>/^https?:/.test(u)),false,JSON.stringify(requests));
      console.log('PASS old Field Kit 1.1 failure reproduced; console repair exports existing edits without reload, source changes or network requests.');
      return;
    }
    await upload(input);await waitFor('document.querySelector("#offset-x").value === "10"','reload after precision regression');
    // Repair an old body layout through the real UI; load alone must not alter it.
    const legacyDoc=H.parse(H.demo()),legacyModel=H.clone(legacyDoc.model);
    Object.assign(legacyModel.slots[3],{anchor:'pelvis',offset:[.031845,-.2493891,-.095]});
    const legacyInput=path.join(temp,'old-placement.state'),legacyBytes=H.serialize(legacyDoc,legacyModel);
    await fs.writeFile(legacyInput,legacyBytes);await upload(legacyInput);
    await waitFor('document.querySelector("#filename").textContent === "old-placement.state"','old layout load');
    await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    assert.deepEqual(await fs.readFile(path.join(downloads,'TTWVR.state')),Buffer.from(legacyBytes));
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('#help');assert.equal(await evaluate('document.querySelector("#repair-body").disabled'),false);
    await click('#repair-body');assert.equal(await evaluate('document.querySelector("#instructions").open'),false);
    assert.equal(await evaluate('document.querySelector("#view-side").getAttribute("aria-pressed")'),'true');
    assert.equal(await evaluate('document.querySelector("#repair-body").disabled'),true);
    await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    const repaired=H.parse(await fs.readFile(path.join(downloads,'TTWVR.state'))).model;
    assert.deepEqual(repaired,{...legacyModel,slots:legacyModel.slots.map(G.repairBodyPlacement)});
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('#undo');await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    assert.deepEqual(await fs.readFile(path.join(downloads,'TTWVR.state')),Buffer.from(legacyBytes));
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('#redo');assert.equal(await evaluate('document.querySelector("#repair-body").disabled'),true);
    assert.deepEqual(await fs.readFile(legacyInput),Buffer.from(legacyBytes));
    await upload(input);await waitFor('document.querySelector("#filename").textContent === "TTWVR.state"','reload after body repair');
    await click('#view-front');
    // Presets are preview-only until applied, and each application is one undo step.
    await click('#open-presets');await click('[data-preset="field_rig"]');
    assert.equal(await evaluate('document.querySelectorAll("#preset-front [data-preset-slot]").length'),8);
    await change('#preset-side','left');await change('#preset-forward','10');
    await click('#close-presets');await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    assert.deepEqual(await fs.readFile(path.join(downloads,'TTWVR.state')),await fs.readFile(input));
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('#open-presets');await click('[data-preset="wasteland_rig"]');
    await change('#preset-side','right');await change('#preset-forward','0');
    await screenshot('holster-editor-presets-library.png');
    await click('[data-preset="gun_belt"]');await screenshot('holster-editor-gun-belt.png');
    await click('[data-preset="utility_rig"]');
    assert.equal(await evaluate('document.querySelectorAll("[data-preset-pouch]").length'),2);
    await change('#preset-pouch-side','right');
    const presetPouchX = await evaluate('Number(document.querySelector("#preset-front [data-preset-pouch] .grab-sphere").getAttribute("cx"))');
    assert.ok(presetPouchX < 220);

    assert.equal(await evaluate('document.querySelector("#preset-pipboy").value'),'right');
    assert.match(await evaluate('document.querySelector("#preset-pairs").textContent'),/8 · Left forearm utility/);
    await change('#preset-pipboy','left');
    assert.match(await evaluate('document.querySelector("#preset-pairs").textContent'),/8 · Right forearm utility/);
    await change('#preset-pipboy','right');
    assert.equal(await evaluate('document.querySelectorAll("#preset-side-view [data-preset-slot]").length'),5);
    await change('#preset-side','left');await change('#preset-forward','5');
    assert.match(await evaluate('document.querySelector("#preset-pairs").textContent'),/8 · Left forearm utility/);
    await screenshot('holster-editor-presets.png');
    await cdp('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
    assert.equal(await evaluate('document.querySelector("#presets").scrollWidth <= document.querySelector("#presets").clientWidth'),true);
    await screenshot('holster-editor-presets-mobile.png');
    await cdp('Emulation.setDeviceMetricsOverride',{width:1440,height:1250,deviceScaleFactor:1,mobile:false});
    await click('#apply-preset');
    assert.equal(await evaluate('document.querySelector("#presets").open'),false);
    assert.equal(await evaluate('document.querySelector("#anchor").value'),'pelvis');
    assert.equal(await evaluate('document.querySelector("#enabled-count").textContent'),'8 / 8');
    await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    const presetExport=H.parse(await fs.readFile(path.join(downloads,'TTWVR.state'))).model;
    assert.deepEqual(presetExport,P.apply(H.parse(H.demo()).model,'utility_rig','left',5,'right',{pouchSide:'right',eyeHeight:H.parse(H.demo()).eyeHeight}));
    assert.equal(presetExport.slots[7].anchor,'left_forearm');
    assert.equal(presetExport.slots.some(s=>s.anchor==='right_forearm'),false);
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('#undo');await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    assert.deepEqual(await fs.readFile(path.join(downloads,'TTWVR.state')),await fs.readFile(input));
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('#redo');assert.equal(await evaluate('document.querySelector("#anchor").value'),'pelvis');
    await click('#undo');
    // Live preview: real mouse drags, physical dimensions, and one undo per gesture.
    const currentWorld = () => evaluate(`HolsterGeometry.world({anchor:document.querySelector('#anchor').value,offset:['x','y','z'].map(a=>Number(document.querySelector('#offset-'+a).value)/100)})`);
    const near = (a,b,eps=1e-6) => a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<eps, `${a} != ${b}`));
    const dragHandle = async (selector,dx,dy) => {
      const p=await evaluate(`(()=>{const e=document.querySelector(${JSON.stringify(selector)}),r=e.getBoundingClientRect();const s=document.querySelector('#body-map').getScreenCTM().a;return {x:r.x+r.width/2,y:r.y+r.height/2,scale:s};})()`);
      await cdp('Input.dispatchMouseEvent',{type:'mouseMoved',x:p.x,y:p.y});
      await cdp('Input.dispatchMouseEvent',{type:'mousePressed',x:p.x,y:p.y,button:'left',clickCount:1});
      await cdp('Input.dispatchMouseEvent',{type:'mouseMoved',x:p.x+dx,y:p.y+dy,button:'left',buttons:1});
      await cdp('Input.dispatchMouseEvent',{type:'mouseReleased',x:p.x+dx,y:p.y+dy,button:'left',clickCount:1});return p.scale;
    };
    // Move equipment through the same front/side handles used by players and
    // re-read the actual download to catch preview-only edits and unit errors.
    const marker = id => `[data-preview-slot="${id}"] .holster-centre`;
    const readCm = async ids => Promise.all(ids.map(id => evaluate(`Number(document.getElementById('${id}').value)/100`)));
    const downloadState = async () => {
      await clickExport();
      const file = path.join(downloads, 'TTWVR.state');
      for (let i = 0; i < 100 && !(await fs.readdir(downloads)).includes('TTWVR.state'); i++) await pause(50);
      const result = H.parse(await fs.readFile(file));
      await fs.unlink(file);
      return result;
    };
    const original = H.parse(H.demo());
    await click('#select-pouch');
    const pouchStart = G.pouchWorld(original.model.pouch);
    await click('#view-orbit');
    const skeletonPath=()=>evaluate('document.querySelector("#reference-body .orbit-bone").getAttribute("d")');
    const beforeOrbit=await skeletonPath();
    const outlineVisible=()=>evaluate("getComputedStyle(document.getElementById('skin-surface')).display !== 'none' && getComputedStyle(document.querySelector('.skin-fill')).fillOpacity < 1");
    const outlinePath=()=>evaluate('document.getElementById("skin-shape").getAttribute("d")');
    const beforeSkin=await outlinePath();
    assert.ok(await outlineVisible());
    assert.ok(beforeSkin.length>1000);
    assert.equal(await evaluate("getComputedStyle(document.querySelector('.skin-edge')).pointerEvents"),'none');
    // Check front/side surface coverage in the actual SVG fill. The gap between
    // the legs is open, and in profile the belly has depth around the spine.
    assert.deepEqual(await evaluate(`(()=>{
      const O=HolsterOrbit, path=document.createElementNS('http://www.w3.org/2000/svg','path');
      path.setAttribute('opacity','0');document.getElementById('body-map').append(path);
      const coverage=[[0,[[0,0,1.1],[.14,0,1.3],[.29,0,1.1],[0,0,.5],[.10,0,.50]]],
        [Math.PI/2,[[0,-.08,1.1],[0,-.24,1.1],[0,.22,1.1]]]].flatMap(([yaw,points])=>{
        const c=O.camera(yaw,0);path.setAttribute('d',O.skinPath(c));
        return points.map(p=>path.isPointInFill(new DOMPoint(...O.project(p,c))));
      });
      path.remove();return coverage;
    })()`),[true,true,false,false,true,true,false,false]);
    await click('#show-skin');assert.equal(await outlineVisible(),false);
    await click('#view-side');
    assert.equal(await evaluate("document.getElementById('skin-control').hidden"),true);
    await click('#view-orbit');assert.equal(await outlineVisible(),false);
    await click('#show-skin');assert.ok(await outlineVisible());
    assert.deepEqual((await downloadState()).bytes,original.bytes);

    // Orbit over clear background, then drag each constrained axis with real browser input.
    const corner=await evaluate(`(()=>{const r=document.querySelector('#body-map').getBoundingClientRect();return {x:r.left+20,y:r.top+45};})()`);
    await cdp('Input.dispatchMouseEvent',{type:'mousePressed',...corner,button:'left',clickCount:1});
    await cdp('Input.dispatchMouseEvent',{type:'mouseMoved',x:corner.x+24,y:corner.y+10,button:'left',buttons:1});
    await cdp('Input.dispatchMouseEvent',{type:'mouseReleased',x:corner.x+24,y:corner.y+10,button:'left',clickCount:1});
    assert.notEqual(await skeletonPath(),beforeOrbit);assert.notEqual(await outlinePath(),beforeSkin);
    assert.equal(await evaluate('document.querySelector("#undo").disabled'),true);
    await click('#orbit-reset');assert.equal(await skeletonPath(),beforeOrbit);assert.equal(await outlinePath(),beforeSkin);
    await screenshot('holster-editor-3d.png');
    for(const [axis,index,dx,dy] of [['right',0,-20,2],['forward',1,14,4],['up',2,0,-18]]) {
      await dragHandle('[data-axis="'+axis+'"] .axis-hit',dx,dy);
      const changed=G.pouchWorld((await downloadState()).model.pouch);
      assert.ok(Math.abs(changed[index]-pouchStart[index])>.025,axis+' gizmo did not move');
      near(changed.filter((_,i)=>i!==index),pouchStart.filter((_,i)=>i!==index));
      await click('#undo');
    }
    await click('[data-slot="1"]');
    const oldSlot=G.world(original.model.slots[0]);
    await dragHandle('[data-axis="up"] .axis-hit',0,-20);
    const newSlot=G.world((await downloadState()).model.slots[0]);
    near(newSlot.slice(0,2),oldSlot.slice(0,2));assert.ok(newSlot[2]>oldSlot[2]+.03);
    await click('#undo'); await click('#view-front'); await click('#select-pouch');
    const dragScale = await dragHandle(marker('pouch'), 19, -12);
    const right = pouchStart[0] - 19 / (230 * dragScale), height = pouchStart[2] + 12 / (230 * dragScale);
    near(await readCm(['pouch-right','pouch-height']), [right, height]);
    await click('#view-side');
    const sideScale = await dragHandle(marker('pouch'), 16, 0);
    const back = pouchStart[1] - 16 / (230 * sideScale);
    await change('#pouch-height', '127');
    let saved = await downloadState();
    near(G.pouchWorld(saved.model.pouch), [right, back, 1.27]);
    assert.deepEqual(saved.model.slots, original.model.slots);
    await evaluate(`document.querySelector('[data-anchor="0"]').dispatchEvent(new MouseEvent("click",{bubbles:true}))`);
    assert.equal(await evaluate('document.getElementById("pouch-anchor").value'), '0');
    await change('#pouch-anchor', '1');
    await change('#pouch-radius', '23');
    saved = await downloadState();
    assert.equal(saved.model.pouch.bone, 1);
    assert.equal(saved.model.pouch.radius, .23);
    near(G.pouchWorld(saved.model.pouch), [right, back, 1.27]);
    assert.deepEqual(saved.model.slots, original.model.slots);
    const sphereSize = () => evaluate(`Number(document.querySelector('[data-preview-slot="pouch"] .grab-sphere').getAttribute("r"))`);
    assert.ok(Math.abs(await sphereSize() - .23 * G.SCALE) < .001);
    await dragHandle('.radius-handle', 13, 0);
    saved = await downloadState();
    assert.ok(saved.model.pouch.radius > .23);
    await change('#pouch-radius-slider', '18');
    await change('#pouch-anchor', 'body');
    saved = await downloadState();
    assert.equal(saved.model.pouch.bone, null);
    assert.equal(saved.model.pouch.radius, .18);
    near(G.pouchWorld(saved.model.pouch), [right, back, 1.27]);
    await click('#pouch-default-radius');
    assert.equal((await downloadState()).refs.pouch.radius, undefined);
    await click('#undo');
    assert.equal((await downloadState()).model.pouch.radius, .18);
    await click('#reset-slot');
    assert.deepEqual((await downloadState()).bytes, original.bytes);
    await click('[data-slot="3"]');
    assert.equal(await evaluate('document.querySelector("#fit-preview").disabled'), false);
    const autoScale = await dragHandle(marker(3), 23, -12);
    saved = await downloadState();
    assert.equal(saved.model.slots[2].anchor, 'auto');
    near(saved.model.slots[2].position, [.25 + 23/(230*autoScale), .075, -.35 + 12/(230*autoScale)]);
    assert.deepEqual(saved.model.pouch, original.model.pouch);
    await click('#undo');
    assert.deepEqual((await downloadState()).bytes, original.bytes);
    await click('#view-front');
    await click('[data-slot="1"]');
    const originalWorld=await currentWorld();
    let scale=await dragHandle('[data-preview-slot="1"] .holster-centre',35,18);
    near(await currentWorld(),[originalWorld[0]-35/(230*scale),originalWorld[1],originalWorld[2]-18/(230*scale)],.00001);
    await click('#undo');near(await currentWorld(),originalWorld);
    await click('#view-side');
    scale=await dragHandle('[data-preview-slot="1"] .holster-centre',30,-15);
    near(await currentWorld(),[originalWorld[0],originalWorld[1]-30/(230*scale),originalWorld[2]+15/(230*scale)],.00001);
    assert.equal(await evaluate('document.querySelectorAll("input:invalid").length'),0);
    await screenshot('holster-editor-side.png');
    await click('#undo');near(await currentWorld(),originalWorld);
    scale=await dragHandle('.radius-handle',25,0);
    assert.ok(Math.abs(Number(await evaluate('document.querySelector("#radius").value'))/100-(.065+25/(230*scale)))<.00001);
    await click('#undo');assert.equal(await evaluate('document.querySelector("#radius").value'),'6.5');
    await click('#view-front');
    await evaluate(`document.querySelector('[data-preview-slot="1"] .holster-centre').focus()`);
    await cdp('Input.dispatchKeyEvent',{type:'keyDown',key:'ArrowUp',code:'ArrowUp',modifiers:8});
    await cdp('Input.dispatchKeyEvent',{type:'keyUp',key:'ArrowUp',code:'ArrowUp',modifiers:8});
    near(await currentWorld(),[originalWorld[0],originalWorld[1],originalWorld[2]+.05]);
    await click('#undo');near(await currentWorld(),originalWorld);
    const dragPoint=await evaluate(`(()=>{const r=document.querySelector('[data-preview-slot="1"] .holster-centre').getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2};})()`);
    await cdp('Input.dispatchMouseEvent',{type:'mousePressed',...dragPoint,button:'left',clickCount:1});
    await cdp('Input.dispatchMouseEvent',{type:'mouseMoved',x:dragPoint.x+40,y:dragPoint.y,button:'left',buttons:1});
    await cdp('Input.dispatchKeyEvent',{type:'keyDown',key:'Escape',code:'Escape'});
    await cdp('Input.dispatchKeyEvent',{type:'keyUp',key:'Escape',code:'Escape'});
    await cdp('Input.dispatchMouseEvent',{type:'mouseReleased',x:dragPoint.x+40,y:dragPoint.y,button:'left',clickCount:1});
    near(await currentWorld(),originalWorld);
    await evaluate(`(()=>{const e=document.querySelector('#radius');e.value='20.4';e.dispatchEvent(new Event('input',{bubbles:true}));})()`);
    assert.equal(await evaluate(`Number(document.querySelector('[data-preview-slot="1"] .grab-sphere').getAttribute('r'))`),.204*230);
    await change('#radius','20.4');await change('#offset-x','16.79');await change('#offset-y','20');await change('#offset-z','8.62');
    await change('#mirror-target','2');await click('#mirror-slot');
    await click('[data-slot="2"]');
    assert.equal(await evaluate('document.querySelector("#anchor").value'),'left_upper_arm');
    assert.equal(await evaluate('document.querySelector("#offset-z").value'),'-8.62');
    assert.equal(await evaluate('document.querySelector("#radius").value'),'20.4');
    await click('#undo');assert.equal(await evaluate('document.querySelector("#offset-z").value'),'0');
    await click('#redo');assert.equal(await evaluate('document.querySelector("#offset-z").value'),'-8.62');
    await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    const mirrored=H.parse(await fs.readFile(path.join(downloads,'TTWVR.state')));
    assert.deepEqual(mirrored.model.slots[1].offset,[.1679,.2,-.0862]);assert.deepEqual(mirrored.model.slots[0].offset,[.1679,.2,.0862]);
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('#amber');await screenshot('holster-editor-mirror.png');await click('#green');
    await upload(input);await waitFor('document.querySelector("#radius").value === "6.5"','reset after mirror');
    await click('[data-slot="3"]');
    await evaluate('document.querySelector("[data-anchor=left_forearm]").dispatchEvent(new MouseEvent("click",{bubbles:true}))');
    assert.equal(await evaluate('document.querySelector("#anchor").value'),'left_forearm');
    await click('#enabled'); await change('#offset-x','12.3'); await change('#offset-y','-4.5'); await change('#radius','10');
    await click('#undo'); assert.equal(await evaluate('document.querySelector("#radius").value'),'6.5');
    await click('#redo'); assert.equal(await evaluate('document.querySelector("#radius").value'),'10');
    await change('#radius','0'); assert.equal(await evaluate('document.querySelector("#export").disabled'),true);
    assert.equal(await evaluate('document.querySelector("#mirror-slot").disabled'),true);
    assert.equal(await evaluate('document.querySelector("#repair-body").disabled'),true);
    await click('#open-presets');assert.equal(await evaluate('document.querySelector("#apply-preset").disabled'),true);await click('#close-presets');
    await click('#enabled');
    assert.equal(await evaluate('document.querySelector("#radius").value'),'0');
    assert.equal(await evaluate('document.querySelector("#enabled").checked'),true);
    await change('#offset-z','2.4');
    assert.equal(await evaluate('document.querySelector("#export").disabled'),true);
    await change('#radius','10'); assert.equal(await evaluate('document.querySelector("#export").disabled'),false);
    await click('#show'); await clickExport();
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    const exported = H.parse(await fs.readFile(path.join(downloads,'TTWVR.state')));
    assert.equal(exported.model.slots[2].anchor,'left_forearm'); assert.deepEqual(exported.model.slots[2].offset.map(Math.fround),[.123,-.045,.024].map(Math.fround));
    assert.equal(exported.model.slots[2].radius,.1);assert.equal(exported.model.slots[2].enabled,true);assert.equal(exported.model.show,false);
    assert.deepEqual(await fs.readFile(input), Buffer.from(H.demo()));
    await upload(badInput); await waitFor('document.querySelector("#notice").classList.contains("error")','invalid-file message');
    assert.equal(await evaluate('document.querySelector("#filename").textContent'),'TTWVR.state');assert.equal(await evaluate('document.querySelector("#anchor").value'),'left_forearm');
    // Reload a known state for presentation and keyboard testing.
    await upload(input); await waitFor('document.querySelector("#anchor").value === "right_upper_arm"','reload');
    await click('[data-slot="4"]');
    await screenshot('holster-editor-green.png');
    await click('#amber'); assert.equal(await evaluate('document.documentElement.dataset.theme'),'amber'); await screenshot('holster-editor-amber.png');
    await click('#help'); assert.equal(await evaluate('document.querySelector("#instructions").open'),true); await click('#close-help');
    await evaluate('document.querySelector("[data-anchor=right_thigh]").dispatchEvent(new KeyboardEvent("keydown",{key:"Enter",bubbles:true}))');
    assert.equal(await evaluate('document.querySelector("#anchor").value'),'right_thigh');
    await click('#reset-slot');assert.equal(await evaluate('document.querySelector("#anchor").value'),'pelvis');
    await cdp('Emulation.setDeviceMetricsOverride', {width:390,height:844,deviceScaleFactor:1,mobile:true});
    assert.equal(await evaluate('document.documentElement.scrollWidth <= window.innerWidth'),true);await screenshot('holster-editor-mobile.png');
    await cdp('Emulation.setDeviceMetricsOverride', {width:1024,height:768,deviceScaleFactor:1,mobile:false});
    assert.equal(await evaluate('document.documentElement.scrollWidth <= window.innerWidth'),true);
    assert.equal(errors.length,0,JSON.stringify(errors));assert.equal(requests.some(u=>/^https?:/.test(u)),false,JSON.stringify(requests));
    console.log('PASS browser: translucent silhouette, toggle, state preservation, 3D orbit and gizmo drag/export/undo, preset ammo pouch previews, ammo pouch and automatic holster drag/export/reset, body repair/export/undo/redo, preset preview/apply/undo/export and mobile chooser, local load/export, exact no-op bytes, front/side mouse drags, live sphere radius, mirror export/undo, map/keyboard anchors, cm conversion, enable/visibility, radius validation, undo/redo, reset, invalid-file retention, green/amber, help and responsive layout.');
    console.log('PASS no network requests; source file unchanged; no browser exceptions.');
  } finally {
    try { await send('Browser.close'); } catch {}
    browser.stdio[3].end();
    for(let i=0;i<40 && browser.exitCode===null;i++)await pause(50);
    if(browser.exitCode===null)browser.kill('SIGTERM');
    for(const p of pending.values())clearTimeout(p.timer);
    await fs.rm(temp,{recursive:true,force:true});
  }
})().catch(e=>{console.error(e);process.exitCode=1;});
