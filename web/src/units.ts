/**
 * Engine units to render space, in one place.
 *
 * Project data stays in the game's own numbers: integer scene units with
 * negative Y up, and 12-bit headings where 4096 is a full turn. glTF files are
 * already in render space, written by `dreams.gltf.write` with this same rule,
 * so only positions read from project records pass through here.
 */
export type Vec3 = [number, number, number];

export const RENDER_SCALE = 0.01;

export function toRender(v: readonly number[]): Vec3 {
  return [v[0] * RENDER_SCALE, -v[1] * RENDER_SCALE, v[2] * RENDER_SCALE];
}

/** An axis-aligned engine box in render space. Negating Y swaps its Y bounds. */
export function boxToRender(min: readonly number[], max: readonly number[]): { min: Vec3; max: Vec3 } {
  const a = toRender(min), b = toRender(max);
  return {
    min: [Math.min(a[0], b[0]), Math.min(a[1], b[1]), Math.min(a[2], b[2])],
    max: [Math.max(a[0], b[0]), Math.max(a[1], b[1]), Math.max(a[2], b[2])],
  };
}

export function headingToYaw(heading: number): number {
  return (heading / 4096) * Math.PI * 2;
}

export function headingToDegrees(heading: number): number {
  return (heading / 4096) * 360;
}
