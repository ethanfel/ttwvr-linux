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
  // Each part is a series of elliptical cross-sections in the skeleton's metre space.
  // The outside of their combined projection stays continuous across joints.
  function bodySections() {
    const j=G.joints, parts=[];
    const circle=Array.from({length:24},(_,i)=>{
      const a=2*Math.PI*i/24;
      return [Math.cos(a),Math.sin(a)];
    });
    function sections(rows,u,v) {
      parts.push(rows.map(([centre,ru,rv])=>circle.map(([c,s])=>
        add(centre,add(times(u,ru*c),times(v,rv*s))))));
    }
    function upright(origin,rows) {
      sections(rows.map(([z,back,width,depth])=>[add(origin,[0,back,z]),width,depth]),[1,0,0],[0,1,0]);
    }
    function limb(a,b,rows) {
      const span=sub(b,a), axis=unit(span);
      const back=unit(sub([0,1,0],times(axis,axis[1])));
      const across=cross(axis,back);
      sections(rows.map(([t,depth,width])=>[add(a,times(span,t)),depth,width]),back,across);
    }
    upright(j.pelvis,[
      [-.11,.008,.08,.066],[-.065,.017,.147,.106],[.005,.020,.177,.124],
      [.085,.012,.159,.115],[.15,.006,.15,.108],[.23,0,.17,.12],
      [.34,-.002,.195,.132],[.43,.010,.208,.12],[.49,.027,.189,.09],[.55,.023,.063,.062]
    ]);
    upright(j.neck,[[-.041,.001,.073,.070],[.012,0,.058,.056],[.088,-.007,.056,.060]]);
    upright(j.head,[
      [-.041,-.043,.037,.051],[-.012,-.029,.066,.078],[.023,-.012,.084,.093],
      [.069,0,.092,.104],[.114,.005,.088,.099],[.15,.009,.065,.077],[.166,.01,.028,.033]
    ]);
    upright(j.head,[[.014,-.093,.014,.020],[.030,-.103,.017,.024],[.065,-.094,.010,.012]]);
    for(const [side,sign] of [['right',1],['left',-1]]) {
      const at=name=>j[side+'_'+name];
      limb(at('upper_arm'),at('forearm'),[
        [-.19,.045,.049],[.04,.073,.072],[.32,.063,.060],[.59,.052,.054],[.90,.043,.042],[1.06,.039,.04]
      ]);
      limb(at('forearm'),at('hand'),[
        [-.09,.04,.04],[.12,.05,.047],[.36,.047,.045],[.69,.034,.034],[1.02,.027,.026]
      ]);
      const hand=at('hand');
      limb(hand,add(hand,[sign*.083,-.006,-.032]),[
        [-.18,.025,.025],[.24,.041,.027],[.55,.038,.024],[.90,.029,.018],[1.03,.013,.012]
      ]);
      limb(add(hand,[sign*.009,-.025,0]),add(hand,[sign*.052,-.055,-.023]),[
        [0,.019,.016],[.51,.016,.014],[1,.009,.010]
      ]);
      limb(at('thigh'),at('shin'),[
        [-.10,.079,.076],[.09,.112,.097],[.31,.104,.089],[.61,.083,.072],[.83,.063,.056],[1.05,.056,.055]
      ]);
      limb(at('shin'),at('foot'),[
        [-.08,.050,.052],[.12,.060,.055],[.32,.070,.057],[.50,.060,.051],[.76,.038,.035],[1.04,.034,.032]
      ]);
      const ankle=at('foot'), toe=at('toe');
      const sole=[(ankle[0]+toe[0])/2,(ankle[1]+toe[1])/2,0];
      upright(sole,[[.004,0,.047,.113],[.021,-.002,.052,.124],[.043,.002,.050,.118],
        [.067,.024,.043,.089],[.092,.050,.035,.051],[.114,.065,.030,.033]]);
    }
    return parts;
  }
  const skin=bodySections();
  function convexOutline(points) {
    const sorted=points.sort((a,b)=>a[0]-b[0] || a[1]-b[1]);
    function chain(sequence) {
      const hull=[];
      for(const p of sequence) {
        while(hull.length>=2) {
          const a=hull[hull.length-2], b=hull[hull.length-1];
          if((b[0]-a[0])*(p[1]-a[1])>(b[1]-a[1])*(p[0]-a[0])) break;
          hull.pop();
        }
        hull.push(p);
      }
      hull.pop();
      return hull;
    }
    const lower=chain(sorted);
    return lower.concat(chain(sorted.reverse()));
  }
  function skinPath(cam) {
    const polygons=[];
    for(const part of skin) {
      let last=null;
      for(const ring of part) {
        const current=ring.map(p=>project(p,cam));
        if(last) {
          const hull=convexOutline(last.concat(current));
          polygons.push('M'+hull.map(([x,y])=>x.toFixed(2)+' '+y.toFixed(2)).join('L')+'Z');
        }
        last=current;
      }
    }
    return polygons.join('');
  }
  return {axes,camera,project,axisDelta,planeDelta,move,shadedFaces,skinPath,dot,add,times};
});
