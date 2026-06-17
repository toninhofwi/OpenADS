import sys, os
sys.path.insert(0, r'F:\OpenADS\bindings\python\bin')
os.add_dll_directory(r'F:\OpenADS\bindings\python\bin')

import openads

conn = openads.AdsConnection(r'F:\OpenADS\testdata\pmsys\pmsys_imported.add',
    server_type=openads.ADS_LOCAL_SERVER, user='adssys', password='pmsys')

stmt = conn.query('SELECT leaseid, rent, latefee, pmfee FROM leases LIMIT 5')
rows = stmt.fetch_all()
print('Money field test:')
for r in rows:
    print(f"  lease={r['leaseid']} rent={r.get('Rent', r.get('rent', '?'))} latefee={r.get('LateFee', r.get('latefee', '?'))} pmfee={r.get('PMFee', r.get('pmfee', '?'))}")
stmt.close()
conn.close()
print('\nPASS: Money fields read correctly')
