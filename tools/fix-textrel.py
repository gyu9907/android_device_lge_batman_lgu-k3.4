#!/usr/bin/env python3
"""Reproduce PIC CRT fixups after proprietary-files stock hash verification.

This tool accepts only the exact pre-TEXTREL inputs in the adjacent manifest.
The liboemcamera input is the output of fix-camera-poll.py, not a stock hash.
It preserves __cxa_finalize(&__dso_handle), the original PLT branch and all
hardware code. No loader/SELinux exception or memory permission change is used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


class Elf:
    def __init__(self, data):
        self.data = data
        require(len(data) >= 52 and data[:7] == b'\x7fELF\x01\x01\x01',
                'Expected little-endian ELF32')
        self.e = self.unpack('<16sHHIIIIIHHHHHH', 0)
        e = self.e
        require(e[1] == 3 and e[2] == 40 and e[3] == 1, 'Expected ARM ET_DYN')
        require(e[8:10] == (52, 32) and e[11] == 40, 'Unsupported ELF header sizes')
        require(0 < e[10] < 128 and 0 < e[12] < 4096, 'Invalid table counts')
        self.ph = [self.unpack('<8I', e[5] + i * 32) for i in range(e[10])]
        self.sh = [self.unpack('<10I', e[6] + i * 40) for i in range(e[12])]
        self.loads = [p for p in self.ph if p[0] == 1]
        for p in self.loads:
            require(p[4] <= p[5] and p[2] + p[5] <= 2**32, 'Invalid LOAD size')
            self.span(p[1], p[4])
            require(p[6] & 3 != 3, 'Unexpected writable executable LOAD')
        dynamic = [p for p in self.ph if p[0] == 2]
        require(len(dynamic) == 1, 'Expected one PT_DYNAMIC')
        self.dp = dynamic[0]
        require(self.dp[4] % 8 == 0, 'Unaligned dynamic table')
        self.dynamic = []
        terminated = False
        for off in range(self.dp[1], self.dp[1] + self.dp[4], 8):
            tag, val = self.unpack('<2I', off)
            if tag == 0:
                terminated = True
                break
            self.dynamic.append((tag, val))
        require(terminated, 'Unterminated dynamic table')
        self.d = dict(self.dynamic)
        require(all(sum(t == k for t, _ in self.dynamic) == 1
                    for k in self.d if k != 1), 'Duplicate singleton dynamic tag')
        require(not any(t in self.d for t in (7, 8, 9, 0x6000000f, 0x60000010,
                    0x60000011, 0x60000012, 35, 36, 37)), 'Unsupported relocation table')
        require(self.d.get(19) == 8 and self.d.get(11) == 16, 'Unexpected REL/SYM entry size')
        require(self.d.get(20, 17) == 17, 'Expected REL PLT')
        require(all(t in self.d for t in (4, 5, 6, 10, 17, 18)), 'Missing ELF tables')
        self.strings = self.span(self.off(self.d[5], self.d[10]), self.d[10])
        _, count = self.unpack('<2I', self.off(self.d[4], 8))
        require(count <= 100000, 'Invalid symbol count')
        self.symbols = []
        for i in range(count):
            s = self.unpack('<IIIBBH', self.off(self.d[6] + i * 16, 16))
            self.symbols.append((self.string(s[0]),) + s[1:])
        self.rel_offset = self.off(self.d[17], self.d[18])
        require(self.d[18] % 8 == 0, 'Unaligned REL table')
        self.rels = [self.unpack('<2I', o) for o in
                     range(self.rel_offset, self.rel_offset + self.d[18], 8)]
        self.plt_rels = []
        if 23 in self.d:
            n = self.d.get(2, 0)
            require(n % 8 == 0, 'Unaligned PLT REL table')
            off = self.off(self.d[23], n)
            self.plt_rels = [self.unpack('<2I', o) for o in range(off, off + n, 8)]
        for addr, info in self.rels + self.plt_rels:
            require(info >> 8 < len(self.symbols), 'Invalid relocation symbol')
            require(addr % 4 == 0, 'Unaligned relocation target')
            self.segment(addr, 4)

    def span(self, offset, size):
        require(0 <= offset <= len(self.data) and 0 <= size <= len(self.data) - offset,
                'ELF file bounds exceeded')
        return self.data[offset:offset + size]

    def unpack(self, fmt, offset):
        return struct.unpack(fmt, self.span(offset, struct.calcsize(fmt)))

    def segment(self, addr, size):
        matches = [p for p in self.loads if p[2] <= addr and addr + size <= p[2] + p[5]]
        require(len(matches) == 1, 'Ambiguous/unmapped relocation address')
        return matches[0]

    def off(self, addr, size=4):
        p = self.segment(addr, size)
        require(addr + size <= p[2] + p[4], 'Address is not file backed')
        return p[1] + addr - p[2]

    def word(self, addr):
        return self.unpack('<I', self.off(addr))[0]

    def string(self, offset):
        require(offset < len(self.strings), 'Invalid string offset')
        end = self.strings.find(b'\0', offset)
        require(end >= 0, 'Unterminated symbol string')
        return bytes(self.strings[offset:end]).decode('ascii')

    def nonw(self):
        return [(a, i) for a, i in self.rels + self.plt_rels
                if not self.segment(a, 4)[6] & 2]


def arm_branch(elf, addr):
    word = elf.word(addr)
    require(word >> 24 == 0xea, 'Expected unconditional ARM B')
    offset = word & 0xffffff
    if offset & 0x800000:
        offset -= 1 << 24
    return (addr + 8 + 4 * offset) & 0xffffffff


def plt_slot(elf, addr):
    words = [elf.word(addr + i * 4) for i in range(3)]
    require(words[0] & 0xfffff000 == 0xe28fc000 and
            words[1] & 0xfffff000 == 0xe28cc000 and
            words[2] & 0xfffff000 == 0xe5bcf000, 'Unsupported ARM PLT sequence')
    def imm(word):
        value, shift = word & 255, ((word >> 8) & 15) * 2
        return ((value >> shift) | (value << (32 - shift))) & 0xffffffff if shift else value
    return (addr + 8 + imm(words[0]) + imm(words[1]) + (words[2] & 4095)) & 0xffffffff


def transform(data, rule):
    require(sha(data) == rule['input_sha256'], 'Unsupported pre-fixup SHA256')
    before = Elf(data)
    require(before.nonw() == [(rule['literal'], 23)], 'Unexpected non-writable relocations')
    literal, start = rule['literal'], rule['literal'] - 12
    words = [before.word(start + i * 4) for i in range(4)]
    require(words[:2] == [0xe28f0004, 0xe5900000], 'Unexpected CRT instructions')
    syms = {s[0]: s for s in before.symbols}
    require('__dso_handle' in syms and syms['__dso_handle'][1] == words[3],
            'CRT literal is not __dso_handle')
    if '__on_dlclose' in syms:
        require(syms['__on_dlclose'][1] == start, 'Wrong CRT function address')
    require(26 in before.d and before.d.get(28, 0) % 4 == 0, 'Missing fini array')
    hooks = [before.d[26] + i for i in range(0, before.d[28], 4)
             if before.word(before.d[26] + i) == start]
    require(len(hooks) == 1 and (hooks[0], 23) in before.rels, 'Unproven fini hook')
    slot = plt_slot(before, arm_branch(before, start + 8))
    calls = [(a, i) for a, i in before.plt_rels if a == slot]
    require(len(calls) == 1 and calls[0][1] & 255 == 22 and
            before.symbols[calls[0][1] >> 8][0] == '__cxa_finalize',
            'CRT branch does not resolve to __cxa_finalize')
    out = bytearray(data)
    code_off = before.off(start, 16)
    struct.pack_into('<2I', out, code_off, 0xe59f0004, 0xe08f0000)
    struct.pack_into('<I', out, code_off + 12, (words[3] - literal) & 0xffffffff)
    rels = list(before.rels)
    require(rels.count((literal, 23)) == 1, 'Duplicate CRT relocation')
    rels.remove((literal, 23))
    old_count = before.d.get(0x6ffffffa, 0)
    require(old_count <= len(before.rels) and all(i == 23 for _, i in before.rels[:old_count]),
            'Invalid original RELCOUNT')
    count = 0
    for _, info in rels:
        if info != 23:
            break
        count += 1
    rel_bytes = b''.join(struct.pack('<2I', *r) for r in rels) + b'\0' * 8
    out[before.rel_offset:before.rel_offset + before.d[18]] = rel_bytes
    dynamic = []
    for tag, value in before.dynamic:
        if tag == 22:
            continue
        if tag == 18:
            value -= 8
        elif tag == 30:
            value &= ~4
        elif tag == 0x6ffffffa:
            value = count
        dynamic.append((tag, value))
    packed = b''.join(struct.pack('<2I', *d) for d in dynamic)
    require(len(packed) + 8 <= before.dp[4], 'Dynamic table overflow')
    out[before.dp[1]:before.dp[1] + before.dp[4]] = packed + bytes(before.dp[4] - len(packed))
    sections = [i for i, s in enumerate(before.sh)
                if s[1] == 9 and s[4] == before.rel_offset and s[5] == before.d[18]]
    require(len(sections) == 1, 'Missing matching REL section')
    size_off = before.e[6] + sections[0] * 40 + 20
    struct.pack_into('<I', out, size_off, before.d[18] - 8)
    after = Elf(out)
    require(not after.nonw() and 22 not in after.d and not after.d.get(30, 0) & 4,
            'TEXTREL remains')
    require(before.e == after.e and before.ph == after.ph and
            before.symbols == after.symbols and before.strings == after.strings and
            after.rels == rels and before.plt_rels == after.plt_rels,
            'Unexpected ABI/layout change')
    allowed = [(code_off, code_off + 16), (before.rel_offset, before.rel_offset + before.d[18]),
               (before.dp[1], before.dp[1] + before.dp[4]), (size_off, size_off + 4)]
    require(len(data) == len(out) and all(a == b or any(lo <= i < hi for lo, hi in allowed)
            for i, (a, b) in enumerate(zip(data, out))), 'Unexpected byte changes')
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true', help='Validate original inputs and all outputs in memory')
    mode.add_argument('--apply', action='store_true', help='Transform a verified extraction staging tree')
    mode.add_argument('--verify', action='store_true', help='Verify already-derived outputs, never stock extraction')
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    manifest = json.loads(Path(__file__).with_name('textrel-fixups.json').read_text())
    require(manifest['version'] == 1, 'Unsupported manifest version')
    rules = manifest['libraries']
    require(len(rules) == 109 and len({r['path'] for r in rules}) == 109,
            'Incomplete or duplicate library manifest')
    root = args.root.resolve(strict=True)
    pending = []
    for rule in rules:
        rel = Path(rule['path'])
        require(not rel.is_absolute() and '..' not in rel.parts, 'Unsafe manifest path')
        path = root / rel
        require(os.path.commonpath((root, path.resolve(strict=True))) == str(root)
                and not path.is_symlink(), 'Unsafe input path')
        data = path.read_bytes()
        if args.verify:
            require(sha(data) == rule['output_sha256'], f'Output hash mismatch: {rel}')
            elf = Elf(data)
            require(not elf.nonw() and 22 not in elf.d and not elf.d.get(30, 0) & 4,
                    f'TEXTREL remains: {rel}')
        else:
            output = transform(data, rule)
            require(sha(output) == rule['output_sha256'], f'Unexpected output hash: {rel}')
            pending.append((path, output))
    # Validate the whole set before writing anything. Extraction invokes this
    # on its disposable staging tree, before replacing any working vendor file.
    if args.apply:
        for path, data in pending:
            name = None
            try:
                with tempfile.NamedTemporaryFile(dir=path.parent, prefix='.pic-', delete=False) as f:
                    name = f.name
                    os.fchmod(f.fileno(), path.stat().st_mode & 0o777)
                    f.write(data)
                    f.flush()
                    os.fsync(f.fileno())
                os.replace(name, path)
            finally:
                if name and os.path.exists(name):
                    os.unlink(name)
    print(f'Validated {len(rules)} PIC CRT libraries ({"verify" if args.verify else "apply" if args.apply else "check"})')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, KeyError, OSError, struct.error) as error:
        raise SystemExit(f'TEXTREL fixup failed: {error}')
