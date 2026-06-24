# iMac Connection Details

## Access
- **IP:** 192.168.18.184
- **SSH:** `ssh Anto@192.168.18.184` (port 22)
- **Password:** `1234`
- **Hostname:** antos-iMac.local
- **OS:** macOS Ventura (Darwin 22.6.0, x86_64)

## Build Tools
- **clang++:** Apple clang 14.0.3
- **make:** /usr/bin/make
- **cmake:** /Applications/CMake.app/Contents/bin/cmake (v3.28.3, installed manually)

## SSH Command Template
```powershell
# Quick connection
echo "1234" | ssh -o ConnectTimeout=10 Anto@192.168.18.184 "COMMAND"

# Interactive
ssh Anto@192.168.18.184
# Password: 1234
```

## OpenADS Server on iMac
```bash
# Clone and build
cd ~
git clone https://github.com/FiveTechSoft/OpenADS.git
cd OpenADS
mkdir -p build && cd build
/Applications/CMake.app/Contents/bin/cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)

# Start server
./tools/serverd/openads_serverd --host 0.0.0.0 --port 6262 --data ~/OpenADS/data
```

## Client Connection (from Windows)
```c
AdsConnect60("tcp://192.168.18.184:6262/path/to/data", ADS_REMOTE_SERVER, NULL, NULL, 0, &hConn);
```

## Network
- Windows machine WiFi: 192.168.18.11
- iMac WiFi: 192.168.18.184
- Subnet: 192.168.18.0/24
- Latency: ~5ms
