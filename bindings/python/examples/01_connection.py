"""
Example 01 — Connect to an OpenADS database dictionary and run a query.

Usage (run from the test_env directory so openace64.dll is on PATH):
    cd F:\OpenADS\bindings\python\test_env
    python ..\examples\01_connection.py
"""

import sys
import os

# Add bin dir so Python finds openads.pyd
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'bin'))

import openads

DD_PATH  = r"F:\OpenADS\testdata\pmsys\pmsys_imported.add"
USER     = "adssys"
PASSWORD = "pmsys"

print(f"Connecting to {DD_PATH} ...")

with openads.AdsConnection(
        DD_PATH,
        server_type=openads.ADS_LOCAL_SERVER,
        user=USER,
        password=PASSWORD) as conn:

    print(f"Connected: {conn}")

    stmt = conn.query("SELECT * FROM system.tables ORDER BY table_name")
    print(f"\nTables in DD:")
    for row in stmt:
        print(f"  {row.get('table_name', row)}")
    stmt.close()

print("Done.")
