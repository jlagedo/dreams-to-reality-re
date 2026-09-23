import type { AnimationClipData, AnimationKeyframeData, Matrix, Pose, Quat, RigData, RootMode, TranslationKeyframeData, TranslationPose, Vec3, WorldTransform } from './types.js';

// Common transition path 00405f1f: weight += 48 * engineDelta, range 0..256.
export const ENGINE_TRANSITION_SECONDS = 256 / (48 * 30);

export function quatSlerp(a: Quat, b: Quat, t: number, shortestPath = true): Quat {
  let dot = a.reduce((sum, v, i) => sum + v * b[i], 0);
  if (dot < 0 && shortestPath) { dot = -dot; b = b.map(v => -v) as Quat; }
  dot = Math.max(-1, Math.min(1, dot));
  if (dot < -0.9995) {
    const orthogonal: Quat = [-a[1],a[0],-a[3],a[2]];
    return a.map((v,i) => v*Math.cos(Math.PI*t)+orthogonal[i]*Math.sin(Math.PI*t)) as Quat;
  }
  const angle = Math.acos(dot);
  const weights = dot > 0.9995 ? [1 - t, t]
    : [Math.sin((1 - t) * angle) / Math.sin(angle), Math.sin(t * angle) / Math.sin(angle)];
  const q = a.map((v, i) => weights[0] * v + weights[1] * b[i]) as Quat;
  const norm = Math.hypot(...q);
  return norm ? q.map(v => v / norm) as Quat : [0, 0, 0, 1];
}

export function splineEase(t: number, leftField: number, rightField: number): number {
  // 00459ec0 and argument pushes at 0045a16a: retain the actual field order.
  let start = rightField, end = leftField;
  const total = start + end;
  if (total === 0) return t;
  if (total > 1) { start /= total; end /= total; }
  const scale = 1 / (2-start-end);
  if (t < start) return scale*t*t/start;
  if (t >= 1-end && end > 0) return 1-scale*(1-t)*(1-t)/end;
  return (2*t-start)*scale;
}

export function sampleRotation(a: AnimationKeyframeData, b: AnimationKeyframeData, t: number): Quat {
  if (!a.outControl || !b.inControl) return quatSlerp(a.rotation,b.rotation,t);
  const u = splineEase(t,a.ease?.[0] ?? 0,b.ease?.[1] ?? 0);
  // Original evaluator blends the primary arc and authored control arc,
  // then blends those results by 2u(1-u). Keep signed quaternion paths.
  return quatSlerp(quatSlerp(a.rotation,b.rotation,u,false),
    quatSlerp(a.outControl,b.inControl,u,false),2*u*(1-u),false);
}

export function quatToMatrix([x, y, z, w]: Quat): Matrix {
  return [
    [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w)],
    [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w)],
    [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y)],
  ].map(row => row.map(v => Math.round(v * 32768)));
}

export function matrixToQuat(matrix: Matrix): Quat {
  const m = matrix.map(row => row.map(v => v / 32768));
  const trace = m[0][0] + m[1][1] + m[2][2];
  let q: Quat;
  if (trace > 0) {
    const s = Math.sqrt(trace + 1) * 2;
    q = [(m[2][1]-m[1][2])/s, (m[0][2]-m[2][0])/s, (m[1][0]-m[0][1])/s, s/4];
  } else {
    const i = m[0][0] > m[1][1] ? (m[0][0] > m[2][2] ? 0 : 2) : (m[1][1] > m[2][2] ? 1 : 2);
    const j = (i + 1) % 3, k = (i + 2) % 3;
    const s = Math.sqrt(Math.max(0, 1 + m[i][i] - m[j][j] - m[k][k])) * 2;
    q = [0, 0, 0, (m[k][j]-m[j][k])/s];
    q[i] = s/4; q[j] = (m[j][i]+m[i][j])/s; q[k] = (m[k][i]+m[i][k])/s;
  }
  return quatSlerp(q, q, 0);
}

export function matApply(m: Matrix, v: Vec3): Vec3 {
  // JS bit shifts truncate to signed 32-bit before shifting: large model
  // coordinates overflow there. Divide as a number, then floor like SAR.
  return m.map(row => Math.floor(row.reduce((sum, n, i) => sum + n * v[i], 0) / 32768)) as Vec3;
}

function matMul(a: Matrix, b: Matrix): Matrix {
  return a.map(row => b[0].map((_, col) =>
    Math.floor(row.reduce((sum, n, k) => sum + n * b[k][col], 0) / 32768)));
}

