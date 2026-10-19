#!/bin/bash
# Run the four Atomic-contract probes against the tree that contains this script.
#
# The probes link nothing but libcudart: they call
# PlatformTraits<IluvatarPlatform>::Atomic directly, so what is measured is the
# production code in iluvatar_platform_traits.h, not a copy of it.
#
# Logs, assembly and IR are written under OUT and are deliberately not part of
# the source tree.
#
#   REPO   repo root          (default: four levels up from this script)
#   OUT    output directory   (default: /tmp/atomic-contract)
#   CLANG  CoreX clang        (default: /usr/local/corex/bin/clang)
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=${REPO:-$(cd "$HERE/../../../.." && pwd)}
OUT=${OUT:-/tmp/atomic-contract}
CLANG=${CLANG:-/usr/local/corex/bin/clang}
export CUDA_VISIBLE_DEVICES=${CUDA_VISIBLE_DEVICES:-1,2}
ulimit -c 0 2>/dev/null

INC="-I$REPO/flagcx/adaptor/include/device_api -I$REPO/flagcx/adaptor/include \
     -I$REPO/flagcx/include -I$REPO/bindings/ir -I$REPO/test/kernel/include"
FLAGS="-x ivcore --cuda-path=/usr/local/corex --cuda-gpu-arch=ivcore11 -std=c++17 -O2 \
       -DUSE_ILUVATAR_ADAPTOR=1 -DFLAGCX_ILUVATAR_DEVICE_COMPILE=1"
LNK="-L/usr/local/corex/lib64 -Wl,-rpath,/usr/local/corex/lib64 -lcudart"

mkdir -p "$OUT" && cd "$OUT" || exit 1
exec > "$OUT/harness.log" 2>&1

echo "===== identity ====="
echo "repo   : $REPO"
echo "script : $HERE"
echo "traits : $(sha256sum "$REPO/flagcx/adaptor/include/device_api/iluvatar_platform_traits.h" | cut -d' ' -f1)"
echo "clang  : $($CLANG --version | head -1)"
for f in order_scope_compile.py load_store_order.cu rmw_atomicity.cu \
         value_contract.cu value_contract_64bit_rmw_must_be_rejected.cu; do
  echo "src    : $(sha256sum "$HERE/$f" | cut -d' ' -f1)  $f"
done

# ------------------------------------------------------- 1) compile-time check
echo
echo "########## order-scope-compile (compile-time) ##########"
# The generator creates its own output directory, but the shell creates the
# redirect target first, so the directory has to exist before this line.
COMPILE_OUT="$OUT/order-scope-compile"
COMPILE_LOG="$COMPILE_OUT/order_scope_compile_run1.log"
mkdir -p "$COMPILE_OUT"
OUT="$COMPILE_OUT" REPO="$REPO" CLANG="$CLANG" \
  timeout 900 python3 "$HERE/order_scope_compile.py" > "$COMPILE_LOG" 2>&1
echo "rc=$?"
grep -aE "PROBE\||kernels found|PREDICTION|distinct groups" "$COMPILE_LOG"

# ------------------------------------------------------------ 2) load / store
echo
echo "########## load-store-order (runtime) ##########"
$CLANG $FLAGS $INC -o load_store_order "$HERE/load_store_order.cu" $LNK 2>&1 \
  | grep -v "not unrolled" | head -5
if [ -x ./load_store_order ]; then
  for r in 1 2 3; do
    echo "--- run $r ---"
    timeout -s KILL 600 ./load_store_order > "load_store_order_run$r.log" 2>&1
    echo "rc=$?"
    grep -aE "PROBE\||DONE" "load_store_order_run$r.log"
    [ -x /tmp/devcheck ] && timeout -s KILL 60 /tmp/devcheck 2>&1 | tail -1
  done
  for r in 1 2 3; do
    grep -ah "PROBE|" "load_store_order_run$r.log" | sed 's/=-\?[0-9]*/=N/g' > "ls_v$r.txt"
  done
  if diff -q ls_v1.txt ls_v2.txt >/dev/null && diff -q ls_v1.txt ls_v3.txt >/dev/null; then
    echo "verdicts identical across 3 runs"
  else
    echo "VERDICT DIFFERS"; diff ls_v1.txt ls_v2.txt || true
  fi
  echo "MISMATCH per run: $(for r in 1 2 3; do printf '%s ' "$(grep -ac 'MISMATCH$' "load_store_order_run$r.log")"; done)"
else
  echo "NO BINARY"
fi

