#!/usr/bin/env python3
"""Extract sp_SaveIntoAuditLog body from pmsys_imported.am."""
import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

path = r'F:\OpenADS\testdata\pmsys\pmsys_imported.am'
with open(path, 'rb') as f:
    data = f.read()

idx = data.find(b'"name":"sp_SaveIntoAuditLog"')
start = data.rfind(b'{', 0, idx)

body_key = b'"body":"'
body_start = data.find(body_key, start) + len(body_key)
body_end_key = b'","procedure"'
body_end = data.find(body_end_key, body_start)

raw_body = data[body_start:body_end].decode('utf-8', errors='replace')
raw_body = raw_body.replace('\\r\\n', '\n').replace('\\n', '\n').replace('\\t', '\t').replace('\\"', '"')

print(f"body length: {len(raw_body)}")
print("=== BODY ===")
print(raw_body)
