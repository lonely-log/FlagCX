#!/bin/bash
#
# Run every FlagCX collective correctness test one interface at a time and
# print a one-row-per-interface result table.
#
# Each interface gets its own log file ($LOG_DIR/<Interface>.log) holding the
# complete, unfiltered output of that run, so a FAIL row can be investigated
# without re-running anything.
#
# Usage:
#   ./run_coll_matrix.sh [np]        # np defaults to 2 (single-node dual card)
#
# Environment overrides:
#   PER_RANK=1   also write per-rank stdout files via OpenMPI
#                --output-filename ($LOG_DIR/<Interface>.ranks/1/rank.N/stdout)
#   TIMEOUT=600  per-interface timeout in seconds (default 300)

set -u

# ---- configuration --------------------------------------------------------
ROOT=${ROOT:-/usr/local/corex-4.5.0.20260804/FlagCX}
BIN=$ROOT/test/unittest/runner/build/bin/runner_mpi_tests
LOG_DIR=${LOG_DIR:-$ROOT/test/unittest/runner/coll_logs}

NP=${1:-2}
VIS="-x CUDA_VISIBLE_DEVICES=1,2"
TIMEOUT=${TIMEOUT:-300}

# One entry per TEST_F in test/unittest/runner/coll_*.cpp.
INTERFACES=(
  AllReduce
  AllGather
  AlltoAll
  AlltoAllv
  Broadcast
  Gather
  Reduce
  ReduceScatter
  Scatter
  SendRecv
)
# ---------------------------------------------------------------------------

export LD_LIBRARY_PATH=/usr/local/openmpi/lib:${LD_LIBRARY_PATH:-}

if [ ! -x "$BIN" ]; then
  echo "error: $BIN not found" >&2
  echo "build it first:" >&2
  echo "  make -C $ROOT/test/unittest/runner USE_ILUVATAR_COREX=1 -j\$(nproc)" >&2
  exit 1
fi

mkdir -p "$LOG_DIR"

printf '%-14s %-9s %-6s %s\n' "Interface" "Result" "Time" "Log"
printf '%s\n' "------------------------------------------------------------------"

pass=0
fail=0
failed_list=""

for iface in "${INTERFACES[@]}"; do
  log="$LOG_DIR/$iface.log"

  out_arg=""
  if [ "${PER_RANK:-0}" = "1" ]; then
    out_arg="--output-filename $LOG_DIR/$iface.ranks"
  fi

  t0=$(date +%s)
  timeout "$TIMEOUT" mpirun --allow-run-as-root -np "$NP" $VIS $out_arg \
    "$BIN" --gtest_filter="FlagCXCollTest.$iface" >"$log" 2>&1
  rc=$?
  t1=$(date +%s)

  case $rc in
    0)   result="PASS" ;;
    124) result="TIMEOUT"; fail=$((fail + 1)); failed_list="$failed_list $iface" ;;
    *)   result="FAIL";    fail=$((fail + 1)); failed_list="$failed_list $iface" ;;
  esac
  [ "$result" = "PASS" ] && pass=$((pass + 1))

  printf '%-14s %-9s %-6s %s\n' "$iface" "$result" "$((t1 - t0))s" "$log"
done

printf '%s\n' "------------------------------------------------------------------"
printf 'PASS=%d  FAIL=%d  (np=%d)\n' "$pass" "$fail" "$NP"

if [ -n "$failed_list" ]; then
  echo
  echo "Failure details:"
  for iface in $failed_list; do
    echo "--- $iface"
    grep -E "Failure|Mismatch|mismatch|FAILED|error|Abort" \
      "$LOG_DIR/$iface.log" | head -6
  done
fi

exit $((fail > 0))