# ------------------------------------------------------------ 3) RMW atomicity
echo
echo "########## rmw-atomicity (runtime) ##########"
$CLANG $FLAGS $INC -o rmw_atomicity "$HERE/rmw_atomicity.cu" $LNK 2>&1 \
  | grep -v "not unrolled" | head -5
if [ -x ./rmw_atomicity ]; then
  for r in 1 2 3; do
    echo "--- run $r ---"
    timeout -s KILL 300 ./rmw_atomicity > "rmw_atomicity_run$r.log" 2>&1
    echo "rc=$?"
    grep -aE "PROBE\||DONE" "rmw_atomicity_run$r.log"
    [ -x /tmp/devcheck ] && timeout -s KILL 60 /tmp/devcheck 2>&1 | tail -1
  done
  # NEG is a genuine race and may move; every other cell must be bit-identical.
  for r in 1 2 3; do
    grep -ah "PROBE|" "rmw_atomicity_run$r.log" | grep -v "NEG(" > "ra_v$r.txt"
  done
  if diff -q ra_v1.txt ra_v2.txt >/dev/null && diff -q ra_v1.txt ra_v3.txt >/dev/null; then
    echo "non-NEG cells bit-identical across 3 runs"
  else
    echo "DIFFERS"; diff ra_v1.txt ra_v2.txt || true
  fi
  echo "VIOLATION per run: $(for r in 1 2 3; do printf '%s ' "$(grep -ac 'VIOLATION$' "rmw_atomicity_run$r.log")"; done)"
else
  echo "NO BINARY"
fi

# ------------------------------------------------------------ 4) value contract
echo
echo "########## value-contract (runtime + negative compile) ##########"
echo "--- 64-bit RMW must be rejected: expect NON-zero rc and 6 messages ---"
$CLANG $FLAGS $INC -S --cuda-device-only -o /dev/null \
  "$HERE/value_contract_64bit_rmw_must_be_rejected.cu" \
  > value_contract_64bit_rmw_must_be_rejected.log 2>&1
negrc=$?
nmsg=$(grep -ac "CoreX RMW operations require a 32-bit operand" \
        value_contract_64bit_rmw_must_be_rejected.log)
echo "rc=$negrc messages=$nmsg"
if [ "$negrc" -ne 0 ] && [ "$nmsg" -eq 6 ]; then
  echo "PROBE|value-contract|64bit-rmw-rejected-at-compile-time|want=6|got=$nmsg|OK"
else
  echo "PROBE|value-contract|64bit-rmw-rejected-at-compile-time|want=6|got=$nmsg|MISMATCH"
fi
echo "--- value observations ---"
$CLANG $FLAGS $INC -o value_contract "$HERE/value_contract.cu" $LNK 2>&1 | head -5
if [ -x ./value_contract ]; then
  for r in 1 2 3; do
    echo "--- run $r ---"
    timeout -s KILL 300 ./value_contract > "value_contract_run$r.log" 2>&1
    echo "rc=$?"
    grep -aE "PROBE\||mismatches" "value_contract_run$r.log"
    [ -x /tmp/devcheck ] && timeout -s KILL 60 /tmp/devcheck 2>&1 | tail -1
  done
  for r in 1 2 3; do grep -ah "PROBE|" "value_contract_run$r.log" > "vc_v$r.txt"; done
  if diff -q vc_v1.txt vc_v2.txt >/dev/null && diff -q vc_v1.txt vc_v3.txt >/dev/null; then
    echo "value observations bit-identical across 3 runs"
  else
    echo "DIFFERS"; diff vc_v1.txt vc_v2.txt || true
  fi
  echo "MISMATCH per run: $(for r in 1 2 3; do printf '%s ' "$(grep -ac 'MISMATCH$' "value_contract_run$r.log")"; done)"
else
  echo "NO BINARY"
fi

# ------------------------------------------------------------------ roll-up
echo
echo "########## counts ##########"
# Count only the per-run logs. harness.log holds the same lines again, so a
# glob over *.log would count everything twice.
LOGS="load_store_order_run1.log load_store_order_run2.log load_store_order_run3.log
      rmw_atomicity_run1.log rmw_atomicity_run2.log rmw_atomicity_run3.log
      value_contract_run1.log value_contract_run2.log value_contract_run3.log
      order-scope-compile/order_scope_compile_run1.log"
echo "OK        : $(cat $LOGS 2>/dev/null | grep -o '|OK$' | wc -l)"
echo "MISMATCH  : $(cat $LOGS 2>/dev/null | grep -o 'MISMATCH$' | wc -l)"
echo "VIOLATION : $(cat $LOGS 2>/dev/null | grep -o 'VIOLATION$' | wc -l)"
echo
echo "logs under: $OUT"
echo ALL_DONE
