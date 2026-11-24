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
  // Rotate a bone from its standing direction to its seated direction. Applying
  // the inverse to a drag keeps the file's offset in the original bone frame.
  function seatTransforms() {
    const j=G.joints, transforms={};
    function bone(start,end,direction,origin) {
      const span=sub(j[end],j[start]), from=unit(span), to=unit(direction);
      const axis=cross(from,to), divisor=1+dot(from,to);
      function rotate(v,inverse=false) {
        const first=cross(axis,v);
        return add(add(v,times(first,inverse?-1:1)),times(cross(axis,first),1/divisor));
      }
      transforms[start]={origin,rest:j[start],rotate};
      return add(origin,times(to,Math.hypot(...span)));
    }
    // Solve upward from a level foot so the sole keeps its original floor height.
    const thigh=[.13,-1,-.095], shin=[.035,-.085,-1];
    const legDrop=(name,end,direction)=>Math.hypot(...sub(j['right_'+end],j['right_'+name]))*unit(direction)[2];
    const hipZ=j.right_foot[2]-legDrop('thigh','shin',thigh)-legDrop('shin','foot',shin);
    const lower=[0,0,hipZ-j.pelvis[2]], identity=v=>v;
    transforms.torso={origin:add(j.pelvis,lower),rest:j.pelvis,rotate:identity};
    for(const [side,sign] of [['right',1],['left',-1]]) {
      const key=name=>side+'_'+name;
      const direction=([x,y,z])=>[sign*x,y,z];
      const thighAt=add(j[key('thigh')],lower);
      const knee=bone(key('thigh'),key('shin'),direction(thigh),thighAt);
      const ankle=bone(key('shin'),key('foot'),direction(shin),knee);
      transforms[key('foot')]={origin:ankle,rest:j[key('foot')],rotate:identity};
      const shoulder=add(j[key('upper_arm')],lower);
      const elbow=bone(key('upper_arm'),key('forearm'),direction([.30,.06,-1]),shoulder);
      bone(key('forearm'),key('hand'),direction([-.13,-1,.04]),elbow);
    }
    return transforms;
  }
  const seated=seatTransforms();
  function seatPoint(point,anchor) {
    const t=seated[anchor] || seated.torso;
    return add(t.origin,t.rotate(sub(point,t.rest)));
  }
  function equipmentWorld(row,isPouch,pose,eyeHeight=G.referenceEyeHeight) {
    if(pose==='seated') {
      // The reference figure has a known seated eye height. Use below-eye
      // offsets directly, including for files calibrated while already seated.
      if(isPouch && row.bone===null)
        return seatPoint([row.body[1],-row.body[0],G.referenceEyeHeight-row.body[2]]);
      if(!isPouch) return seatPoint(G.world(row),row.anchor);
      return seatPoint(G.pouchWorld(row));
    }
    return isPouch ? G.pouchWorld(row) : G.world(row,eyeHeight);
  }
  function move(row,isPouch,delta,pose) {
    if(pose==='seated' && !isPouch && seated[row.anchor]) delta=seated[row.anchor].rotate(delta,true);
    if (isPouch) return G.shiftPouch(row,delta);
    if (row.anchor==='auto') {
      const [forward,right,up]=row.position;
      return {...row, position:[forward-delta[1],right+delta[0],up+delta[2]]};
    }
    return {...row, offset:G.local(row.anchor,add(G.world(row),delta))};
  }

  // Build the reference skeleton once. All geometry is in the same metre-space
  // as the holsters. Closed rings give the bones volume from every camera angle.
  function skeleton(pose) {
    const faces=[], j=G.joints;
    let anchor='torso';
    function shell(rings, tone=1) {
      if(pose==='seated') rings=rings.map(ring=>ring.map(p=>seatPoint(p,anchor)));
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
      anchor=side+'_upper_arm';
      bone(at('upper_arm'),at('forearm'),.036);
      anchor=side+'_forearm';
      bone(at('forearm'),at('hand'),.024);
      bone(add(at('forearm'),[0,.015,-.008]),add(at('hand'),[0,.022,0]),.015);
      oval(at('hand'),[.045,.027,.031]);
      for(let i=0;i<4;i++) {
        const knuckle=add(at('hand'),[sign*.027,(i-1.5)*.014,-.008]);
        const tip=add(knuckle,[sign*(.039-.006*Math.abs(i-1)),(i-1.5)*.004,-.020]);
        bone(knuckle,tip,.007);
      }
      anchor=side+'_thigh';
      bone(at('thigh'),at('shin'),.049);
      anchor=side+'_shin';
      bone(at('shin'),at('foot'),.034);
      anchor=side+'_foot';
      bone(at('foot'),at('toe'),.037);
      for(const [name,r] of [['upper_arm',.041],['forearm',.029],['thigh',.049],['shin',.043]]) {
        anchor=side+'_'+name; oval(at(name),[r,r,r]);
      }
      anchor='torso';
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
  const skeletons=new Map();
  function shadedFaces(cam,pose='standing') {
    if(!skeletons.has(pose)) skeletons.set(pose,skeleton(pose));
    const faces=skeletons.get(pose);
    const light=unit(add(cam.depth,add(times(cam.right,-.55),times(cam.up,.75))));
    return faces.filter(f=>dot(f.normal,cam.depth)>0).sort((a,b)=>dot(a.centre,cam.depth)-dot(b.centre,cam.depth)).map(f=>{
      const shade=Math.round((23+Math.max(0,dot(f.normal,light))*54)*f.tone);
      return {shade, path:'M'+f.points.map(p=>project(p,cam).map(v=>v.toFixed(2)).join(' ')).join('L')+'Z'};
    });
  }
  // Each part is a series of elliptical cross-sections in the skeleton's metre space.
  // The outside of their combined projection stays continuous across joints.
  const buildRange = {minKg:45, referenceKg:75, maxKg:180};
  function bodySections(weightKg,pose) {
    // Model a change in cross-section at a fixed skeleton height. These are
    // illustrative regional proportions, not an anthropometric prediction.
    const size = Math.sqrt(weightKg/buildRange.referenceKg)-1;
    const change = size<0 ? size*.68 : size;
    const j=G.joints, parts=[];
    let anchor='torso';
    const circle=Array.from({length:24},(_,i)=>{
      const a=2*Math.PI*i/24;
      return [Math.cos(a),Math.sin(a)];
    });
    function sections(rows,u,v) {
      const rings=rows.map(([centre,ru,rv])=>circle.map(([c,s])=>{
        const point=add(centre,add(times(u,ru*c),times(v,rv*s)));
        return pose==='seated' ? seatPoint(point,anchor) : point;
      }));
      const transform=pose==='seated' && (seated[anchor] || seated.torso);
      if(transform) { u=transform.rotate(u); v=transform.rotate(v); }
      parts.push({rings,u,v});
    }
    function upright(origin,rows) {
      sections(rows.map(([z,back,width,depth,fullness=0])=>{
        const gain=change*fullness;
        const depthDelta=depth*gain*1.16;
        return [add(origin,[0,back-depthDelta*.35,z]),width*(1+gain),depth+depthDelta];
      }),[1,0,0],[0,1,0]);
    }
    function limb(a,b,rows) {
      const span=sub(b,a), axis=unit(span);
      const back=unit(sub([0,1,0],times(axis,axis[1])));
      const across=cross(axis,back);
      sections(rows.map(([t,depth,width,fullness=0])=>{
        const scale=1+change*fullness;
        return [add(a,times(span,t)),depth*scale,width*scale];
      }),back,across);
    }
    upright(j.pelvis,[
      [-.11,.008,.08,.066,.50],[-.065,.017,.147,.106,.85],[.005,.020,.177,.124,1.05],
      [.085,.012,.159,.115,1.38],[.15,.006,.15,.108,1.55],[.23,0,.17,.12,1.25],
      [.34,-.002,.195,.132,.82],[.43,.010,.208,.12,.53],[.49,.027,.189,.09,.34],[.55,.023,.063,.062,.30]
    ]);
    upright(j.neck,[[-.041,.001,.073,.070,.35],[.012,0,.058,.056,.30],[.088,-.007,.056,.060,.16]]);
    upright(j.head,[
      [-.041,-.043,.037,.051],[-.012,-.029,.066,.078],[.023,-.012,.084,.093],
      [.069,0,.092,.104],[.114,.005,.088,.099],[.15,.009,.065,.077],[.166,.01,.028,.033]
    ]);
    upright(j.head,[[.014,-.093,.014,.020],[.030,-.103,.017,.024],[.065,-.094,.010,.012]]);
    for(const [side,sign] of [['right',1],['left',-1]]) {
      const at=name=>j[side+'_'+name];
      anchor=side+'_upper_arm';
      limb(at('upper_arm'),at('forearm'),[
        [-.19,.045,.049,.33],[.04,.073,.072,.49],[.32,.063,.060,.76],[.59,.052,.054,.75],[.90,.043,.042,.43],[1.06,.039,.04,.22]
      ]);
      anchor=side+'_forearm';
      limb(at('forearm'),at('hand'),[
        [-.09,.04,.04,.22],[.12,.05,.047,.43],[.36,.047,.045,.48],[.69,.034,.034,.32],[1.02,.027,.026,.12]
      ]);
      const hand=at('hand');
      limb(hand,add(hand,[sign*.083,-.006,-.032]),[
        [-.18,.025,.025],[.24,.041,.027],[.55,.038,.024],[.90,.029,.018],[1.03,.013,.012]
      ]);
      limb(add(hand,[sign*.009,-.025,0]),add(hand,[sign*.052,-.055,-.023]),[
        [0,.019,.016],[.51,.016,.014],[1,.009,.010]
      ]);
      anchor=side+'_thigh';
      limb(at('thigh'),at('shin'),[
        [-.10,.079,.076,.72],[.09,.112,.097,1.03],[.31,.104,.089,.95],[.61,.083,.072,.82],[.83,.063,.056,.54],[1.05,.056,.055,.36]
      ]);
      anchor=side+'_shin';
      limb(at('shin'),at('foot'),[
        [-.08,.050,.052,.36],[.12,.060,.055,.43],[.32,.070,.057,.60],[.50,.060,.051,.49],[.76,.038,.035,.29],[1.04,.034,.032,.14]
      ]);
      anchor=side+'_foot';
      const ankle=at('foot'), toe=at('toe');
      const sole=[(ankle[0]+toe[0])/2,(ankle[1]+toe[1])/2,0];
      upright(sole,[[.004,0,.047,.113],[.021,-.002,.052,.124],[.043,.002,.050,.118],
        [.067,.024,.043,.089],[.092,.050,.035,.051],[.114,.065,.030,.033]]);
    }
    return parts;
  }
  let skin, cachedWeight, cachedPose;
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
  function skinFor(weightKg,pose) {
    if(!Number.isFinite(weightKg) || weightKg<buildRange.minKg || weightKg>buildRange.maxKg)
      throw new RangeError('Body weight outside the reference range.');
    if(weightKg!==cachedWeight || pose!==cachedPose) {
      skin=bodySections(weightKg,pose);
      cachedPose=pose;
      cachedWeight=weightKg;
    }
    return skin;
  }
  function skinPath(cam,weightKg=buildRange.referenceKg,pose='standing') {
    const polygons=[];
    for(const {rings} of skinFor(weightKg,pose)) {
      let last=null;
      for(const ring of rings) {
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
  // The outline is a union of convex volumes between adjacent skin rings.
  // Slice each volume in its own ring planes, where a sphere cuts a circle.
  // A side of either end polygon supplies a supporting plane for the volume.
  function sectionPlanes(a,b) {
    const planes=[];
    for(const ring of [a,b]) for(let i=0;i<ring.length;i++) {
      const p=ring[i], q=ring[(i+1)%ring.length];
      const nx=q[1]-p[1], ny=p[0]-q[0];
      const support=r=>Math.max(...r.map(([x,y])=>nx*x+ny*y));
      const start=support(a);
      planes.push({nx,ny,start,change:support(b)-start});
    }
    return planes;
  }
  function collisionSections(part) {
    const {u,v,rings}=part, normal=cross(u,v);
    const flat=rings.map(ring=>ring.map(p=>[dot(u,p),dot(v,p)]));
    const sections=[];
    for(let i=1;i<rings.length;i++) {
      const a=flat[i-1], b=flat[i], points=a.concat(b);
      const xs=points.map(p=>p[0]), ys=points.map(p=>p[1]);
      sections.push({a,b,z0:dot(normal,rings[i-1][0]),z1:dot(normal,rings[i][0]),
        minX:Math.min(...xs),maxX:Math.max(...xs),minY:Math.min(...ys),maxY:Math.max(...ys)});
    }
    return {normal,sections};
  }
  function clipDisk(polygon,{nx,ny,start,change},fraction) {
    const limit=start+fraction*change, clipped=[];
    let a=polygon[polygon.length-1], da=nx*a[0]+ny*a[1]-limit;
    for(const b of polygon) {
      const db=nx*b[0]+ny*b[1]-limit;
      if((da<=0)!==(db<=0)){
        const t=da/(da-db);
        clipped.push([a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])]);
      }
      if(db<=0) clipped.push(b);
      a=b; da=db;
    }
    return clipped;
  }
  const diskEdge=Array.from({length:40},(_,i)=>[Math.cos(i*Math.PI/20),Math.sin(i*Math.PI/20)]);
  function overlapPath(centre,radius,cam,weightKg=buildRange.referenceKg,pose='standing') {
    if(!(radius>0) || !Number.isFinite(radius) || !centre.every(Number.isFinite)) return '';
    const paths=[];
    for(const part of skinFor(weightKg,pose)) {
      const {normal,sections}=part.collision ??= collisionSections(part);
      const cx=dot(part.u,centre), cy=dot(part.v,centre), cz=dot(normal,centre);
      for(const segment of sections) {
        const {minX,maxX,minY,maxY,z0,z1}=segment;
        const dx=Math.max(minX-cx,0,cx-maxX), dy=Math.max(minY-cy,0,cy-maxY);
        const dz=Math.max(z0-cz,0,cz-z1);
        if(dx*dx+dy*dy+dz*dz>=radius*radius) continue;
        const planes=segment.planes ??= sectionPlanes(segment.a,segment.b);
        const low=Math.max(z0,cz-radius), high=Math.min(z1,cz+radius);
        if(high<=low) continue;
        const angle=z=>Math.asin(Math.max(-1,Math.min(1,(z-cz)/radius)));
        const angle0=angle(low), angle1=angle(high);
        const steps=Math.max(2,Math.ceil((angle1-angle0)*24/Math.PI));
        const points=[];
        for(let i=0;i<=steps;i++) {
          const t=angle0+(angle1-angle0)*i/steps;
          const z=i===0 ? low : i===steps ? high : cz+radius*Math.sin(t);
          const r=radius*Math.cos(t);
          let disk=diskEdge.map(([x,y])=>[cx+r*x,cy+r*y]);
          for(const plane of planes) {
            disk=clipDisk(disk,plane,(z-z0)/(z1-z0));
            if(!disk.length) break;
          }
          for(const [x,y] of disk) {
            const point=normal.map((n,k)=>part.u[k]*x+part.v[k]*y+n*z);
            points.push(project(point,cam));
          }
        }
        if(points.length<3) continue;
        const hull=convexOutline(points);
        if(hull.length>=3) paths.push('M'+hull.map(p=>p.map(v=>v.toFixed(2)).join(' ')).join('L')+'Z');
      }
    }
    return paths.join('');
  }
  function chairPath(cam) {
    const lines=[];
    function cushion(top,bottom) {
      lines.push([...top,top[0]],[...bottom,bottom[0]]);
      for(let i=0;i<top.length;i+=2) lines.push([bottom[i],top[i]]);
    }
    const rim=[[-.195,-.37],[.195,-.37],[.245,-.32],[.245,.12],
      [.20,.17],[-.20,.17],[-.245,.12],[-.245,-.32]];
    const seatZ=seated.torso.origin[2]-.098;
    cushion(rim.map(([x,y])=>[x,y,seatZ]),rim.map(([x,y])=>[x,y,seatZ-.042]));
    const back=[[-.175,.04],[.175,.04],[.21,.09],[.21,.44],
      [.17,.49],[-.17,.49],[-.21,.44],[-.21,.09]];
    const pad=depth=>back.map(([x,h])=>[x,.14+h*.16+depth,seatZ+h]);
    cushion(pad(0),pad(.03));
    for(const sign of [-1,1]) {
      const x=sign*.274;
      lines.push([[sign*.22,.026,seatZ-.02],[x,.026,seatZ+.207]]);
      const arm=[[x,-.19,seatZ+.215],[x,.08,seatZ+.215],[x,.08,seatZ+.196],[x,-.19,seatZ+.196]];
      lines.push([...arm,arm[0]]);
    }
    lines.push([[0,0,.10],[0,0,seatZ-.042]]);
    for(let i=0;i<5;i++){
      const a=i*Math.PI*2/5, x=.28*Math.cos(a), y=.28*Math.sin(a);
      lines.push([[0,0,.10],[x,y,.050],[x,y,.031]]);
      const wheel=Array.from({length:13},(_,k)=>{
        const t=k*Math.PI/6;
        return [x+.026*Math.cos(t)*Math.cos(a),y+.026*Math.cos(t)*Math.sin(a),.027+.026*Math.sin(t)];
      });
      lines.push(wheel);
    }
    return lines.map(line=>'M'+line.map(p=>project(p,cam).map(v=>v.toFixed(2)).join(' ')).join('L')).join('');
  }
  return {axes,camera,project,axisDelta,planeDelta,move,seatPoint,equipmentWorld,chairPath,shadedFaces,skinPath,overlapPath,buildRange,dot,add,times};
});
