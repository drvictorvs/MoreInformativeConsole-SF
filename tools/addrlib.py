"""Address Library v5 lookup: id -> address, address -> id. Image base 0x140000000."""
import struct, sys
BASE = 0x140000000
class DB:
    def __init__(self, path):
        d = open(path, 'rb').read()
        fmt, = struct.unpack_from('<i', d, 0)
        ver = struct.unpack_from('<4I', d, 4)
        name = d[20:84].split(b'\0')[0].decode()
        ptr, dfmt, count = struct.unpack_from('<3i', d, 84)
        assert fmt == 5, fmt
        self.version, self.name, self.count = ver, name, count
        self.offsets = struct.unpack_from(f'<{count}I', d, 96)
        self.rev = {}
        for i, o in enumerate(self.offsets):
            if o: self.rev.setdefault(o, []).append(i)
    def addr(self, id_): o = self.offsets[id_] if id_ < self.count else 0; return BASE + o if o else None
    def ids(self, addr): return self.rev.get(addr - BASE, [])
if __name__ == '__main__':
    db = DB(sys.argv[1])
    print('version', '.'.join(map(str, db.version)), 'name', db.name, 'ids', db.count)
    for a in sys.argv[2:]:
        v = int(a, 16) if a.lower().startswith(('0x', '14')) and len(a) > 7 else int(a)
        if v >= BASE: print(f'{v:#x} -> ID {db.ids(v) or "none"}')
        else: print(f'ID {v} -> {hex(db.addr(v)) if db.addr(v) else "none"}')
