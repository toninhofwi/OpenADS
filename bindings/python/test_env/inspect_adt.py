import struct, sys

sys.stdout.reconfigure(encoding='utf-8')

path = r'F:\OpenADS\testdata\pmsys\leases.adt'

with open(path, 'rb') as f:
    all_data = f.read()

# ADT header layout (from adt_driver.cpp):
# 0-14:  "Advantage Table" signature
# 24:    rec_count  (uint32 LE)
# 32:    hdr_len    (uint32 LE)
# 36:    rec_len    (uint32 LE)
sig = all_data[:15].decode('ascii', errors='replace')
rec_count = struct.unpack_from('<I', all_data, 24)[0]
hdr_len   = struct.unpack_from('<I', all_data, 32)[0]
rec_len   = struct.unpack_from('<I', all_data, 36)[0]

print(f'Signature: {sig!r}')
print(f'rec_count={rec_count}, hdr_len={hdr_len}, rec_len={rec_len}')

# Field descriptors: start at 400, each 200 bytes
# field[0..127]: name
# field[128]:    flags
# field[129]:    raw_type (uint16 LE)  -- but read_u16_le uses p[0]|p[1]<<8
# field[131]:    record_offset (uint16 LE)
# field[135]:    length (uint16 LE)
# field[137]:    decimals (uint16 LE)
num_fields = (hdr_len - 400) // 200

print(f'num_fields={num_fields}')
print()

fields = []
for fi in range(num_fields):
    fd_off = 400 + fi * 200
    fd = all_data[fd_off:fd_off+200]
    name = fd[:128].rstrip(b'\x00').decode('latin-1', errors='replace').rstrip()
    flags    = fd[128]
    raw_type = struct.unpack_from('<H', fd, 129)[0]
    rec_offset = struct.unpack_from('<H', fd, 131)[0]
    length     = struct.unpack_from('<H', fd, 135)[0]
    decimals   = struct.unpack_from('<H', fd, 137)[0]
    type_name = {1:'Logical',3:'Date',4:'Char',5:'Memo',6:'Binary',
                 10:'Double',11:'Integer',12:'ShortInt',13:'Time',
                 14:'Timestamp',15:'AutoInc',18:'Money',
                 20:'CIChar',21:'RowVersion',22:'ModTime'}.get(raw_type, f'?{raw_type}')
    print(f'  [{fi:2}] {name:20} type={raw_type:2}({type_name:12}) off={rec_offset:4} len={length:4} dec={decimals}')
    fields.append((name, raw_type, rec_offset, length))

print()
print('Money fields (type=18) raw values from first 5 records:')
for rec_num in range(min(5, rec_count)):
    rec_start = hdr_len + rec_num * rec_len
    rec = all_data[rec_start:rec_start+rec_len]
    deleted = rec[0]
    if deleted == 0x05:
        continue
    print(f'\n  Record {rec_num+1}:')
    for fname, ftype, foff, flen in fields:
        if ftype == 18:
            raw = rec[foff:foff+flen]
            as_double, = struct.unpack_from('<d', raw)
            as_int64,  = struct.unpack_from('<q', raw)
            scaled     = as_int64 / 10000.0
            print(f'    {fname:12}: hex={" ".join(f"{b:02x}" for b in raw)}')
            print(f'               as_ieee754_double = {as_double}')
            print(f'               as_int64/10000    = {scaled:.4f}')
