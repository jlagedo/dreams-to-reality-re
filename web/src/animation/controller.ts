import type { AnimationClipData, Matrix, Pose, Quat, RigData, Vec3, WorldTransform } from './types.js';

export function quatSlerp(a: Quat, b: Quat, t: number): Quat {
  let dot = a.reduce((sum, v, i) => sum + v * b[i], 0);
  if (dot < 0) { dot = -dot; b = b.map(v => -v) as Quat; }
  dot = Math.min(1, dot);
  const angle = Math.acos(dot);
  const weights = dot > 0.9995 ? [1 - t, t]
    : [Math.sin((1 - t) * angle) / Math.sin(angle), Math.sin(t * angle) / Math.sin(angle)];
  const q = a.map((v, i) => weights[0] * v + weights[1] * b[i]) as Quat;
  const norm = Math.hypot(...q);
  return norm ? q.map(v => v / norm) as Quat : [0, 0, 0, 1];
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

export function evaluateWorldTransforms(rig: RigData, pose: Pose, order = hierarchyOrder(rig)): WorldTransform[] {
  const world: WorldTransform[] = new Array(rig.nodes.length);
  for (const i of order) {
    const node = rig.nodes[i];
    const rot = pose[i] ? quatToMatrix(pose[i]) : node.rotation;
    const parent = world[node.parent];
    if (node.parent === -1) world[i] = {rot, tr: [...node.translation]};
    else {
      const tr = matApply(parent.rot, node.translation).map((v, k) => v + parent.tr[k]) as Vec3;
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
    pose[track.nodeIndex] = quatSlerp(a.rotation, b.rotation, (frame-a.time)/(b.time-a.time));
  }
  return pose;
}

export interface PlaybackState {
  clip: AnimationClipData | null; frame: number; speed: number;
  playing: boolean; loop: boolean; startFrame: number;
}

export class AnimationController {
  public clip: AnimationClipData | null = null;
  public frame = 0;
  public speed = 1;
  public playing = false;
  public loop = true;
  public startFrame = 0;
  public readonly order: number[];
  private fade: {from: Pose; elapsed: number; duration: number} | null = null;
  private disposed = false;
  constructor(public readonly rig: RigData) { this.order = hierarchyOrder(rig); }

  setClip(clip: AnimationClipData, options: {playing?: boolean; loop?: boolean; startFrame?: number; fadeSeconds?: number} = {}): void {
    if (this.disposed) throw new Error('Animation controller was disposed.');
    validateClip(this.rig, clip); // Reject before mutating the current valid playback.
    const from = this.pose();
    this.clip = clip;
    this.startFrame = Math.min(clip.duration, Math.max(0, options.startFrame ?? 0));
    this.frame = this.startFrame;
    this.playing = options.playing ?? true;
    this.loop = options.loop ?? true;
    this.fade = options.fadeSeconds && options.fadeSeconds > 0
      ? {from, elapsed: 0, duration: options.fadeSeconds} : null;
  }

  seek(frame: number): void {
    if (!Number.isFinite(frame)) throw new Error('Frame must be finite.');
    this.frame = Math.max(0, Math.min(this.clip?.duration ?? 0, frame));
    this.fade = null;
  }

  update(seconds: number): void {
    if (this.disposed || !this.playing || !this.clip) return;
    if (!Number.isFinite(seconds) || seconds < 0 || !Number.isFinite(this.speed) || this.speed < 0)
      throw new Error('Invalid animation time step.');
    this.frame += seconds * this.speed * this.clip.frameRate;
    const end = this.clip.duration, span = end - this.startFrame;
    if (this.frame >= end) {
      if (this.loop && span > 0) this.frame = this.startFrame + (this.frame - this.startFrame) % span;
      else { this.frame = end; this.playing = false; }
    }
    if (this.fade) {
      this.fade.elapsed += seconds;
      if (this.fade.elapsed >= this.fade.duration) this.fade = null;
    }
  }

  pose(): Pose {
    const pose = this.clip ? sampleClipPose(this.clip, this.frame) : {};
    if (!this.fade) return pose;
    const alpha = this.fade.elapsed / this.fade.duration;
    for (const node of this.rig.nodes) {
      const bind = matrixToQuat(node.rotation);
      pose[node.index] = quatSlerp(this.fade.from[node.index] ?? bind, pose[node.index] ?? bind, alpha);
    }
    return pose;
  }

  snapshot(): PlaybackState {
    return {clip: this.clip, frame: this.frame, playing: this.playing, speed: this.speed,
      loop: this.loop, startFrame: this.startFrame};
  }
  restore(state: PlaybackState): void {
    if (state.clip) this.setClip(state.clip, state);
    else { this.clip = null; this.fade = null; }
    this.frame = state.frame; this.speed = state.speed; this.playing = state.playing;
  }
  dispose(): void { this.disposed = true; this.playing = false; this.clip = null; this.fade = null; }
}
