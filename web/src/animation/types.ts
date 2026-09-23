export type Vec3 = [number, number, number];
export type Quat = [number, number, number, number];
export type Matrix = number[][];
export interface AnimationKeyframeData { time: number; rotation: Quat }
export interface AnimationTrackData {
  nodeIndex: number; boneName?: string | null; meshNodeIndex?: number | null;
  duration: number; translationKeyCount?: number; keyStride?: number;
  restRotation: Quat; keyframes: AnimationKeyframeData[];
}
export interface AnimationClipData {
  schemaVersion: number; model: string; rigId: string;
  name: string; duration: number; frameRate: number; frameRateSource?: string;
  trackCount: number; tracks: AnimationTrackData[];
  bindingStatus: string; bindingError?: string | null;
}
export interface RigNode {
  index: number; name: string; parent: number; hasGeometry: boolean;
  translation: Vec3; rotation: Matrix;
}
export interface RigData {
  schemaVersion: number; model: string; assetStem: string; rigId: string;
  nodes: RigNode[]; primitives: Record<string, [number, number, number, number][]>;
}
export interface ClipEntry {
  id: string; name: string; duration: number; trackCount: number; frameRate: number;
  playable: boolean; bindingError?: string | null;
}
export interface ModelEntry {
  model: string; assetStem: string; rigId: string; nodeCount: number;
  clipCount: number; playableClipCount: number; defaultClipId: string | null;
}
export type Pose = Record<number, Quat>;
export interface WorldTransform { rot: Matrix; tr: Vec3 }
