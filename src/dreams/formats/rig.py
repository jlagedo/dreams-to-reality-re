"""Named animation slots in the .DAN Tag 1 node directory.

Directory order is animation order; physical record order is not. Offsets in
the directory point at serialized headers, 20 bytes before runtime nodes.
Unlike the geometry scanner, this also retains nodes with zero vertices.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

from dreams.formats.node import address_delta, find_nodes


@dataclass(frozen=True)
class RigNode:
    slot: int
    name: str
    offset: int
    parent: int  # directory slot; -1 at a root
    mesh_index: int | None  # geometry scanner order, used by existing skin exports
    translation: tuple[int, int, int]
    rotation: tuple[tuple[int, int, int], ...]


def read_rig(buf: bytes) -> list[RigNode]:
    """Read a decompressed model payload, rejecting unresolved parent links."""
    if len(buf) < 24:
        raise ValueError("Truncated model node directory")
    count = struct.unpack_from("<I", buf, 20)[0]
    if not 0 < count <= (len(buf) - 24) // 4:
        raise ValueError("Invalid model node count")
    offsets = struct.unpack_from(f"<{count}I", buf, 24)
    if len(set(offsets)) != count or any(off + 0xF0 > len(buf) for off in offsets):
        raise ValueError("Invalid or duplicate model node offset")
    geometry = find_nodes(buf)
    if not geometry:
        raise ValueError("Cannot establish node address relocation without geometry")
    delta = address_delta(geometry)
    by_offset = {nd.offset: i for i, nd in enumerate(geometry)}
    by_address = {off + 20 - delta: i for i, off in enumerate(offsets)}
    result = []
    for slot, off in enumerate(offsets):
        parent_address = struct.unpack_from("<I", buf, off + 0x24)[0]
        if parent_address and parent_address not in by_address:
            raise ValueError(f"Slot {slot}: unresolved parent address {parent_address:#x}")
        result.append(
            RigNode(
                slot=slot,
                name=buf[off + 0x14 : off + 0x20].split(b"\0", 1)[0].decode("latin-1"),
                offset=off,
                parent=by_address[parent_address] if parent_address else -1,
                mesh_index=by_offset.get(off),
                translation=struct.unpack_from("<3i", buf, off + 0x30),
                rotation=tuple(
                    struct.unpack_from("<3i", buf, off + 0x3C + 12 * row) for row in range(3)
                ),
            )
        )
    for nd in result:
        seen = set()
        current = nd.slot
        while current != -1:
            if current in seen:
                raise ValueError(f"Cycle in model node hierarchy at slot {current}")
            seen.add(current)
            current = result[current].parent
    return result
