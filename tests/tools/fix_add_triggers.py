#!/usr/bin/env python3
"""Mark all OpenADS Test Insert trigger records as deleted (0x05) in pmsys_imported.add."""
import struct, shutil, os

path = r'F:\OpenADS\testdata\pmsys\pmsys_imported.add'
shutil.copy2(path, path + '.bak2')

with open(path, 'r+b') as f:
    data = bytearray(f.read())

hdr_len = struct.unpack_from('<I', data, 0x20)[0]
rec_len = struct.unpack_from('<I', data, 0x24)[0]
total   = struct.unpack_from('<I', data, 0x18)[0]

patched = 0
for i in range(total):
    base = hdr_len + i * rec_len
    if base + rec_len > len(data):
        break
    status   = data[base]
    obj_type = bytes(data[base+13:base+23]).rstrip(b'\x00 ').decode('ascii', 'replace')
    obj_name = bytes(data[base+23:base+223]).rstrip(b'\x00 ').decode('ascii', 'replace')
    if obj_type == 'Trigger' and 'OpenADS Test' in obj_name and status == 0x04:
        print(f'  Patching record {i}: {repr(obj_name[:60])}')
        data[base] = 0x05
        patched += 1

with open(path, 'r+b') as f:
    f.write(data)

print(f'Done: patched {patched} records.')
