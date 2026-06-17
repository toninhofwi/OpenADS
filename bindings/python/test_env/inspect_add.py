import struct, sys

sys.stdout.reconfigure(encoding='utf-8')

def le32(data, off): return struct.unpack_from('<I', data, off)[0]
def le16(data, off): return struct.unpack_from('<H', data, off)[0]

def inspect_sp(path, sp_name, sp_type='proc'):
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except Exception as e:
        print(f"  Cannot read {path}: {e}")
        return

    hdrLen = le32(data, 0x20)
    recLen = le32(data, 0x24)
    total = (len(data) - hdrLen) // recLen

    for i in range(total):
        base = hdrLen + i * recLen
        if base + recLen > len(data): break
        if data[base] != 0x04: continue
        objType = data[base+13:base+23].rstrip(b' \x00').decode('ascii', errors='replace')
        objName = data[base+23:base+223].rstrip(b' \x00').decode('ascii', errors='replace')
        if sp_type == 'proc' and objType not in ('StoredProc', 'Procedure'): continue
        if sp_type == 'function' and objType != 'Function': continue
        if objName.lower() != sp_name.lower(): continue

        plen = le16(data, base + 223)
        PS = base + 225
        PL = 273

        prop = bytes(data[PS:PS+PL])
        print(f"  objType={objType!r}, objName={objName!r}")
        print(f"  plen={plen:#x} ({plen}), propNull={plen==0xFFFF}")
        print(f"  Full property area ({PL} bytes):")

        # Show in 16-byte rows
        for row in range(0, PL, 16):
            chunk = prop[row:row+16]
            hex_part = ' '.join(f'{b:02x}' for b in chunk)
            ascii_part = ''.join(chr(b) if 0x20 <= b <= 0x7e else '.' for b in chunk)
            print(f"    {row:3d}: {hex_part:<48}  {ascii_part}")

        # more_property at base+498
        mp = data[base+498:base+507]
        amBlock = struct.unpack_from('<I', mp, 0)[0]
        amLen   = struct.unpack_from('<I', mp, 4)[0]
        print(f"  amBlock={amBlock}, amLen={amLen}")

        amPath = path.replace('.add', '.am')
        if amBlock > 0:
            try:
                with open(amPath, 'rb') as f:
                    f.seek(amBlock * 8)
                    amData = f.read(min(amLen + 20, 400))
                print(f"  .am data (first 200): {repr(amData[:200])}")
            except Exception as e:
                print(f"  .am not available: {e}")
        return

    print(f"  NOT FOUND: {sp_name!r}")

print("=== sp_ChargeLateFees in pmsys_imported.add ===")
inspect_sp(r'F:\OpenADS\testdata\pmsys\pmsys_imported.add', 'sp_ChargeLateFees', 'proc')

print("\n=== BoY function in pmsys_imported.add ===")
inspect_sp(r'F:\OpenADS\testdata\pmsys\pmsys_imported.add', 'BoY', 'function')

# Also check a simpler proc if any exists
print("\n=== All procs/functions in pmsys_imported.add ===")
with open(r'F:\OpenADS\testdata\pmsys\pmsys_imported.add', 'rb') as f:
    data = f.read()
hdrLen = le32(data, 0x20)
recLen = le32(data, 0x24)
total = (len(data) - hdrLen) // recLen
for i in range(total):
    base = hdrLen + i * recLen
    if base + recLen > len(data): break
    if data[base] != 0x04: continue
    objType = data[base+13:base+23].rstrip(b' \x00').decode('ascii', errors='replace')
    if objType in ('StoredProc', 'Procedure', 'Function'):
        objName = data[base+23:base+223].rstrip(b' \x00').decode('ascii', errors='replace')
        plen = le16(data, base + 223)
        amBlock = struct.unpack_from('<I', data, base+498)[0]
        print(f"  {objType:12} {objName:30} plen={plen:#x:6} amBlock={amBlock}")
