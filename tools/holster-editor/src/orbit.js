/* SPDX-License-Identifier: GPL-3.0-only */
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory(require('./geometry.js'));
  else root.HolsterOrbit = factory(root.HolsterGeometry);
})(globalThis, G => {
  'use strict';
  const axes = [
    {label:'R', name:'right', vector:[1,0,0]},
    {label:'F', name:'forward', vector:[0,-1,0]},
    {label:'U', name:'up', vector:[0,0,1]}
  ];
  const add = (a,b) => a.map((v,i)=>v+b[i]);
  const sub = (a,b) => a.map((v,i)=>v-b[i]);
  const times = (v,f) => v.map(x=>x*f);
  const dot = (a,b) => a.reduce((sum,v,i)=>sum+v*b[i],0);
  const unit = v => times(v,1/(Math.hypot(...v)||1));
  const cross = (a,b) => [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]];
  function camera(yaw=.62,pitch=.22) {
    pitch = Math.max(-1.25,Math.min(1.25,pitch));
    const c=Math.cos(yaw), s=Math.sin(yaw), h=Math.cos(pitch), v=Math.sin(pitch);
    return {yaw,pitch,right:[-c,-s,0],up:[-s*v,c*v,h],depth:[s*h,-c*h,v]};
  }
  function project(p,cam) {
    const centered=[p[0],p[1],p[2]-.95];
    return [220+G.SCALE*dot(centered,cam.right),490-G.SCALE*(.95+dot(centered,cam.up))];
  }
  // Project a pointer movement onto one world axis, independently of viewing angle.
  function axisDelta(axis,dx,dy,cam) {
    const x=dot(axis,cam.right), y=-dot(axis,cam.up), len2=x*x+y*y;
    if (len2 < .035) return [0,0,0];
    return times(axis,(dx*x+dy*y)/(G.SCALE*len2));
  }
  function planeDelta(dx,dy,cam) {
    return times(add(times(cam.right,dx),times(cam.up,-dy)),1/G.SCALE);
  }
  function move(row,isPouch,delta) {
    if (isPouch) return G.shiftPouch(row,delta);
    if (row.anchor==='auto') {
      const [forward,right,up]=row.position;
      return {...row, position:[forward-delta[1],right+delta[0],up+delta[2]]};
    }
    return {...row, offset:G.local(row.anchor,add(G.world(row),delta))};
  }

  // Build the reference skeleton once. All geometry is in the same metre-space
  // as the holsters. Closed rings give the bones volume from every camera angle.
  function skeleton() {
    const faces=[], j=G.joints;
    function shell(rings, tone=1) {
      const count=rings[0].length;
      for (let r=1;r<rings.length;r++) for(let i=0;i<count;i++) {
        const k=(i+1)%count;
        const points=[rings[r-1][i],rings[r-1][k],rings[r][k],rings[r][i]];
        const centre=times(points.reduce(add),.25);
        const normal=unit(cross(sub(points[1],points[0]),sub(points[3],points[0])));
        if (Math.hypot(...normal)>.5) faces.push({points,centre,normal,tone});
      }
    }
    function bone(a,b,radius) {
      const length=sub(b,a), axis=unit(length);
      const u=unit(cross(Math.abs(axis[2])>.9?[0,1,0]:[0,0,1],axis)), v=cross(axis,u);
      const rings=[[0,.22],[.09,1],[.45,.60],[.86,.76],[.97,1],[1,.18]].map(([h,r]) =>
        Array.from({length:6},(_,i)=>{
          const t=2*Math.PI*i/6;
          return add(add(a,times(length,h)),times(add(times(u,Math.cos(t)),times(v,Math.sin(t))),r*radius));
        }));
      shell(rings);
    }
    function oval(centre,dimensions,tone=1) {
      shell([-.99,-.72,-.28,.28,.72,.99].map(h=>Array.from({length:12},(_,i)=>{
        const t=2*Math.PI*i/12, r=Math.sqrt(1-h*h);
        return add(centre,[dimensions[0]*r*Math.cos(t),dimensions[1]*r*Math.sin(t),dimensions[2]*h]);
      })),tone);
    }
    const skull=add(j.head,[0,0,.055]);
    oval(skull,[.082,.097,.105]);
    oval(add(skull,[0,-.04,-.073]),[.064,.064,.045]);
    bone(add(skull,[0,-.075,.012]),add(skull,[0,-.108,-.038]),.017);
    bone(j.pelvis,j.neck,.031);
    for (let i=0;i<11;i++) {
      const vertebra=add(j.pelvis,times(sub(j.neck,j.pelvis),i/11));
      oval(vertebra,[.035,.033,.016]);
    }
    bone(j.neck,j.head,.034);
    for(const [side,sign] of [['right',1],['left',-1]]) {
      const at=k=>j[side+'_'+k];
      oval(add(skull,[sign*.037,-.080,.001]),[.024,.014,.020],.24);
      bone(add(skull,[sign*.014,-.094,.024]),add(skull,[sign*.061,-.063,.022]),.012);
      oval(add(j.pelvis,[sign*.071,0,0]),[.096,.091,.070]);
      bone(j.neck,at('upper_arm'),.025);
      bone(at('upper_arm'),at('forearm'),.036);
      bone(at('forearm'),at('hand'),.024);
      bone(add(at('forearm'),[0,.015,-.008]),add(at('hand'),[0,.022,0]),.015);
      oval(at('hand'),[.045,.027,.031]);
      for(let i=0;i<4;i++) {
        const knuckle=add(at('hand'),[sign*.027,(i-1.5)*.014,-.008]);
        const tip=add(knuckle,[sign*(.039-.006*Math.abs(i-1)),(i-1.5)*.004,-.020]);
        bone(knuckle,tip,.007);
      }
      bone(at('thigh'),at('shin'),.049);
      bone(at('shin'),at('foot'),.034);
      bone(at('foot'),at('toe'),.037);
      for(const [name,r] of [['upper_arm',.041],['forearm',.029],['thigh',.049],['shin',.043]])
        oval(at(name),[r,r,r]);
      // Ribs wrap from the spine to the sternum. Each arc is visible when orbited.
      for(let i=0;i<6;i++) {
        const z=j.chest[2]-.075+i*.036, width=.138+.033*Math.sin(i*Math.PI/6);
        const arc=[[0,.038,z],[sign*.62*width,.062,z+.015],[sign*width,0,z],
          [sign*.78*width,-.081,z-.023],[sign*.10,-.106,z-.028],[0,-.099,z-.008]];
        for(let k=1;k<arc.length;k++) bone(arc[k-1],arc[k],.011);
      }
    }
    return faces;
  }
  const faces=skeleton();
  function shadedFaces(cam) {
    const light=unit(add(cam.depth,add(times(cam.right,-.55),times(cam.up,.75))));
    return faces.filter(f=>dot(f.normal,cam.depth)>0).sort((a,b)=>dot(a.centre,cam.depth)-dot(b.centre,cam.depth)).map(f=>{
      const shade=Math.round((23+Math.max(0,dot(f.normal,light))*54)*f.tone);
      return {shade, path:'M'+f.points.map(p=>project(p,cam).map(v=>v.toFixed(2)).join(' ')).join('L')+'Z'};
    });
  }
  return {axes,camera,project,axisDelta,planeDelta,move,shadedFaces,dot,add,times};
});
