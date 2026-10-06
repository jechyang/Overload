// Small, deterministic authoring tool. Reuses engine meshes; no copied project/model data.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const dir=path.dirname(fileURLToPath(import.meta.url));
const assets=path.join(dir,'Assets');
for(const folder of ['Scenes','Materials'])fs.mkdirSync(path.join(assets,folder),{recursive:true});
const esc=s=>String(s).replaceAll('&','&amp;').replaceAll('<','&lt;').replaceAll('>','&gt;');
const tag=(k,v)=>'<'+k+'>'+esc(v)+'</'+k+'>';
const vec=(k,a)=>'<'+k+'>'+a.map((v,i)=>tag(['x','y','z','w'][i],v)).join('')+'</'+k+'>';
const fields=o=>Object.entries(o).map(([k,v])=>Array.isArray(v)?vec(k,v):tag(k,v)).join('');
const quat=([pitch=0,yaw=0,roll=0])=>{
  const [x,y,z]=[pitch,yaw,roll].map(v=>v*Math.PI/360),cx=Math.cos(x),sx=Math.sin(x),cy=Math.cos(y),sy=Math.sin(y),cz=Math.cos(z),sz=Math.sin(z);
  return [sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz];
};
const component=(name,data)=>'<component>'+tag('type','class OvCore::ECS::Components::'+name)+'<data>'+data+'</data></component>';
const transform=(pos=[0,0,0],scale=[1,1,1],rotation=[0,0,0])=>component('CTransform',vec('position',pos)+vec('rotation',quat(rotation))+vec('scale',scale));
const materials={
  Floor:{albedo:[.09,.12,.18,1],metal:.35,rough:.32},
  Stone:{albedo:[.24,.29,.36,1],metal:.1,rough:.7},
  Dark:{albedo:[.035,.055,.08,1],metal:.3,rough:.45},
  Chrome:{albedo:[.78,.88,.97,1],metal:1,rough:.06},
  Gold:{albedo:[.93,.48,.12,1],metal:.9,rough:.1},
  Copper:{albedo:[.65,.2,.12,1],metal:.7,rough:.2},
  Warm:{albedo:[.85,.23,.06,1],metal:0,rough:.6},
  Cool:{albedo:[.02,.38,.5,1],metal:.1,rough:.4},
  Cyan:{albedo:[.05,.5,.7,1],metal:0,rough:.4,emissive:[.08,.8,1],intensity:2},
  Amber:{albedo:[.95,.34,.04,1],metal:0,rough:.4,emissive:[1,.28,.03],intensity:1.8},
  Glass:{albedo:[.1,.65,.82,.28],metal:.05,rough:.1,glass:true},
};
for(const [name,m] of Object.entries(materials)){
  const settings=fields({support_orthographic:true,support_perspective:true,blendable:!!m.glass,backface_culling:!m.glass,
    frontface_culling:false,depth_test:true,depth_writing:!m.glass,color_writing:true,cast_shadows:!m.glass,
    // Stable priorities make intersecting sculpture edges reproducible across
    // process-local shader/program identities. Normal depth testing stays on.
    receive_shadows:!m.glass,receive_reflections:true,user_interface:false,gpu_instances:1,draw_order:100+Object.keys(materials).indexOf(name)});
  const uniforms={u_Albedo:m.albedo,u_Metallic:m.metal,u_Roughness:m.rough,u_EmissiveColor:m.emissive??[0,0,0],
    u_EmissiveIntensity:m.intensity??0,u_BuiltInToneMapping:false,u_BuiltInGammaCorrection:false};
  fs.writeFileSync(path.join(assets,'Materials',name+'.ovmat'),'<root>'+tag('shader',':Shaders/Standard.ovfx')+'<settings>'+settings+'</settings><uniforms>'+Object.entries(uniforms).map(([k,v])=>'<uniform>'+tag('name',k)+'<value>'+ (Array.isArray(v)?fields(Object.fromEntries(v.map((x,i)=>[['x','y','z','w'][i],x]))):esc(v))+'</value></uniform>').join('')+'</uniforms><features/></root>\n');
}
let next=1;const actors=[];const manifest=[];
function add(name,pos,components='',options={}){
  const id=next++,guid=7000000+id;
  actors.push('<actor>'+fields({name,tag:options.tag??'',active:options.active??true,id,guid,prefab_source:'',prefab_node_guid:0,parent:options.parent??0})+
    '<components>'+transform(pos,options.scale,options.rotation)+components+'</components><behaviours>'+ (options.script?'<behaviour>'+tag('type','Scripts/LabController.lua')+'<data/></behaviour>':'')+'</behaviours></actor>');
  manifest.push({id,guid,name,parent:options.parent??0});return id;
}
function mesh(name,model,mat,pos,scale=[1,1,1],options={}){
  return add(name,pos,component('CModelRenderer',fields({model:':Models/'+model+'.fbx',frustum_behaviour:2}))+
    (options.skin?component('CSkinnedMeshRenderer',fields({playing:true,looping:true,playback_speed:1,mesh_bounds_scale:1.5,pose_eval_rate:0,time_ticks:0,animation_source:'?',animation:'root|air_jump'})):'')+
    component('CMaterialRenderer','<materials>'+Array(options.skin?3:1).fill(tag('material','Materials/'+mat+'.ovmat')).join('')+'</materials><visibility_flags>7</visibility_flags>'),{...options,scale});
}
add('00 Lab Camera',[0,10,23],component('CCamera',fields({fov:45,size:5,near:.1,far:120,clear_color:[.025,.035,.06],frustum_geometry_culling:true,frustum_light_culling:true,projection_mode:1})),{rotation:[22,180,0]});
add('01 Shadow Sun',[-8,14,6],component('CDirectionalLight',fields({color:[1,.89,.72],intensity:2.6,cast_shadows:true,shadow_area_size:38,shadow_follow_camera:false,shadow_map_resolution:1024})),{rotation:[55,-35,0]});
add('02 Warm Point',[-6,4,1],component('CPointLight',fields({color:[1,.23,.06],intensity:15,constant:1,linear:.12,quadratic:.1})));
add('03 Cool Point',[6,4,1],component('CPointLight',fields({color:[.05,.6,1],intensity:15,constant:1,linear:.12,quadratic:.1})));
add('04 Post Process',[0,0,0],component('CPostProcessStack',fields({auto_exposure_enabled:true,auto_exposure_center_weight_bias:.5,auto_exposure_luminance_min:-2,
  auto_exposure_luminance_max:12,auto_exposure_exposure_compensation:0,auto_exposure_progressive:true,auto_exposure_speed_up:2,auto_exposure_speed_down:1,
  bloom_enabled:true,bloom_intensity:.8,bloom_passes:5,tonemapping_enabled:true,tonemapping_exposure:1,tonemapping_mode:5,tonemapping_gamma_correction:true,fxaa_enabled:true})));
