/**
 * Where the app's data lives, and what it looks like.
 *
 * Everything is read from one static folder, the data root written by
 * `dreams bake`. The dev server mounts it at /data; a release ships it as
 * data/ beside index.html. Paths are relative, so the same build works from
 * any host or subfolder. See docs/pipeline.md.
 *
 * The shapes below mirror bake's output and change with it in the same
 * commit. Project fields named by offset (`x6c`) are ones whose meaning is not
 * verified yet; positions are raw engine units (see ./units.ts).
 */
import type { ClipEntry, ModelEntry } from './animation/types';
import type { Vec3 } from './units';

const DATA_ROOT = `${import.meta.env.BASE_URL}data/`;

export function dataUrl(path: string): string {
  return DATA_ROOT + path;
}

export const sceneFolder = (stem: string): string => dataUrl(`scenes/${stem}/`);
export const modelFolder = (id: string): string => dataUrl(`models/${id}/`);
export const SCENE_FILE = 'scene.gltf';
export const MODEL_FILE = 'model.gltf';

export interface ProjectSummary { index: number; id: string; name: string; scene: string }

export interface DataIndex {
  generated: string;
  projects: ProjectSummary[];
  scenes: Record<string, { textured: boolean }>;
  models: Record<string, { textured: boolean; animation?: ModelEntry }>;
  audio: { music: string[]; sfx: string[]; voice: string[] };
  video: { cutscenes: string[]; movies: string[]; textures: string[] };
  boot: {
    intro: string | null; warp: string | null; elder: string | null;
    menuMusic: string | null; startProject: number;
  };
  resident: { scenes: string[]; models: string[] };
}

export interface ProjectObject {
  slot: number; name: string; asset: string;
  /** Model folder id, or null when the asset is not baked. */
  model: string | null;
  pos: Vec3; heading: number; flags: number;
  x3c: number; x64: number; x68: number; x6c: number; x70: number; x78: number; x88: number;
}

export interface ProjectLink {
  slot: number; name: string; destination: string;
  /** Destination project index. */
  to: number | null;
  min: Vec3; max: Vec3;
}

export interface ProjectBox { slot: number; name: string; xf0: number; points: Vec3[] }

export interface ProjectAdvent {
  slot: number; name: string;
  x14: number; x1c: number; x20: number; x24: number;
  video: string; videoPath: string | null;
}

export interface ProjectRecord {
  index: number; id: string; name: string; scene: string; disc: number;
  spawn: Vec3; heading: number;
  ambient: Vec3; light1: Vec3; light2: Vec3; fog: number[]; sky: number[]; fov: number;
  cdTrack: number; music: string | null; aiSchedule: number; x138: number;
  /** Video names the engine reads at +0x3C / +0x5C, with their baked file. */
  videos: { at: string; file: string; path: string | null }[];
  /** Every printable run in the header's name area (+0x3C..+0x9C), keyed by offset. */
  headerStrings: Record<string, string>;
  links: ProjectLink[]; objects: ProjectObject[]; boxes: ProjectBox[]; advents: ProjectAdvent[];
  needs: string[]; missing: string[];
}

export interface ModelClips extends ModelEntry { clips: ClipEntry[] }

export const isActive = (o: ProjectObject): boolean => (o.flags & 1) !== 0 && (o.flags & 0x100) === 0;
export const isCharacter = (o: ProjectObject): boolean => (o.flags & 2) !== 0;

export async function fetchJson<T>(path: string): Promise<T> {
  const url = dataUrl(path);
  const res = await fetch(url);
  if (!res.ok) throw new Error(`${res.status} ${url} (run: uv run dreams bake)`);
  return res.json() as Promise<T>;
}

let index: Promise<DataIndex> | null = null;

export function loadIndex(): Promise<DataIndex> {
  index ??= fetchJson<DataIndex>('index.json').catch((error) => {
    index = null;
    throw error;
  });
  return index;
}

/** Development shortcut: `?project=62` skips the boot flow and starts in that project. */
export function requestedProject(): number | null {
  const value = new URLSearchParams(window.location.search).get('project');
  return value !== null && /^\d+$/.test(value) ? Number(value) : null;
}

export function loadProject(n: number): Promise<ProjectRecord> {
  return fetchJson<ProjectRecord>(`projects/${n}.json`);
}

export async function animationCatalog(): Promise<ModelEntry[]> {
  const models = (await loadIndex()).models;
  return Object.values(models).flatMap((m) => (m.animation ? [m.animation] : []));
}

/** A file from the index's audio list by stem, e.g. `sfx_009`. */
export function audioFile(list: string[], stem: string): string | null {
  const file = list.find((f) => f.slice(f.lastIndexOf('/') + 1).split('.')[0] === stem);
  return file ? dataUrl(file) : null;
}
