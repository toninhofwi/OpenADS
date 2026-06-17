#!/usr/bin/env python3
"""Patch trigger timing values in pmsys_imported.am JSON blocks.

Authoritative values from SAP system.triggers query:
  Delete AuditLog / properties: Trig_Trigger_Type=4 (AFTER)
  Insert AuditLog / properties: Trig_Trigger_Type=2 (INSTEAD OF)
  Update AuditLog / properties: Trig_Trigger_Type=1 (BEFORE)
  Update AuditLog / leases:     Trig_Trigger_Type=1 (BEFORE)
  Insert AuditLog / leases:     Trig_Trigger_Type=2 (INSTEAD OF)
  Create Recurrance / reminders: Trig_Trigger_Type=4 (AFTER)
"""
import struct, re, sys

ADD_PATH = r'F:\OpenADS\testdata\pmsys\pmsys_imported.add'
AM_PATH  = r'F:\OpenADS\testdata\pmsys\pmsys_imported.am'

TIMING = {
    ('properties', 'Delete AuditLog'):   4,
    ('properties', 'Insert AuditLog'):   2,
    ('properties', 'Update AuditLog'):   1,
    ('leases',     'Update AuditLog'):   1,
    ('leases',     'Insert AuditLog'):   2,
    ('reminders',  'Create Recurrance'): 4,
}

with open(ADD_PATH, 'rb') as f:
    add_data = bytearray(f.read())
with open(AM_PATH, 'rb') as f:
    am_data = bytearray(f.read())

hdr_len = struct.unpack_from('<I', add_data, 0x20)[0]
rec_len = struct.unpack_from('<I', add_data, 0x24)[0]
total   = (len(add_data) - hdr_len) // rec_len
print(f'hdr_len={hdr_len} rec_len={rec_len} total={total}')

# Build obj_id → name map from Table records
id_to_name = {}
for i in range(total):
    base     = hdr_len + i * rec_len
    status   = add_data[base]
    if status != 0x04:
        continue
    obj_type = add_data[base+13:base+23].rstrip(b'\x00 ').decode('latin1')
    if obj_type not in ('Table', 'ADSTable'):
        continue
    obj_id   = struct.unpack_from('<I', add_data, base+5)[0]
    obj_name = add_data[base+23:base+223].rstrip(b'\x00 ').decode('latin1')
    id_to_name[obj_id] = obj_name
print('Tables:', id_to_name)

patched = 0
for i in range(total):
    base     = hdr_len + i * rec_len
    status   = add_data[base]
    if status != 0x04:
        continue
    obj_type = add_data[base+13:base+23].rstrip(b'\x00 ').decode('latin1')
    if obj_type != 'Trigger':
        continue

    obj_name  = add_data[base+23:base+223].rstrip(b'\x00 ').decode('latin1')
    parent_id = struct.unpack_from('<I', add_data, base+9)[0]
    table_name = id_to_name.get(parent_id, '').lower()

    correct_timing = TIMING.get((table_name, obj_name))
    if correct_timing is None:
        print(f'  No timing entry for table={table_name!r} trigger={obj_name!r}')
        continue

    # Verify OpenADS JSON sentinel (property[0] == 0x08)
    plen = struct.unpack_from('<H', add_data, base+223)[0]
    if plen < 1 or add_data[base+225] != 0x08:
        print(f'  {obj_name!r}: not OpenADS JSON format (plen={plen})')
        continue

    am_block = struct.unpack_from('<I', add_data, base+498)[0]
    am_len   = struct.unpack_from('<I', add_data, base+502)[0]
    if am_block == 0 or am_len == 0:
        print(f'  {obj_name!r}: no .am block pointer')
        continue

    am_off = am_block * 8
    if am_off + am_len > len(am_data):
        print(f'  {obj_name!r}: .am block out of range (off={am_off} len={am_len})')
        continue

    json_text = am_data[am_off:am_off+am_len].decode('utf-8', errors='replace')
    new_json  = re.sub(r'"timing"\s*:\s*\d+', f'"timing":{correct_timing}', json_text)

    if new_json == json_text:
        print(f'  {table_name}::{obj_name}: no change (current JSON: {json_text[:80]!r})')
        continue

    new_bytes = new_json.encode('utf-8')
    if len(new_bytes) != am_len:
        print(f'  {obj_name!r}: length mismatch (old={am_len} new={len(new_bytes)}), cannot patch in-place')
        continue

    am_data[am_off:am_off+am_len] = new_bytes
    print(f'  PATCHED {table_name}::{obj_name}: timing={correct_timing}')
    patched += 1

print(f'\nPatched {patched} of {len(TIMING)} triggers')
if patched > 0:
    with open(AM_PATH, 'wb') as f:
        f.write(am_data)
    print(f'Wrote {AM_PATH}')