export function hierarchyOrder(rig: RigData): number[] {
  if (rig.schemaVersion !== 2) throw new Error('Animation rig needs a schema 2 export.');
  const visited = new Set<number>(), visiting = new Set<number>(), order: number[] = [];
  const visit = (i: number) => {
    if (visited.has(i)) return;
    if (visiting.has(i)) throw new Error('Cycle in animation rig.');
    const node = rig.nodes[i];
    if (!node || node.index !== i) throw new Error(`Missing rig slot ${i}.`);
    visiting.add(i);
    if (node.parent !== -1) visit(node.parent);
    visiting.delete(i); visited.add(i); order.push(i);
  };
  rig.nodes.forEach((_, i) => visit(i));
  return order;
}

export function evaluateWorldTransforms(rig: RigData, pose: Pose, order = hierarchyOrder(rig), translations: TranslationPose = {}): WorldTransform[] {
  const world: WorldTransform[] = new Array(rig.nodes.length);
  for (const i of order) {
    const node = rig.nodes[i];
    const rot = pose[i] ? quatToMatrix(pose[i]) : node.rotation;
    const parent = world[node.parent];
    const local = translations[i] ?? node.translation;
    if (node.parent === -1) world[i] = {rot, tr: [...local]};
    else {
      const tr = matApply(parent.rot, local).map((v, k) => v + parent.tr[k]) as Vec3;
      world[i] = {rot: matMul(parent.rot, rot), tr};
    }
  }
  return world;
}

export function validateClip(rig: RigData, clip: AnimationClipData): void {
  if (clip.bindingStatus !== 'verified-directory') throw new Error(clip.bindingError || 'Unresolved bone binding.');
  if (clip.schemaVersion !== 2 || clip.rigId !== rig.rigId || clip.model !== rig.model)
    throw new Error('Clip belongs to a different rig.');
  if (!Number.isFinite(clip.duration) || clip.duration < 0 || !Number.isFinite(clip.frameRate) || clip.frameRate <= 0)
    throw new Error('Invalid animation timing.');
  if (clip.trackCount !== rig.nodes.length || clip.tracks.length !== rig.nodes.length)
    throw new Error('Track count does not match the rig.');
  const seen = new Set<number>();
  for (const track of clip.tracks) {
    if (seen.has(track.nodeIndex) || rig.nodes[track.nodeIndex]?.name !== track.boneName)
      throw new Error('Track slots do not match the named rig.');
    seen.add(track.nodeIndex);
    let last = -Infinity;
    for (const key of track.keyframes) {
      if (!Number.isFinite(key.time) || key.time < last || !key.rotation.every(Number.isFinite)
          || Math.abs(Math.hypot(...key.rotation) - 1) > 0.001)
        throw new Error('Invalid rotation key.');
      for (const control of [key.outControl,key.inControl]) {
        if (control && (!control.every(Number.isFinite) || Math.abs(Math.hypot(...control)-1)>0.001))
          throw new Error('Invalid spline control.');
      }
      if (key.ease?.some(v => !Number.isFinite(v) || v < 0)) throw new Error('Invalid spline easing.');
      last = key.time;
    }
    last = -Infinity;
    for (const key of track.translationKeys ?? []) {
      if (!Number.isFinite(key.time) || key.time < last || !key.position.every(Number.isFinite)
          || key.inTangent?.some(v => !Number.isFinite(v)) || key.outTangent?.some(v => !Number.isFinite(v))
          || key.ease?.some(v => !Number.isFinite(v) || v < 0)) throw new Error('Invalid translation key.');
      last = key.time;
    }
  }
}

export function sampleClipPose(clip: AnimationClipData, frame: number): Pose {
  const pose: Pose = {};
  for (const track of clip.tracks) {
    const keys = track.keyframes;
    if (!keys.length) continue; // Preserve the rig's bind rotation for absent channels.
    if (frame <= keys[0].time) { pose[track.nodeIndex] = keys[0].rotation; continue; }
    if (frame >= keys[keys.length-1].time) { pose[track.nodeIndex] = keys[keys.length-1].rotation; continue; }
    let low = 0, high = keys.length - 1;
    while (low + 1 < high) {
      const mid = (low + high) >> 1;
      if (keys[mid].time <= frame) low = mid; else high = mid;
    }
    const a = keys[low], b = keys[high];
    pose[track.nodeIndex] = sampleRotation(a, b, (frame-a.time)/(b.time-a.time));
  }
  return pose;
}

export function sampleTranslation(a: TranslationKeyframeData, b: TranslationKeyframeData, t: number): Vec3 {
  if (!a.outTangent || !b.inTangent) return a.position.map((v,i) => v+(b.position[i]-v)*t) as Vec3;
  const u = splineEase(t,a.ease?.[0] ?? 0,b.ease?.[1] ?? 0), u2=u*u, u3=u2*u;
  // Hermite matrix at 004aa710; stored tangents already include interval scaling.
  return a.position.map((v,i) => (2*u3-3*u2+1)*v + (-2*u3+3*u2)*b.position[i]
    + (u3-2*u2+u)*a.outTangent![i] + (u3-u2)*b.inTangent![i]) as Vec3;
}

