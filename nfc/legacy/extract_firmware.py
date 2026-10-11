#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Extract PN544 firmware data without loading its obsolete ARM shared library."""
import hashlib
from pathlib import Path
import struct
import sys


def extract(data):
    if len(data) < 52 or data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected a little-endian ELF32 firmware container')
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if header[2] != 40 or header[11] != 40:
        raise ValueError('Expected ARM ELF32 section headers')
    shoff, count = header[6], header[12]
    if not count or shoff + count * 40 > len(data):
        raise ValueError('Invalid section table')
    sections = [struct.unpack_from('<10I', data, shoff + i * 40) for i in range(count)]

    def contents(section):
        offset, size = section[4:6]
        if offset + size > len(data):
            raise ValueError('Section exceeds file bounds')
        return data[offset:offset + size]

    result = {}
    names = {'nxp_nfc_fw', 'nxp_nfc_full_version'}
    for section in sections:
        if section[1] != 11:  # SHT_DYNSYM
            continue
        if section[9] != 16 or section[5] % 16 or section[6] >= count:
            raise ValueError('Invalid dynamic symbol table')
        strings = contents(sections[section[6]])
        for name, value, size, info, _, index in struct.iter_unpack('<IIIBBH', contents(section)):
            if name >= len(strings):
                raise ValueError('Invalid symbol name offset')
            symbol = strings[name:].split(b'\0', 1)[0].decode('ascii')
            if symbol not in names:
                continue
            if info & 15 != 1 or index >= count or not size or symbol in result:
                raise ValueError('Invalid or duplicate firmware data symbol')
            source = sections[index]
            offset = value - source[3]
            if source[1] != 1 or offset < 0 or offset + size > source[5]:
                raise ValueError('Firmware symbol exceeds its data section')
            for reloc in sections:
                if reloc[1] not in (4, 9):
                    continue
                entry_size = 12 if reloc[1] == 4 else 8
                if reloc[9] != entry_size or reloc[5] % entry_size:
                    raise ValueError('Invalid relocation table')
                entries = contents(reloc)
                for pos in range(0, len(entries), entry_size):
                    target, = struct.unpack_from('<I', entries, pos)
                    if value <= target < value + size:
                        raise ValueError('Firmware data requires relocation')
            result[symbol] = contents(source)[offset:offset + size]
    if result.keys() != names or len(result['nxp_nfc_full_version']) != 12:
        raise ValueError('Missing or invalid PN544 firmware data')
    return result


def main():
    source, destination = map(Path, sys.argv[1:])
    data = source.read_bytes()
    arrays = extract(data)
    output = ['/* Generated from the stock PN544 firmware; data only. */',
              '/* Source SHA-256: ' + hashlib.sha256(data).hexdigest() + ' */']
    for name in sorted(arrays):
        output.append('static const unsigned char pn544_' + name + '[] = {')
        value = arrays[name]
        for pos in range(0, len(value), 16):
            output.append('    ' + ', '.join('0x%02x' % x for x in value[pos:pos + 16]) + ',')
        output.append('};')
    destination.write_text('\n'.join(output) + '\n')


if __name__ == '__main__':
    main()
