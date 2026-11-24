"""Small static-analysis helper for Starfield.exe (1.16.244): strings, RIP-relative xrefs,
function bounds from .pdata, and annotated disassembly (Address Library IDs + fork names)."""
import bisect
import os
import re
import struct
import sys

import numpy as np
import pefile
from capstone import CS_ARCH_X86, CS_MODE_64, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from addrlib import DB  # noqa: E402

EXE = 'Starfield.exe'  # the 1.16.244 executable, next to this script or give a path
LIB = 'versionlib-1-16-244-0.bin'
IDS_H = '../../commonlibsf/include/RE/IDs.h'  # CommonLibSF fork, for names


def fork_names():
    names, ns = {}, []
    if not os.path.exists(IDS_H):
        return names
    for line in open(IDS_H):
        m = re.match(r'\s*namespace (\w+)', line)
        if m:
            ns.append(m.group(1))
            continue
        if re.match(r'\s*}', line) and ns:
            ns.pop()
            continue
        m = re.search(r'REL::ID (\w+)\{ (\d+) \}', line)
        if m and int(m.group(2)):
            names.setdefault(int(m.group(2)), '::'.join(ns[1:] + [m.group(1)]))
    return names


class Exe:
    def __init__(self):
        self.pe = pefile.PE(EXE, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.get_memory_mapped_image()  # bytes laid out by RVA
        self.secs = {s.Name.rstrip(b'\0').decode(): s for s in self.pe.sections}
        self.db = DB(LIB)
        self.names = fork_names()
        self.cs = Cs(CS_ARCH_X86, CS_MODE_64)
        self.cs.detail = True
        # .pdata: RUNTIME_FUNCTION {begin, end, unwind} RVAs
        p = self.secs['.pdata']
        raw = self.data[p.VirtualAddress:p.VirtualAddress + p.Misc_VirtualSize]
        arr = np.frombuffer(raw[:len(raw) // 12 * 12], dtype='<u4').reshape(-1, 3)
        arr = arr[arr[:, 0] != 0]
        order = np.argsort(arr[:, 0])
        self.fbegin = arr[order, 0].astype(np.int64)
        self.fend = arr[order, 1].astype(np.int64)
        t = self.secs['.text']
        self.text_rva = t.VirtualAddress
        self.text = np.frombuffer(self.data[t.VirtualAddress:t.VirtualAddress + t.Misc_VirtualSize], dtype=np.uint8)

    # --- addresses -------------------------------------------------------------------------
    def va(self, rva): return self.base + rva
    def rva(self, va): return va - self.base

    def label(self, va):
        ids = self.db.ids(va)
        if not ids:
            return None
        named = [f'{self.names[i]}' for i in ids if i in self.names]
        return f"ID {ids[0]}" + (f" = {named[0]}" if named else '')

    def func_of(self, va):
        r = self.rva(va)
        i = bisect.bisect_right(self.fbegin, r) - 1
        if i >= 0 and self.fbegin[i] <= r < self.fend[i]:
            # walk back over chained (split) entries: a function chunk whose unwind is chained
            return self.va(int(self.fbegin[i])), self.va(int(self.fend[i]))
        return None

    # --- strings ---------------------------------------------------------------------------
    def find_strings(self, needle, exact=False):
        b = needle.encode()
        out = []
        r = self.secs['.rdata']
        blob = self.data[r.VirtualAddress:r.VirtualAddress + r.Misc_VirtualSize]
        for m in re.finditer(re.escape(b), blob):
            start = m.start()
            while start > 0 and 0x20 <= blob[start - 1] < 0x7f:
                start -= 1
            end = blob.index(b'\0', m.end())
            s = blob[start:end].decode('latin1')
            if exact and s != needle:
                continue
            out.append((self.va(r.VirtualAddress + start), s))
        return sorted(set(out))

    def cstr(self, va, maxlen=120):
        r = self.rva(va)
        if not (0 <= r < len(self.data)):
            return None
        end = self.data.find(b'\0', r, r + maxlen + 1)
        if end <= r:
            return None
        s = self.data[r:end]
        if all(0x20 <= c < 0x7f or c in (9, 10) for c in s) and len(s) >= 3:
            return s.decode()
        return None

    # --- xrefs -----------------------------------------------------------------------------
    def xrefs(self, target_va):
        """Candidate RIP-relative references (disp32 followed by 0, 1, 2 or 4 more bytes),
        confirmed by disassembling the containing function."""
        target = self.rva(target_va)
        n = len(self.text) - 8
        hits = set()
        for k in (0, 1, 2, 4):
            for shift in range(4):
                d = np.frombuffer(self.text[shift:shift + (n - shift) // 4 * 4].tobytes(), dtype='<i4').astype(np.int64)
                pos = shift + np.arange(len(d), dtype=np.int64) * 4 + self.text_rva
                idx = np.nonzero(pos + 4 + k + d == target)[0]
                for p in pos[idx]:
                    hits.add(int(p))
        confirmed = []
        for p in sorted(hits):
            f = self.func_of(self.va(p))
            if not f:
                continue
            for ins in self.disasm_range(*f):
                if ins.address <= self.va(p) < ins.address + ins.size and self.mem_target(ins) == target_va:
                    confirmed.append((ins.address, f[0]))
        return sorted(set(confirmed))

    def callers(self, target_va):
        """Direct CALL/JMP rel32 to target."""
        target = self.rva(target_va)
        out = []
        for opc in (0xE8, 0xE9):
            idx = np.nonzero(self.text[:-5] == opc)[0]
            d = np.frombuffer(self.text.tobytes(), dtype=np.uint8)
            disp = (d[idx + 1].astype(np.int64) | (d[idx + 2].astype(np.int64) << 8) |
                    (d[idx + 3].astype(np.int64) << 16) | (d[idx + 4].astype(np.int64) << 24))
            disp = np.where(disp >= 2**31, disp - 2**32, disp)
            dest = idx + self.text_rva + 5 + disp
            for i in np.nonzero(dest == target)[0]:
                at = self.va(int(idx[i]) + self.text_rva)
                f = self.func_of(at)
                if f:  # confirm the byte is an instruction start
                    if any(ins.address == at for ins in self.disasm_range(*f)):
                        out.append((at, f[0], 'call' if opc == 0xE8 else 'jmp'))
        return out

    # --- disassembly -----------------------------------------------------------------------
    def disasm_range(self, start, end):
        r0, r1 = self.rva(start), self.rva(end)
        return list(self.cs.disasm(self.data[r0:r1], start))

    def mem_target(self, ins):
        for op in ins.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                return ins.address + ins.size + op.mem.disp
        return None

    def annotate(self, ins):
        notes = []
        t = self.mem_target(ins)
        if t is None and ins.mnemonic in ('call', 'jmp') and ins.operands and ins.operands[0].type == X86_OP_IMM:
            t = ins.operands[0].imm
        if t is not None:
            lab = self.label(t)
            s = self.cstr(t)
            if lab: notes.append(lab)
            if s: notes.append(repr(s))
            if not lab and not s: notes.append(hex(t))
        return '  ; ' + ', '.join(notes) if notes else ''

    def show(self, va, limit=400):
        f = self.func_of(va)
        if not f:
            print('no function at', hex(va)); return
        lab = self.label(f[0])
        print(f'== function {f[0]:#x}-{f[1]:#x} ({lab or "no ID"})')
        for ins in self.disasm_range(*f)[:limit]:
            print(f'{ins.address:x}  {ins.mnemonic:8} {ins.op_str}{self.annotate(ins)}')
