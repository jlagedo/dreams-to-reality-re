import type { AssetContainer, Scene } from '@babylonjs/core';
import { SkeletalAnimator } from './renderer.js';
import type { AnimationClipData, ClipEntry, ModelEntry, RigData } from './types.js';

/** Shared immutable data; playback state and vertex buffers belong to each actor. */
export class AnimationLibrary {
  private requests = new Map<string, Promise<unknown>>();
  private json<T>(url: string): Promise<T> {
    if (!this.requests.has(url)) {
      const request = fetch(url).then(res => {
        if (!res.ok) throw new Error(`Animation asset unavailable (${res.status}): ${url}`);
        return res.json();
      }).catch(error => { this.requests.delete(url); throw error; });
      this.requests.set(url, request);
    }
    return this.requests.get(url)! as Promise<T>;
  }
  catalog(): Promise<ModelEntry[]> { return this.json('/api/animation-models'); }
  async resolve(model: string): Promise<ModelEntry | undefined> {
    const key = model.toLowerCase();
    return (await this.catalog()).find(entry => entry.model === key || entry.assetStem === key);
  }
  async clips(model: string): Promise<ClipEntry[]> {
    const entry = await this.resolve(model);
    return entry ? this.json(`/api/animations/${encodeURIComponent(entry.model)}`) : [];
  }
  async clip(model: string, id: string): Promise<AnimationClipData> {
    const entry = await this.resolve(model);
    if (!entry) throw new Error(`No animation rig for ${model}.`);
    return this.json(`/api/animation/${encodeURIComponent(entry.model)}/${encodeURIComponent(id)}`);
  }
  async attach(model: string, container: AssetContainer, scene: Scene): Promise<SkeletalAnimator | null> {
    const entry = await this.resolve(model);
    if (!entry) return null; // Static .3DC props have no animation library entry.
    const [rig, clip] = await Promise.all([
      this.json<RigData>(`/api/skin/${encodeURIComponent(entry.assetStem)}`),
      entry.defaultClipId ? this.clip(entry.model, entry.defaultClipId) : Promise.resolve(null),
    ]);
    if (!container.meshes.length || container.meshes.some(mesh => mesh.isDisposed())) return null;
    const animator = new SkeletalAnimator(rig, container, scene);
    try {
      if (clip) animator.controller.setClip(clip, {startFrame: Math.min(1, clip.duration)});
      animator.apply();
      return animator;
    } catch (error) { animator.dispose(); throw error; }
  }
}
