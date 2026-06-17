"""
Basic smoke tests for the OpenADS Python extension.
Run from the test_env directory so openace64.dll is on PATH:
    cd F:\OpenADS\bindings\python\test_env
    python ..\tests\test_openads.py
"""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'bin'))

import openads

DD_PATH  = r"F:\OpenADS\testdata\pmsys\pmsys_imported.add"
USER     = "adssys"
PASSWORD = "pmsys"

def test_import():
    assert hasattr(openads, 'AdsConnection')
    assert hasattr(openads, 'AdsStatement')
    assert hasattr(openads, 'AdsTable')
    assert hasattr(openads, 'AdsDictionary')
    assert hasattr(openads, 'AdsTransaction')
    assert hasattr(openads, 'AdsPreparedStatement')
    assert hasattr(openads, 'AdsError')
    print("PASS: module imports")

def test_connect():
    conn = openads.AdsConnection(DD_PATH,
                                  server_type=openads.ADS_LOCAL_SERVER,
                                  user=USER, password=PASSWORD)
    assert conn.is_alive()
    conn.close()
    print("PASS: connect/disconnect")

def test_query():
    with openads.AdsConnection(DD_PATH,
                                server_type=openads.ADS_LOCAL_SERVER,
                                user=USER, password=PASSWORD) as conn:
        stmt = conn.query("SELECT table_name FROM system.tables ORDER BY table_name")
        rows = stmt.fetch_all()
        assert len(rows) > 0
        stmt.close()
    print(f"PASS: query returned {len(rows)} rows")

def test_prepared():
    with openads.AdsConnection(DD_PATH,
                                server_type=openads.ADS_LOCAL_SERVER,
                                user=USER, password=PASSWORD) as conn:
        prep = conn.prepare("SELECT table_name FROM system.tables WHERE table_name = :tname")
        prep.bind(':tname', 'leases')
        stmt = prep.execute()
        rows = stmt.fetch_all()
        stmt.close()
    print(f"PASS: prepared statement, rows={len(rows)}")

def test_dictionary():
    with openads.AdsDictionary.open(DD_PATH,
                                     server_type=openads.ADS_LOCAL_SERVER,
                                     user=USER, password=PASSWORD) as d:
        name = d.get_database_property(1)  # ADS_DD_DATABASE_NAME = 1
        print(f"PASS: dictionary, DB name='{name}'")

if __name__ == '__main__':
    test_import()
    test_connect()
    test_query()
    test_prepared()
    test_dictionary()
    print("\nAll tests passed.")
