#!/usr/bin/env python3
"""Fix uninitialized optional pollfd.revents in the stock LG camera daemon blob.

mctl_pp_poll_fn (0x4fb34) polls 2-4 descriptors but always tests the optional
command slot at 0x4ff38. A stale POLLIN|POLLRDNORM there makes it read fd 0,
then block on the newly created c2d pipe before returning to poll. Initialize
all four revents at the existing loop entry. No camera command is bypassed.

The 26-byte Thumb trampoline occupies verified unused padding after the RX
segment, within its already mapped final page. Its original instruction and
all subsequent control flow are preserved. No ELF permission changes.
"""
import hashlib
import struct
import sys
from pathlib import Path

ORIGINAL = '63320afc9954342d086cd71d58cdb87890bd7d1df295c3345a35b509ae62e9f1'
PATCHED = '3fc7fdce757859b599281db932c86f1ed9105d173aa15f675c2d2fd4662dd0f3'

def fix(path):
    data = bytearray(path.read_bytes())
    digest = hashlib.sha256(data).hexdigest()
    if digest == PATCHED:
        return
    if digest != ORIGINAL:
        raise SystemExit(f'Unsupported liboemcamera.so SHA256: {digest}')
    assert data[0x4fb6c:0x4fb70] == bytes.fromhex('04f5803c')
    assert not any(data[0x9af74:0x9af8e])
    assert struct.unpack_from('<8I', data, 84) == (1, 0, 0, 0, 0x9af74, 0x9af74, 5, 4096)
    # b.w 0x9af74
    data[0x4fb6c:0x4fb70] = bytes.fromhex('4bf002ba')
    # movs r2,#0; strh.w r2,[sp,#74/82/90/98];
    # add.w ip,r4,#65536 (displaced instruction); b.w 0x4fb70
    data[0x9af74:0x9af8e] = bytes.fromhex(
        '0022adf84a20adf85220adf85a20adf8622004f5803cb4f7f1bd')
    struct.pack_into('<II', data, 100, 0x9af8e, 0x9af8e)
    assert hashlib.sha256(data).hexdigest() == PATCHED
    path.write_bytes(data)

if __name__ == '__main__':
    fix(Path(sys.argv[1]))
