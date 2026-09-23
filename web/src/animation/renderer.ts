import {
  AssetContainer, Color4, LinesMesh, Mesh, MeshBuilder, Scene, TransformNode, Vector3, VertexBuffer, VertexData,
} from '@babylonjs/core';
import { AnimationController, evaluateWorldTransforms, matApply } from './controller.js';
import type { RigData, WorldTransform } from './types.js';

export function detachContainerRoots(container: AssetContainer): void {
  const owned = new Set<TransformNode>([...container.meshes, ...container.transformNodes]);
  for (const node of owned) if (node.parent && !owned.has(node.parent as TransformNode)) node.parent = null;
}

/** CPU deformation for the game's one-node-per-vertex mesh format. Owns no model meshes. */
export class SkeletalAnimator {
  public readonly controller: AnimationController;
  public disposed = false;
  private bones: LinesMesh | null = null;
  private bonesVisible = false;
  private meshes: {mesh: Mesh; bindings: number[][]; positions: Float32Array; normals: Float32Array}[];

  constructor(public readonly rig: RigData, private container: AssetContainer, private scene: Scene) {
    this.controller = new AnimationController(rig);
    this.meshes = Object.entries(rig.primitives).map(([name, bindings]) => {
      const matches = container.meshes.filter(m => m instanceof Mesh && m.name === name) as Mesh[];
      if (matches.length !== 1 || matches[0].getTotalVertices() !== bindings.length)
        throw new Error(`Geometry does not match animation bindings: ${name}. Re-export the model.`);
      if (bindings.some(([slot]) => !rig.nodes[slot])) throw new Error('Vertex references a missing bone.');
      return {mesh: matches[0], bindings, positions: new Float32Array(bindings.length * 3),
        normals: new Float32Array(bindings.length * 3)};
    });
    for (const {mesh} of this.meshes) {
      mesh.makeGeometryUnique();
      mesh.markVerticesDataAsUpdatable(VertexBuffer.PositionKind, true);
      mesh.markVerticesDataAsUpdatable(VertexBuffer.NormalKind, true);
    }
    this.apply();
  }

  update(seconds: number): void {
    if (this.disposed) return;
    this.controller.update(seconds);
    this.apply();
  }

  apply(): void {
    if (this.disposed) return;
    const world = evaluateWorldTransforms(this.rig, this.controller.pose(), this.controller.order);
    for (const {mesh, bindings, positions, normals} of this.meshes) {
      if (mesh.isDisposed()) continue;
      bindings.forEach(([slot, x, y, z], i) => {
        const {rot, tr} = world[slot];
        const v = matApply(rot, [x, y, z]);
        positions[i*3] = (v[0]+tr[0])*0.01;
        positions[i*3+1] = -(v[1]+tr[1])*0.01;
        positions[i*3+2] = (v[2]+tr[2])*0.01;
      });
      mesh.updateVerticesData(VertexBuffer.PositionKind, positions, true);
      const indices = mesh.getIndices();
      if (indices) {
        VertexData.ComputeNormals(positions, indices, normals);
        if (mesh.isVerticesDataPresent(VertexBuffer.NormalKind))
          mesh.updateVerticesData(VertexBuffer.NormalKind, normals);
        else mesh.setVerticesData(VertexBuffer.NormalKind, normals, true);
      }
    }
    if (this.bonesVisible) this.drawBones(world);
  }

  showBones(show: boolean): void {
    if (this.disposed) return;
    this.bonesVisible = show;
    this.bones?.setEnabled(show);
    if (show) this.apply();
  }

  private drawBones(world: WorldTransform[]): void {
    const points = world.map(({tr}) => new Vector3(tr[0]*0.01, -tr[1]*0.01, tr[2]*0.01));
    const lines: Vector3[][] = [], colors: Color4[][] = [];
    for (const node of this.rig.nodes) {
      if (node.parent < 0 || /^Z{3,}/i.test(node.name)) continue;
      lines.push([points[node.parent], points[node.index]]);
      const color = /^nate?\d/i.test(node.name) ? new Color4(1,0.3,0.7,1)
        : node.hasGeometry ? new Color4(0,0.94,1,1) : new Color4(1,0.8,0.2,1);
      colors.push([color, color]);
    }
    // Facing is established for Duncan's source mesh. Other rigs need their own evidence.
    const torso = this.rig.model === 'xh_' ? this.rig.nodes.find(n => n.name === 'torse') : null;
    if (torso) {
      const {rot, tr} = world[torso.index], offset = matApply(rot, [60,0,0]);
      lines.push([points[torso.index], new Vector3((tr[0]+offset[0])*0.01,
        -(tr[1]+offset[1])*0.01, (tr[2]+offset[2])*0.01)]);
      const color = new Color4(1,0.65,0.1,1); colors.push([color,color]);
    }
    if (!lines.length) return;
    if (this.bones) MeshBuilder.CreateLineSystem('rig_bones', {lines, colors, instance:this.bones}, this.scene);
    else {
      this.bones = MeshBuilder.CreateLineSystem('rig_bones', {lines, colors, updatable:true}, this.scene);
      this.bones.parent = this.container.meshes.find(m => m.name === '__root__') ?? this.meshes[0]?.mesh.parent ?? null;
      this.bones.isPickable = false; this.bones.renderingGroupId = 2;
    }
  }

  dispose(): void {
    if (this.disposed) return;
    this.disposed = true;
    this.bones?.dispose(); this.bones = null;
    this.controller.dispose(); this.meshes = [];
  }
}
