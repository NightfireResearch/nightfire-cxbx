"""Program bytes, read once through the read-only client and cached in data/image/.

Both images are fixed (the disc's executables), so the cache never goes stale; delete data/image/ to refetch.
"""

import os

from lib import ghidra_ro as g

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SECTIONS = {
    g.XBOX: {".text": (0x11000, 0x15d370), ".rdata": (0x189be0, 0x1b3d98), ".data": (0x1b3da0, 0x24b5bc)},
    g.PS2: {".text": (0x107100, 0x3247fc), ".data": (0x326000, 0x363148), ".rodata": (0x363180, 0x3c7c58)},
}

CHUNK = 0x40000


class Image:
    def __init__(self, program):
        self.program = program
        self.sections = {}
        folder = os.path.join(HERE, "data", "image", program)
        os.makedirs(folder, exist_ok=True)
        for name, (lo, hi) in SECTIONS[program].items():
            path = os.path.join(folder, name.strip(".") + ".bin")
            if not os.path.exists(path) or os.path.getsize(path) != hi - lo:
                data = bytearray()
                for a in range(lo, hi, CHUNK):
                    n = min(CHUNK, hi - a)
                    data += bytes.fromhex(g.get_json("read_memory", program=program, address=f"0x{a:x}", length=n)["hex"])
                with open(path, "wb") as f:
                    f.write(data)
            with open(path, "rb") as f:
                self.sections[name] = (lo, f.read())

    def read(self, address, length):
        for lo, data in self.sections.values():
            if lo <= address and address + length <= lo + len(data):
                return data[address - lo:address - lo + length]
        raise ValueError(f"0x{address:x} is not in a cached section of {self.program}")

    def u32(self, address):
        return int.from_bytes(self.read(address, 4), "little")

    def in_section(self, name, address):
        lo, data = self.sections[name]
        return lo <= address < lo + len(data)
