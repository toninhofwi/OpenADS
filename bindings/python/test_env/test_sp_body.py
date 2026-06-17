import sys, os

# SAP ACE DLL location
SAP_DLL_DIR = r'F:\Ads11'
SAP_PYD_DIR = r'F:\python_ads\bin'

sys.path.insert(0, SAP_PYD_DIR)
os.add_dll_directory(SAP_DLL_DIR)
os.add_dll_directory(SAP_PYD_DIR)

import ads

# Use SAP ADS with pmsys.add
conn = ads.AdsConnection(r'F:\OpenADS\testdata\pmsys\pmsys.add',
    server_type=ads.ADS_LOCAL_SERVER, user='adssys', password='pmsys')

stmt = conn.query("SELECT * FROM system.storedprocedures WHERE name='sp_ChargeLateFees'")
rows = stmt.fetch_all()
print("SAP system.storedprocedures for sp_ChargeLateFees:")
for r in rows:
    for k, v in r.items():
        print(f"  {k}: {repr(v)[:200]}")
stmt.close()

stmt2 = conn.query("SELECT * FROM system.storedprocedures WHERE name='BoY'")
rows2 = stmt2.fetch_all()
print("\nSAP system.storedprocedures for BoY:")
for r in rows2:
    for k, v in r.items():
        print(f"  {k}: {repr(v)[:200]}")
stmt2.close()

conn.close()
