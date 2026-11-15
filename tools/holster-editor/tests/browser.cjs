// SPDX-License-Identifier: GPL-3.0-only
// Native Chromium DevTools protocol: no npm packages or public browser profile.
const fs = require('node:fs/promises');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {spawn} = require('node:child_process');
const assert = require('node:assert/strict');
const H = require('../src/state.js');
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
    const {root} = await cdp('DOM.getDocument'); const {nodeId} = await cdp('DOM.querySelector', {nodeId: root.nodeId, selector: '#file-input'});
    await cdp('DOM.setFileInputFiles', {nodeId, files: [file]});
  };
  const click = selector => evaluate(`document.querySelector(${JSON.stringify(selector)}).click()`);
  const change = (selector, value) => evaluate(`(() => { const e=document.querySelector(${JSON.stringify(selector)}); e.value=${JSON.stringify(value)}; e.dispatchEvent(new Event('input',{bubbles:true})); e.dispatchEvent(new Event('change',{bubbles:true})); })()`);
  const screenshot = async name => {
    if (!process.env.SCREENSHOT_DIR) return;
    await fs.mkdir(process.env.SCREENSHOT_DIR, {recursive: true});
    const {data} = await cdp('Page.captureScreenshot', {format: 'png', captureBeyondViewport: false}); await fs.writeFile(path.join(process.env.SCREENSHOT_DIR, name), Buffer.from(data, 'base64'));
  };
  try {
    const {targetId} = await send('Target.createTarget', {url: 'about:blank'}); ({sessionId: session} = await send('Target.attachToTarget', {targetId, flatten: true}));
    await cdp('Page.enable'); await cdp('Runtime.enable'); await cdp('Network.enable');
    await cdp('Emulation.setDeviceMetricsOverride', {width:1440,height:1050,deviceScaleFactor:1,mobile:false});
    await send('Browser.setDownloadBehavior', {behavior:'allow', downloadPath:downloads});
    await cdp('Page.navigate', {url:pathToFileURL(path.resolve(__dirname,'../index.html')).href});
    await waitFor('document.querySelectorAll(".slot").length === 8', 'initial UI');
    assert.equal(await evaluate('document.querySelector("#export").disabled'), true);
    await click('#demo'); assert.equal(await evaluate('document.querySelector("#export").disabled'), true);
    await upload(input); await waitFor('document.querySelector("#filename").textContent === "TTWVR.state"','state load');
    assert.equal(await evaluate('document.querySelector("#offset-x").value'), '10');
    await click('#export');
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    assert.deepEqual(await fs.readFile(path.join(downloads,'TTWVR.state')),await fs.readFile(input));
    await fs.unlink(path.join(downloads,'TTWVR.state'));
    await click('[data-slot="3"]');
    await evaluate('document.querySelector("[data-anchor=left_forearm]").dispatchEvent(new MouseEvent("click",{bubbles:true}))');
    assert.equal(await evaluate('document.querySelector("#anchor").value'),'left_forearm');
    await click('#enabled'); await change('#offset-x','12.3'); await change('#offset-y','-4.5'); await change('#radius','10');
    await click('#undo'); assert.equal(await evaluate('document.querySelector("#radius").value'),'6.5');
    await click('#redo'); assert.equal(await evaluate('document.querySelector("#radius").value'),'10');
    await change('#radius','0'); assert.equal(await evaluate('document.querySelector("#export").disabled'),true);
    await click('#enabled');
    assert.equal(await evaluate('document.querySelector("#radius").value'),'0');
    assert.equal(await evaluate('document.querySelector("#enabled").checked'),true);
    await change('#offset-z','2.4');
    assert.equal(await evaluate('document.querySelector("#export").disabled'),true);
    await change('#radius','10'); assert.equal(await evaluate('document.querySelector("#export").disabled'),false);
    await click('#show'); await click('#export');
    for(let i=0;i<100;i++){if((await fs.readdir(downloads)).includes('TTWVR.state'))break;await pause(50);}
    const exported = H.parse(await fs.readFile(path.join(downloads,'TTWVR.state')));
    assert.equal(exported.model.slots[2].anchor,'left_forearm'); assert.deepEqual(exported.model.slots[2].offset,[.123,-.045,.024]);
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
    console.log('PASS browser: local load/export, exact no-op bytes, map/keyboard anchors, cm conversion, enable/visibility, radius validation, undo/redo, reset, invalid-file retention, green/amber, help and responsive layout.');
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