export function sampleClipTranslations(clip: AnimationClipData, frame: number): TranslationPose {
  const result: TranslationPose = {};
  for (const track of clip.tracks) {
    const keys=track.translationKeys;
    if (!keys?.length) continue;
    if (frame<=keys[0].time) { result[track.nodeIndex]=[...keys[0].position]; continue; }
    if (frame>=keys[keys.length-1].time) { result[track.nodeIndex]=[...keys[keys.length-1].position]; continue; }
    let low=0, high=keys.length-1;
    while (low+1<high) { const mid=(low+high)>>1; if (keys[mid].time<=frame) low=mid; else high=mid; }
    const a=keys[low], b=keys[high];
    result[track.nodeIndex]=sampleTranslation(a,b,(frame-a.time)/(b.time-a.time));
  }
  return result;
}

interface BlendChannel { clip: AnimationClipData; frame: number; weight: number }
interface FadeState { from: Pose; positions: TranslationPose; elapsed: number; duration: number; hold: boolean }
export interface PlaybackState {
  clip: AnimationClipData | null; frame: number; speed: number;
  playing: boolean; loop: boolean; startFrame: number;
  rootMode: RootMode; blend: BlendChannel | null; fade: FadeState | null;
}

export class AnimationController {
  public clip: AnimationClipData | null = null;
  public frame = 0;
  public speed = 1;
  public playing = false;
  public loop = true;
  public startFrame = 0;
  public rootMode: RootMode = 'in-place';
  public rootMotionDelta: Vec3 = [0,0,0];
  public readonly order: number[];
  private blend: BlendChannel | null = null;
  private fade: FadeState | null = null;
  private disposed = false;
  constructor(public readonly rig: RigData) { this.order = hierarchyOrder(rig); }

  setClip(clip: AnimationClipData, options: {playing?: boolean; loop?: boolean; startFrame?: number;
      fadeSeconds?: number; engineTransition?: boolean} = {}): void {
    if (this.disposed) throw new Error('Animation controller was disposed.');
    validateClip(this.rig, clip);
    const from=this.pose(), positions=this.translations(true);
    this.clip = clip;
    this.startFrame = Math.min(clip.duration, Math.max(0, options.startFrame ?? clip.playbackStart ?? 0));
    this.frame = this.startFrame;
    this.playing = options.playing ?? true;
    this.loop = options.loop ?? true;
    this.blend = null; this.rootMotionDelta=[0,0,0];
    const duration=options.engineTransition ? ENGINE_TRANSITION_SECONDS : options.fadeSeconds ?? 0;
    this.fade=duration>0 ? {from,positions,elapsed:0,duration,hold:!!options.engineTransition} : null;
  }

  setBlend(clip: AnimationClipData | null, weight=0.5): void {
    if (this.disposed) throw new Error('Animation controller was disposed.');
    if (clip) validateClip(this.rig,clip);
    if (!Number.isFinite(weight) || weight<0 || weight>1) throw new Error('Blend weight must be 0..1.');
    this.blend=clip ? {clip,frame:clip.playbackStart ?? 0,weight} : null;
    this.fade=null; this.rootMotionDelta=[0,0,0];
  }
  setBlendWeight(weight: number): void {
    if (!Number.isFinite(weight) || weight<0 || weight>1) throw new Error('Blend weight must be 0..1.');
    if (this.blend) this.blend.weight=weight;
    this.rootMotionDelta=[0,0,0];
  }
  get secondaryFrame(): number | null { return this.blend?.frame ?? null; }

  seek(frame: number): void {
    if (!Number.isFinite(frame)) throw new Error('Frame must be finite.');
    this.frame = Math.max(0, Math.min(this.clip?.duration ?? 0, frame));
    if (this.blend) this.blend.frame=Math.max(0,Math.min(this.blend.clip.duration,frame));
    this.fade = null; this.rootMotionDelta=[0,0,0];
  }

  private advance(frame: number, clip: AnimationClipData, seconds: number, start: number): [number,number] {
    frame=Math.max(start,frame)+seconds*this.speed*clip.frameRate;
    const span=clip.duration-start;
    if (frame<clip.duration) return [frame,0];
    if (!this.loop || span<=0) return [clip.duration,0];
    return [start+(frame-start)%span,Math.floor((frame-start)/span)];
  }

