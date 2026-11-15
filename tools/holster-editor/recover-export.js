/* SPDX-License-Identifier: GPL-3.0-only */
/* Run once in the console of the existing Field Kit 1.1 tab, without reloading.
   Repairs export in memory, then downloads the current edits as TTWVR.state.
   No network access, disk writes to the game, or change to other tabs. */
(() => {
  const H = globalThis.HolsterState;
  const button = document.getElementById('export');
  if (!H?.serialize || !button) throw new Error('Run this in the open Field Kit editor tab.');
  if (button.disabled) throw new Error('Load your real state file and correct invalid fields first.');
  const save = H.serialize;
  H.serialize = (doc, model = doc.model) => {
    const copy = H.clone(model);
    copy.slots.forEach((slot, i) => {
      const old = doc.original.slots[i];
      for (const field of ['position', 'offset']) {
        slot[field] = slot[field].map((n, j) => n === old[field][j] ? n : Math.fround(n));
      }
      if (slot.radius !== old.radius) slot.radius = Math.fround(slot.radius);
    });
    return save(doc, copy);
  };
  button.click();
})();
