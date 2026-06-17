#!/usr/bin/env python3
"""Check which triggers are active in pmsys_imported.add binary."""
import sys, struct

path = r'F:\OpenADS\testdata\pmsys\pmsys_imported.add'
with open(path, 'rb') as f:
    data = f.read()

print(f"File size: {len(data)}")

# Header: find total records count at offset 0x18
total = struct.unpack_from('<I', data, 0x18)[0]
print(f"Total records: {total}")

# Each record is 524 bytes, header is 0x1b bytes (standard SAP .add)
# Actually find record length from header
# Standard SAP binary .add: header=27 bytes, record=524 bytes
hdr_len = 0x1b  # 27 bytes header
rec_len = 524

for i in range(total):
    base = hdr_len + i * rec_len
    if base + rec_len > len(data):
        break

    status = data[base]
    active = (status == 0x04)

    # obj_type at base+13, 10 chars
    obj_type = data[base+13:base+23].decode('ascii', errors='replace').rstrip()
    # obj_name at base+23, 200 chars
    obj_name = data[base+23:base+223].decode('ascii', errors='replace').rstrip()

    if obj_type == 'Trigger':
        mark = 'ACTIVE' if active else 'DELETED'
        print(f"  [{mark}] Trigger obj_name={repr(obj_name[:50])} status=0x{status:02x}")
