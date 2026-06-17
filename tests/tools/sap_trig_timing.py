#!/usr/bin/env python3
"""Query SAP ADS for trigger properties by enumerating DD object names."""
import sys, struct
sys.path.insert(0, r'F:\python_ads\bin')
import ads

conn = ads.AdsConnection(
    r'e:\development\Openads\testdata\pmsys\pmsys.add',
    server_type=ads.ADS_REMOTE_SERVER,
    user='adssys',
    password='pmsys',
)
dd = ads.AdsDictionary.from_connection(conn)

# Try get_trigger_property with known names + guesses
names_to_try = [
    'Delete AuditLog',
    'properties::Delete AuditLog',
    'leases::Insert AuditLog',
    'leases::Update AuditLog',
    'Insert AuditLog',
    'Update AuditLog',
    'Create Recurrance',
    'reminders::Create Recurrance',
]

print("=== Trying trigger names ===")
for n in names_to_try:
    for prop_id, prop_name in [(1401,'event'),(1402,'timing'),(1404,'body')]:
        try:
            val = dd.get_trigger_property(n, prop_id)
            if prop_id in (1401,1402):
                v = struct.unpack('<I', val[:4])[0] if val and len(val)>=4 else repr(val)
            else:
                v = repr(val[:60]) if val else repr(val)
            print(f'  {n!r} prop={prop_id}: {v}')
        except ads.AdsError as e:
            if prop_id == 1401:  # only report name errors once per name
                code = str(e).split(']')[0].lstrip('[')
                print(f'  {n!r}: error {code}')
            break

dd.close()
conn.close()
