/* SPDX-License-Identifier: GPL-3.0-only */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory(require('./geometry.js'));
  else root.HolsterPresets = factory(root.HolsterGeometry);
})(globalThis, G => {
  'use strict';
  // Reference targets: right, back, up in cm. Negative back = forward.
  // Belt pieces follow a shallow arc around the waist. Torso pockets use chest
  // or pelvis frames; their offsets put the targets outside the reference body.
  const frontBelt = {anchor:'pelvis', target:[9.5,-19,100], radius:6, label:'Front belt pouches'};
  const hip = {anchor:'pelvis', target:[25,-7.5,100], radius:6.5, label:'Hip holsters'};
  const chest = {anchor:'chest', target:[12,-18.5,135], radius:6.5, label:'Chest pockets'};
  const lowerChest = {anchor:'chest', target:[10,-18.5,117], radius:6, label:'Lower vest pockets'};
  const shoulder = {anchor:'chest', target:[22,-16,132], radius:6.5, label:'Underarm holsters'};
  const thigh = {anchor:'right_thigh', target:[20,-13,80], radius:7, label:'Upper thigh holsters'};
  const centre = {anchor:'chest', target:[0,-21,117], radius:6, label:'Centre vest pouch', single:true};
  const forearm = {anchor:'free_forearm', target:[50,-18,129], radius:6, label:'Forearm utility', single:true};
  const definitions = [
    {id:'wasteland_rig', name:'Wasteland rig', tag:'BELT + VEST', description:'Four belt positions and four vest pockets fill the waist and torso. A gear-style starting layout with no arm-mounted holsters.', note:'Belt pouches sit across the front; hip holsters wrap toward the sides. Vest pockets fill two rows on the torso.', parts:[frontBelt,hip,chest,lowerChest]},
    {id:'gun_belt', name:'Gun belt', tag:'WAIST ONLY', description:'Two front pouches and a holster on each hip, arranged at one belt height.', note:'A compact belt layout. Use the side view to see the front pouches and hip holsters wrapping around the waist.', parts:[frontBelt,hip]},
    {id:'chest_rig', name:'Chest rig', tag:'CHEST + BELT', description:'A pair of chest pockets above a four-position belt. All six slots follow the torso and waist.', note:'Leaves the arms free and uses distinct chest, front-belt and hip locations.', parts:[frontBelt,hip,chest]},
    {id:'travel_vest', name:'Travel vest', tag:'TORSO POCKETS', description:'Four vest pockets across the chest and abdomen, with two front-belt pouches below.', note:'A pocket-style layout without side hip holsters. Check lower vest and belt reach in VR.', parts:[frontBelt,chest,lowerChest]},
    {id:'shoulder_rig', name:'Shoulder rig', tag:'UNDERARM + BELT', description:'Two underarm holsters carried by the chest frame, plus the four-position belt.', note:'Reach across to the underarm targets. They follow the torso rather than moving with the upper arms.', parts:[frontBelt,hip,shoulder]},
    {id:'field_rig', name:'Field rig', tag:'BELT + CHEST + THIGHS', description:'Four belt positions, two chest pockets and one upper-thigh holster on each leg.', note:'The thigh holsters sit below the belt. There is only one target on each leg.', parts:[frontBelt,hip,chest,thigh]},
    {id:'utility_rig', name:'Utility rig', tag:'BELT + VEST + UTILITY', description:'Four belt positions, two chest pockets, a centre vest pouch and one forearm utility target.', note:'The forearm target always goes opposite your selected Pip-Boy arm. Right is reserved by default, leaving the left forearm available.', parts:[frontBelt,hip,chest,centre,forearm]}
  ];
  function get(id) {
    const preset=definitions.find(p=>p.id===id);
    if(!preset)throw new Error('Unknown preset layout.');
    return preset;
  }
  function checkPipBoy(side) {
    if(!['right','left'].includes(side))throw new Error('Choose the arm carrying your Pip-Boy.');
  }
  function list(pipBoySide='right') {
    checkPipBoy(pipBoySide);
    return definitions.map(p=>{
      let nextId=1;
      const rows=p.parts.map(part=>{
        const ids=part.single?[nextId++]:[nextId++,nextId++];
        const label=part.anchor==='free_forearm'?`${pipBoySide==='right'?'Left':'Right'} forearm utility`:part.label;
        return {ids,label,radius:part.radius};
      });
      return {id:p.id,name:p.name,tag:p.tag,description:p.description,note:p.note,count:nextId-1,rows,usesForearm:p.parts.some(part=>part.anchor==='free_forearm')};
    });
  }
  function slots(id,side='right',forwardCm=0,pipBoySide='right') {
    if(!['right','left'].includes(side))throw new Error('Slot order must be right or left.');
    if(![0,5,10].includes(forwardCm))throw new Error('Choose 0, 5 or 10 cm of extra forward reach.');
    checkPipBoy(pipBoySide);
    let nextId=1;
    return get(id).parts.flatMap(part=>{
      const point=part.target.map(v=>v/100);point[1]-=forwardCm/100;
      let anchor=part.anchor;
      if(anchor==='free_forearm') {
        anchor=pipBoySide==='right'?'left_forearm':'right_forearm';
        if(anchor==='left_forearm')point[0]*=-1;
      }
      const first={id:nextId++,anchor,offset:G.local(anchor,point),radius:part.radius/100,enabled:true,position:[0,0,0]};
      if(part.single)return [first];
      const second=G.mirror(first,nextId++);
      if(side==='left')[first.id,second.id]=[second.id,first.id];
      return [first,second];
    }).sort((a,b)=>a.id-b.id);
  }
  function apply(model,id,side='right',forwardCm=0,pipBoySide='right') {
    const placements=slots(id,side,forwardCm,pipBoySide),next=JSON.parse(JSON.stringify(model));
    // Slots outside the chosen layout are disabled without erasing their placement.
    next.slots.forEach(s=>{s.enabled=false;});
    for(const placement of placements) {
      const target=next.slots[placement.id-1],position=target.position;
      Object.assign(target,placement,{position});
    }
    return next;
  }
  function metrics(placements) {
    let gap=Infinity;
    for(let i=0;i<placements.length;i++)for(let j=0;j<i;j++) {
      const a=G.world(placements[i]),b=G.world(placements[j]);
      gap=Math.min(gap,Math.hypot(...a.map((v,k)=>v-b[k]))-placements[i].radius-placements[j].radius);
    }
    return {gap,minRadius:Math.min(...placements.map(s=>s.radius)),maxRadius:Math.max(...placements.map(s=>s.radius))};
  }
  return {list,slots,apply,metrics};
});
