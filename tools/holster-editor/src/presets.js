/* SPDX-License-Identifier: GPL-3.0-only */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory(require('./geometry.js'));
  else root.HolsterPresets = factory(root.HolsterGeometry);
})(globalThis, G => {
  'use strict';
  // Reference targets in centimetres: right, back, up. Negative back = in front.
  // Each definition describes a right-side slot and its mirrored partner.
  // Limb and torso layouts are both available; all targets are placed forward.
  // No shin/ankle targets or automatic bindings are used.
  const upper = {anchor:'right_upper_arm', target:[31,-23,139], radius:8, label:'Front of upper arms'};
  const fore = {anchor:'right_forearm', target:[50,-23,129], radius:7, label:'Front of forearms'};
  const thigh = {anchor:'right_thigh', target:[18,-22,91], radius:8.5, label:'Front of upper thighs'};
  const chest = {anchor:'chest', target:[16,-26,130], radius:8.5, label:'In front of chest'};
  const belt = {anchor:'pelvis', target:[24,-26,99], radius:8.5, label:'In front of belt'};
  const lower = {anchor:'right_thigh', target:[20,-22,68], radius:7.5, label:'Front of lower thighs'};
  const definitions = [
    {id:'front_reach', name:'Front reach', tag:'START HERE', description:'Six targets in front of your upper arms, forearms and upper thighs. A compact layout without torso anchors.', note:'Start with this layout, then adjust the fit in VR.', pairs:[upper,fore,thigh]},
    {id:'upper_body', name:'Upper body', tag:'ARMS + TORSO', description:'Six forward targets carried by the upper arms, chest and pelvis. Torso holsters sit clear of the body rather than on the bone centres.', note:'Chest and belt targets follow torso rotation. Test them while turning and reaching.', pairs:[upper,{...chest,target:[14,-25,118],radius:7.5},{...belt,target:[22,-25,97]}]},
    {id:'torso_front', name:'Torso front', tag:'CHEST + BELT', description:'Four targets in front of the chest and belt, using chest and pelvis anchors. No arm or leg bindings.', note:'A direct comparison with limb layouts: the targets follow the torso, with extra space in front for your hands.', pairs:[chest,belt]},
    {id:'arms_only', name:'Arms only', tag:'NO LEG REACH', description:'Four targets carried by the arms. No bending down to reach leg holsters.', note:'Check the left forearm target with your wrist Pip-Boy open.', pairs:[upper,fore]},
    {id:'large_targets', name:'Large targets', tag:'WIDE SPACING', description:'Four larger grab spheres in front of your upper arms and upper thighs, with room between them.', note:'10.5 cm radii make these more forgiving to grab. Fine-tune if they catch your hands during normal movement.', pairs:[{...upper,target:[33,-28,140],radius:10.5},{...thigh,target:[20,-28,91],radius:10.5}]},
    {id:'thigh_kit', name:'Thigh kit', tag:'THIGH ANCHORS', description:'Two targets on the front of each thigh. All four follow thigh bones instead of the pelvis.', note:'The lower pair takes a longer reach; choose Arms only if you want to keep every target high.', pairs:[{...thigh,target:[16,-26,94],radius:8},{...lower,target:[27,-23,74],radius:7.5}]},
    {id:'full_kit', name:'Full kit', tag:'EXTRA CAPACITY', description:'Front reach plus a second pair lower on the thighs. Eight targets across arms and legs.', note:'Slots 7–8 are lower and may need a bend to reach. Start with Front reach if six slots are enough.', pairs:[upper,fore,thigh,lower]}
  ];
  function get(id) {
    const preset=definitions.find(p=>p.id===id);
    if(!preset)throw new Error('Unknown preset layout.');
    return preset;
  }
  const list = () => definitions.map(p=>({id:p.id,name:p.name,tag:p.tag,description:p.description,note:p.note,count:p.pairs.length*2,pairs:p.pairs.map(pair=>({label:pair.label,radius:pair.radius}))}));
  function slots(id, side='right', forwardCm=0) {
    if(!['right','left'].includes(side))throw new Error('Slot order must be right or left.');
    if(![0,5,10].includes(forwardCm))throw new Error('Choose 0, 5 or 10 cm of extra forward reach.');
    return get(id).pairs.flatMap((pair,i)=>{
      const point=pair.target.map(v=>v/100);point[1]-=forwardCm/100;
      const right={id:i*2+1,anchor:pair.anchor,offset:G.local(pair.anchor,point),radius:pair.radius/100,enabled:true};
      // Mirror through the verified bone frames. Fallback is only a placeholder
      // for this preview; apply() retains each real slot's existing fallback.
      right.position=[0,0,0];
      const left=G.mirror(right,i*2+2);
      if(side==='left') [right.id,left.id]=[left.id,right.id];
      return [right,left];
    }).sort((a,b)=>a.id-b.id);
  }
  function apply(model,id,side='right',forwardCm=0) {
    const placements=slots(id,side,forwardCm),next=JSON.parse(JSON.stringify(model));
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
