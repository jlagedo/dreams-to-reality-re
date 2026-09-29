"""Read guest memory from a recomp full-memory minidump (Memory64List stream).
Guest VA -> host address = arena base + VA; the base comes from the crash report."""

import bisect
import struct


class Dump:
    def __init__(self, path, arena):
        self.f = open(path, "rb")
        self.arena = arena
        sig, ver, nstreams, dir_rva = struct.unpack("<4sIII", self.f.read(16))
        assert sig == b"MDMP"
        self.f.seek(dir_rva)
        dirs = [struct.unpack("<III", self.f.read(12)) for _ in range(nstreams)]
        mem64 = [d for d in dirs if d[0] == 9]
        assert mem64, "no Memory64ListStream (not a full dump)"
        self.f.seek(mem64[0][2])
        n, base_rva = struct.unpack("<QQ", self.f.read(16))
        self.ranges = []
        rva = base_rva
        for _ in range(n):
            start, size = struct.unpack("<QQ", self.f.read(16))
            self.ranges.append((start, size, rva))
            rva += size
        self.starts = [r[0] for r in self.ranges]

    def host(self, addr, n):
        i = bisect.bisect_right(self.starts, addr) - 1
        if i < 0:
            return None
        start, size, rva = self.ranges[i]
        if addr + n > start + size:
            return None
        self.f.seek(rva + addr - start)
        return self.f.read(n)

    def read(self, va, n):
        return self.host(self.arena + va, n)

    def u32(self, va):
        b = self.read(va, 4)
        return None if b is None else struct.unpack("<I", b)[0]

    def dwords(self, va, n):
        b = self.read(va, 4 * n)
        return None if b is None else list(struct.unpack(f"<{n}I", b))
