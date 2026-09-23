import assert from 'node:assert/strict';
import {test} from 'node:test';
import {AssetContainer, Mesh, NullEngine, Scene, VertexBuffer} from '@babylonjs/core';
import {
  AnimationController, evaluateWorldTransforms, matApply, quatToMatrix, sampleClipPose,
} from '../src/animation/controller.js';
import {SkeletalAnimator} from '../src/animation/renderer.js';
import type {AnimationClipData, Quat, RigData} from '../src/animation/types.js';

const identity = [[32768,0,0],[0,32768,0],[0,0,32768]];
const turn: Quat = [0,0,Math.SQRT1_2,Math.SQRT1_2];
function rig(): RigData {
  return {schemaVersion:2, model:'test', assetStem:'test', rigId:'test-rig', nodes:[
    {index:0, name:'root', parent:-1, hasGeometry:true, translation:[100,0,0], rotation:identity},
    {index:1, name:'hand', parent:2, hasGeometry:true, translation:[20,0,0], rotation:identity},
    {index:2, name:'helper', parent:0, hasGeometry:false, translation:[10,0,0], rotation:identity},
  ], primitives:{body:[[1,0,0,0],[1,5,0,0],[1,0,5,0]]}};
}
function clip(r = rig(), rotation: Quat = turn): AnimationClipData {
  return {schemaVersion:2, model:r.model, rigId:r.rigId, name:'test', duration:60,
    frameRate:30, trackCount:3, bindingStatus:'verified-directory', tracks:r.nodes.map(n => ({
      nodeIndex:n.index, boneName:n.name, duration:60, restRotation:[0,0,0,1], keyframes:[
        {time:0, rotation:[0,0,0,1]}, {time:60, rotation:n.name === 'helper' ? rotation : [0,0,0,1]},
      ],
    }))};
}
test('directory order and zero-geometry helpers deform their descendants', () => {
  const r = rig();
  const world = evaluateWorldTransforms(r, sampleClipPose(clip(r),60));
  assert.deepEqual(world[1].tr, [110,20,0]);
});
test('missing rotation channels preserve nonidentity bind rotation', () => {
  const r=rig(); r.nodes[2].rotation=quatToMatrix(turn);
  assert.deepEqual(evaluateWorldTransforms(r,{})[1].tr,[110,20,0]);
});
test('Q15 vertex math does not overflow the JS 32-bit shift range', () => {
  assert.deepEqual(matApply(identity,[100000,-100000,200000]),[100000,-100000,200000]);
});
test('independent clocks, speed, pause, overshoot and one-shot completion', () => {
  const r=rig(), c=clip(r), a=new AnimationController(r), b=new AnimationController(r);
  a.setClip(c); b.setClip(c); a.speed=2; a.update(0.5); b.update(0.5);
  assert.equal(a.frame,30); assert.equal(b.frame,15);
  a.playing=false; a.update(1); assert.equal(a.frame,30);
  b.seek(59); b.update(0.1); assert.equal(b.frame,2);
  b.setClip(c,{loop:false}); b.update(5); assert.equal(b.frame,60); assert.equal(b.playing,false);
});
test('seek is exact; inspection restore resumes the prior actor state', () => {
  const r=rig(), a=new AnimationController(r); a.setClip(clip(r),{startFrame:1}); a.update(0.5);
  const saved=a.snapshot(); a.setClip(clip(r),{playing:false}); a.seek(60);
  assert.deepEqual(evaluateWorldTransforms(r,a.pose())[1].tr,[110,20,0]);
  a.restore(saved); assert.equal(a.frame,16); assert.equal(a.playing,true);
});
test('crossfade starts from the previous pose and remains a unit quaternion', () => {
  const r=rig(), a=new AnimationController(r); a.setClip(clip(r)); a.seek(60);
  const q=a.pose()[2]; a.setClip(clip(r,[0,0,0,1]),{fadeSeconds:1});
  assert.ok(Math.abs(a.pose()[2][2]-q[2])<1e-8);
  a.update(0.5); assert.ok(Math.abs(Math.hypot(...a.pose()[2])-1)<1e-8);
  a.update(0.5); assert.deepEqual(a.pose()[2],[0,0,0,1]);
});
test('foreign rigs, unresolved clips, malformed slots, and cyclic rigs fail explicitly', () => {
  const r=rig(), a=new AnimationController(r), good=clip(r); a.setClip(good);
  assert.throws(()=>a.setClip({...good,rigId:'foreign'}),/different rig/);
  assert.throws(()=>a.setClip({...good,bindingStatus:'unresolved',bindingError:'track mismatch'}),/track mismatch/);
  const bad=clip(r); bad.tracks[1].nodeIndex=0;
  assert.throws(()=>a.setClip(bad),/Track slots/); assert.equal(a.clip,good);
  r.nodes[0].parent=1; assert.throws(()=>new AnimationController(r),/Cycle/);
});
test('CPU renderer updates actual vertices and bounds; disposal prevents further changes', () => {
  const engine=new NullEngine(), scene=new Scene(engine), container=new AssetContainer(scene);
  const mesh=new Mesh('body',scene);
  mesh.setVerticesData(VertexBuffer.PositionKind,[0,0,0,1,0,0,0,1,0]); mesh.setIndices([0,1,2]);
  container.meshes.push(mesh);
  const r=rig(), animator=new SkeletalAnimator(r,container,scene);
  animator.controller.setClip(clip(r),{playing:false}); animator.controller.seek(60); animator.apply();
  const positions=Array.from(mesh.getVerticesData(VertexBuffer.PositionKind)!);
  assert.ok(Math.abs(positions[0]-1.1)<1e-5); assert.ok(Math.abs(positions[1]+0.2)<1e-5);
  assert.ok(mesh.getBoundingInfo().boundingBox.maximum.x>1);
  animator.showBones(true); assert.equal(scene.meshes.filter(m=>m.name==='rig_bones').length,1);
  animator.dispose(); animator.update(5);
  assert.equal(scene.meshes.filter(m=>m.name==='rig_bones').length,0);
  assert.deepEqual(Array.from(mesh.getVerticesData(VertexBuffer.PositionKind)!),positions);
  assert.equal(mesh.isDisposed(),false); scene.dispose(); engine.dispose();
});
