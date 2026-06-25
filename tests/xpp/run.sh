#!/usr/bin/env bash
# Build + run the Xbase++ ADSDBE smoke test, headless with a timeout.
# Usage: ./run.sh            -> uses whatever ace32.dll is present
#        ./run.sh openads    -> drop OpenADS in as ace32.dll first
#        ./run.sh real       -> drop Alaska's real ace32.dll first
set -u
cd "$(dirname "$0")"
XPP="C:/Program Files (x86)/Alaska Software/xpp20"
export PATH=".:$XPP/bin:$PATH"
export LIB="$XPP/lib"
export INCLUDE="$XPP/include"

case "${1:-}" in
  openads) cp ace32_openads.dll ace32.dll && echo "[dll] OpenADS openace32 -> ace32.dll" ;;
  real)    cp "$XPP/lib/ace32.dll" ace32.dll && echo "[dll] Alaska real -> ace32.dll" ;;
esac

rm -f test_ads.dbf test_ads.cdx test_ads.fpt test_result.log test_ads.obj test_ads.exe XPPERROR.LOG

echo "[build] compiling..."
xpp.exe test_ads.prg 2>&1 | grep -iE "error XBT|successfully compiled" | grep -v "XBT0024"
echo "[build] linking..."
alink.exe test_ads 2>&1 | grep -iE "error|fatal"
[ -f test_ads.exe ] || { echo "[build] FAILED (no exe)"; exit 1; }

echo "[run] launching (15s cap)..."
./test_ads.exe &
PID=$!
for i in $(seq 1 30); do
  kill -0 $PID 2>/dev/null || break
  sleep 0.5
done
if kill -0 $PID 2>/dev/null; then
  echo "[run] HUNG -> killing"
  taskkill //F //IM test_ads.exe >/dev/null 2>&1
fi

echo "=== test_result.log ==="
cat test_result.log 2>/dev/null
echo "=== XPPERROR.LOG ==="
[ -f XPPERROR.LOG ] && sed -n '3,28p' XPPERROR.LOG
echo "=== files ==="
ls -la test_ads.dbf test_ads.cdx 2>/dev/null