  update(seconds: number): void {
    this.rootMotionDelta=[0,0,0];
    if (this.disposed || !this.playing || !this.clip) return;
    if (!Number.isFinite(seconds) || seconds < 0 || !Number.isFinite(this.speed) || this.speed < 0)
      throw new Error('Invalid animation time step.');
    if (this.speed===0) return;
    this.frame=Math.max(this.startFrame,this.frame);
    if (this.blend) this.blend.frame=Math.max(this.blend.clip.playbackStart ?? 0,this.blend.frame);
    const before=this.rootPosition(), transitioning=!!this.fade;
    if (this.fade) {
      const remaining=this.fade.duration-this.fade.elapsed;
      this.fade.elapsed+=seconds*this.speed;
      if (this.fade.hold) seconds=Math.max(0,seconds-remaining/this.speed);
      if (this.fade.elapsed>=this.fade.duration) this.fade=null;
    }
    let cycles=0, secondaryCycles=0;
    [this.frame,cycles]=this.advance(this.frame,this.clip,seconds,this.startFrame);
    if (this.blend) [this.blend.frame,secondaryCycles]=this.advance(
      this.blend.frame,this.blend.clip,seconds,this.blend.clip.playbackStart ?? 0);
    if (!this.loop && this.frame>=this.clip.duration) this.playing=false;
    if (transitioning) return; // A pose transition must not generate locomotion from changing origins.
    const after=this.rootPosition(), root=0;
    const cycle=(clip: AnimationClipData, start: number): Vec3 => {
      const first=sampleClipTranslations(clip,start)[root] ?? this.rig.nodes[root].translation;
      const last=sampleClipTranslations(clip,clip.duration)[root] ?? first;
      return last.map((v,i)=>v-first[i]) as Vec3;
    };
    const main=cycle(this.clip,this.startFrame);
    const other=this.blend ? cycle(this.blend.clip,this.blend.clip.playbackStart ?? 0) : [0,0,0];
    const w=this.blend?.weight ?? 0;
    this.rootMotionDelta=after.map((v,i)=>v-before[i]+main[i]*cycles*(1-w)+other[i]*secondaryCycles*w) as Vec3;
  }

  pose(): Pose {
    const pose = this.clip ? sampleClipPose(this.clip, this.frame) : {};
    if (this.blend) {
      const secondary=sampleClipPose(this.blend.clip,this.blend.frame);
      for (const node of this.rig.nodes) {
        const bind=matrixToQuat(node.rotation);
        pose[node.index]=quatSlerp(pose[node.index] ?? bind,secondary[node.index] ?? bind,this.blend.weight);
      }
    }
    if (!this.fade) return pose;
    const alpha=Math.min(1,this.fade.elapsed/this.fade.duration);
    for (const node of this.rig.nodes) {
      const bind=matrixToQuat(node.rotation);
      pose[node.index]=quatSlerp(this.fade.from[node.index] ?? bind,pose[node.index] ?? bind,alpha);
    }
    return pose;
  }

  translations(includeRootTravel=false): TranslationPose {
    const pose=this.clip ? sampleClipTranslations(this.clip,this.frame) : {};
    if (this.blend) {
      const other=sampleClipTranslations(this.blend.clip,this.blend.frame), w=this.blend.weight;
      for (const node of this.rig.nodes) {
        const a=pose[node.index] ?? node.translation, b=other[node.index] ?? node.translation;
        pose[node.index]=a.map((v,i)=>v+(b[i]-v)*w) as Vec3;
      }
    }
    if (this.fade) {
      const alpha=Math.min(1,this.fade.elapsed/this.fade.duration);
      for (const node of this.rig.nodes) {
        const a=this.fade.positions[node.index] ?? node.translation, b=pose[node.index] ?? node.translation;
        pose[node.index]=a.map((v,i)=>v+(b[i]-v)*alpha) as Vec3;
      }
    }
    if (!includeRootTravel && this.rootMode==='in-place' && pose[0]) {
      const bind=this.rig.nodes[0].translation;
      pose[0]=[bind[0],pose[0][1],bind[2]]; // Keep vertical body motion, remove horizontal travel.
    }
    return pose;
  }
  rootPosition(): Vec3 { return [...(this.translations(true)[0] ?? this.rig.nodes[0].translation)]; }

  snapshot(): PlaybackState {
    return {clip:this.clip,frame:this.frame,playing:this.playing,speed:this.speed,loop:this.loop,
      startFrame:this.startFrame,rootMode:this.rootMode,blend:this.blend ? {...this.blend} : null,
      fade:this.fade ? {...this.fade} : null};
  }
  restore(state: PlaybackState): void {
    if (state.clip) this.setClip(state.clip,state); else this.clip=null;
    this.frame=state.frame; this.speed=state.speed; this.playing=state.playing; this.rootMode=state.rootMode;
    this.blend=state.blend ? {...state.blend} : null; this.fade=state.fade ? {...state.fade} : null;
    this.rootMotionDelta=[0,0,0];
  }
  dispose(): void {
    this.disposed=true; this.playing=false; this.clip=null; this.fade=null; this.blend=null;
    this.rootMotionDelta=[0,0,0];
  }
}