add('05 Global Probe - Once',[0,3,0],component('CReflectionProbe',fields({refresh_mode:1,capture_speed:6,resolution:128,capture_position:[0,0,0],brightness:1,influence_policy:1,influence_size:[30,12,22],box_projection:false})));
add('06 Local Probe - Progressive',[6,2.5,0],component('CReflectionProbe',fields({refresh_mode:0,capture_speed:1,resolution:128,capture_position:[0,0,0],brightness:1.2,influence_policy:0,influence_size:[8,8,9],box_projection:true})));
add('07 Lab Controls',[0,0,0],'',{script:true});
const architecture=add('10 Architecture',[0,0,0]);
mesh('Floor','Cube','Floor',[0,-.2,0],[48,.35,40],{parent:architecture});
mesh('Stage','Cube','Dark',[0,.2,-1],[17,.4,7],{parent:architecture});
mesh('Backdrop Warm','Cube','Warm',[-6,2.5,-5.8],[5,5,.3],{parent:architecture});
mesh('Backdrop Cool','Cube','Cool',[6,2.5,-5.8],[5,5,.3],{parent:architecture});
mesh('Backdrop Center','Cube','Dark',[0,2.5,-6],[5,5,.3],{parent:architecture});
for(let i=0;i<10;i++){
  const x=-12+i*2.65;
  mesh('Shadow column '+i,'Cube','Stone',[x,2,-6.4],[.45,4.2,.45],{parent:architecture});
  mesh('Amber wall light '+i,'Cube','Amber',[x,4.1,-6.2],[.8,.08,.16],{parent:architecture});
}
const sculptures=add('20 Reflective Sculptures',[0,0,0]);
for(const [x,mat] of [[-6,'Gold'],[6,'Chrome']]){
  mesh('Mirror pedestal '+x,'Cylinder','Stone',[x,.6,0],[2.4,.8,2.4],{parent:sculptures});
  mesh('Mirror sphere '+x,'Sphere',mat,[x,2.1,0],[2.1,2.1,2.1],{parent:sculptures});
  mesh('Halo '+x,'Torus','Amber',[x,2.1,0],[1.05,1.05,1.05],{parent:sculptures,rotation:[90,0,0]});
}
mesh('Kinetic Ring','Torus','Chrome',[0,2.8,-1],[1.5,1.5,1.5],{parent:sculptures,rotation:[90,0,0]});
mesh('Kinetic Core','Sphere','Cyan',[0,2.8,-1],[1.25,1.25,1.25],{parent:sculptures});
mesh('Orbiting Amber','Sphere','Amber',[3,2.8,-1],[.55,.55,.55],{parent:sculptures});
const glass=add('30 Transparency',[0,0,0]);
mesh('Cyan glass left','Cube','Glass',[-3.5,1.7,2.5],[2,3.3,.12],{parent:glass,rotation:[0,22,0]});
mesh('Cyan glass right','Cube','Glass',[3.5,1.7,2.5],[2,3.3,.12],{parent:glass,rotation:[0,-22,0]});
const animation=add('40 Animation',[0,0,0]);
mesh('Animated Copper','Mannequin','Copper',[-1.5,.5,3.6],[1,1,1],{parent:animation,skin:true});
mesh('Animated Chrome','Mannequin','Chrome',[1.5,.5,3.6],[1,1,1],{parent:animation,skin:true});
const crowd=add('50 Shared Material Gallery',[0,0,0]);
for(let row=0;row<4;row++)for(let col=0;col<12;col++)mesh('Shared cube '+row+' '+col,'Cube',row%2?'Copper':'Chrome',[-11+col*2,.8+row*.9,-7],[.5,.5,.5],{parent:crowd});
fs.writeFileSync(path.join(assets,'Scenes/FrameGraphLab.ovscene'),'<root><scene><actors>\n'+actors.join('\n')+'\n</actors></scene></root>\n');
fs.writeFileSync(path.join(dir,'manifest.json'),JSON.stringify({schema:1,actors:manifest,engineModels:['Cube','Cylinder','Sphere','Torus','Mannequin'],
  shadowResolution:1024,probeResolution:128,bloomPasses:5,notes:'Alpha blended glass; cubemap reflections; fixed pass order. Not refraction, ray tracing, or automatic instancing.'},null,2)+'\n');
console.log('Generated FrameGraphLab: '+actors.length+' actors, '+Object.keys(materials).length+' materials; all meshes reference engine assets.');
