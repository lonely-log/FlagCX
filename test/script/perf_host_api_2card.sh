#!/bin/bash
#
# FlagCX Host API perf sweep - single node, Iluvatar CoreX (2 devices by default).
#
# Runs every perf_* binary in test/perf/host_api/build/bin and writes one log
# file per operation plus summary.txt.
#
# Usage:
#   ./perf_host_api_2card.sh [np]        # np defaults to 2
#   BUILD=1 ./perf_host_api_2card.sh     # build the perf binaries first
#
# Environment knobs:
#   ROOT=<path>       FlagCX root (default: derived from this script's location)
#   BIN=<dir>         perf binary dir (default <ROOT>/test/perf/host_api/build/bin)
#   VIS=1,2           devices handed to the ranks via -x CUDA_VISIBLE_DEVICES.
#                     Defaults to 1,2 (GPU0 is broken on this host, so it must
#                     stay hidden). Set VIS= to expose every device instead.
#   MPIRUN_EXTRA="..."  optional: appended verbatim to mpirun, e.g.
#                     MPIRUN_EXTRA="--bind-to none -x LD_LIBRARY_PATH"
#   NP=2              MPI ranks (default: 2)
#   MIN=128K MAX=4G STEP=2 WARMUP=5 ITERS=20  size sweep (same defaults as the docs)
#   LOG_DIR=<dir>     default <ROOT>/test/perf/host_api/perf_logs/<timestamp>
#   TIMEOUT=1800      per-case timeout in seconds
#   COREX_LIB=/usr/local/corex/lib64          toolkit lib dir, added to LD_LIBRARY_PATH
#   BUILD=1           run make before testing
#   ALLOW_SHLIB_UNDEFINED=1   build with -Wl,--allow-shlib-undefined
#                             (workaround when the toolkit is newer than the driver)
#   SKIP_COLLECTIVE=1 / SKIP_RMA=1 / SKIP_INTERNAL=1   skip a group
#
set -u

case "${1:-}" in
  -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
esac

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ROOT=${ROOT:-$(cd -- "$SCRIPT_DIR/../.." && pwd)}
BIN=${BIN:-$ROOT/test/perf/host_api/build/bin}

NP=${1:-${NP:-2}}
VIS=${VIS-1,2}
MPIRUN_EXTRA=${MPIRUN_EXTRA:-}
MIN=${MIN:-128K}
MAX=${MAX:-4G}
STEP=${STEP:-2}
WARMUP=${WARMUP:-5}
ITERS=${ITERS:-20}
TIMEOUT=${TIMEOUT:-1800}
COREX_LIB=${COREX_LIB:-/usr/local/corex/lib64}

STAMP=$(date +%Y%m%d-%H%M%S)
LOG_DIR=${LOG_DIR:-$ROOT/test/perf/host_api/perf_logs/$STAMP}
SUMMARY="$LOG_DIR/summary.txt"

export PATH="/usr/local/openmpi/bin:${PATH:-}"
export LD_LIBRARY_PATH="$ROOT/build/lib:$COREX_LIB:${LD_LIBRARY_PATH:-}"

# MPI launcher, kept in the same shape as the documented example:
#   mpirun --allow-run-as-root -np 2 ./perf_allreduce -b 128K -e 4G -f 2
# VIS / MPIRUN_EXTRA are only appended when explicitly requested.
MPIRUN="mpirun --allow-run-as-root -np $NP"
if [ -n "$VIS" ]; then
  MPIRUN="$MPIRUN -x CUDA_VISIBLE_DEVICES=$VIS"
fi
if [ -n "$MPIRUN_EXTRA" ]; then
  MPIRUN="$MPIRUN $MPIRUN_EXTRA"
fi
SIZE="-b $MIN -e $MAX -f $STEP -w $WARMUP -n $ITERS"
RMA_SIZE="-b $MIN -e 1M -f $STEP -w $WARMUP -n $ITERS"
RMA_ENV="-x FLAGCX_MEM_ENABLE=1 -x FLAGCX_VMM_ENABLE=0 -x FLAGCX_USE_HETERO_COMM=1"
CASE_ENV=""
TOTAL=0
FAILED=0

build_binaries() {
  local perf_dir="$ROOT/test/perf/host_api"
  local -a cmd=(make -C "$perf_dir" USE_ILUVATAR=1 -j"$(nproc)")
  if [ "${ALLOW_SHLIB_UNDEFINED:-0}" = "1" ]; then
    local mpi_link="-lmpi"
    if command -v mpicxx >/dev/null 2>&1; then
      mpi_link="$(mpicxx --showme:link)"
    fi
    cmd+=("MPI_LINK=$mpi_link -Wl,--allow-shlib-undefined")
    echo "  (linking with -Wl,--allow-shlib-undefined)"
  fi
  echo "== build: ${cmd[*]}"
  if ! "${cmd[@]}"; then
    echo "build failed" >&2
    return 1
  fi
}

echo "FlagCX host API perf sweep"
echo "  root     : $ROOT"
echo "  binaries : $BIN"
echo "  np       : $NP"
echo "  mpirun   : $MPIRUN"
echo "  sizes    : $SIZE"
echo "  toolkit  : $(readlink -f /usr/local/corex 2>/dev/null || echo '(not found)')"
if command -v ixsmi >/dev/null 2>&1; then
  ixsmi 2>/dev/null | sed -n '3p' | sed 's/^| */  driver   : /'
fi
echo

if [ "${BUILD:-0}" = "1" ]; then
  build_binaries || exit 1
  echo
fi

if ! ls "$BIN"/perf_* >/dev/null 2>&1; then
  echo "error: no perf binaries in $BIN" >&2
  echo "build them with:" >&2
  echo "  make -C $ROOT/test/perf/host_api USE_ILUVATAR=1 -j\$(nproc)" >&2
  echo "(or re-run this script with BUILD=1)" >&2
  exit 1
fi

mkdir -p "$LOG_DIR"
echo "  logs     : $LOG_DIR"
echo

{
  echo "np=$NP CUDA_VISIBLE_DEVICES=$VIS"
  echo "sizes=$SIZE"
  echo "rma_sizes=$RMA_SIZE"
  echo "toolkit=$(readlink -f /usr/local/corex 2>/dev/null)"
  echo "driver=$(ixsmi 2>/dev/null | sed -n '3p' | tr -s ' ')"
  echo
} >"$SUMMARY"

# run_case <label> <binary> [args...]
# CASE_ENV carries extra mpirun -x options for the current group.
run_case() {
  local label=$1 bin=$2
  shift 2
  local log="$LOG_DIR/$label.log"
  local t0 t1 rc status
  TOTAL=$((TOTAL + 1))
  {
    echo "# $(date -Is)"
    echo "# np=$NP CUDA_VISIBLE_DEVICES=$VIS"
    echo "# $BIN/$bin $*"
  } >"$log"
  t0=$(date +%s)
  # shellcheck disable=SC2086
  timeout "$TIMEOUT" $MPIRUN $CASE_ENV "$BIN/$bin" "$@" >>"$log" 2>&1
  rc=$?
  t1=$(date +%s)
  if [ "$rc" -eq 0 ]; then
    status=PASS
  else
    status=FAIL
    FAILED=$((FAILED + 1))
  fi
  printf '%-22s %-4s %5ss  %s\n' "$label" "$status" "$((t1 - t0))" "$log"
  printf '%-22s %-4s %5ss  rc=%-3s %s\n' \
    "$label" "$status" "$((t1 - t0))" "$rc" "$log" >>"$SUMMARY"
}

if [ "${SKIP_COLLECTIVE:-0}" != "1" ]; then
  echo "-- collectives (any np; root-based ops pinned with -r 0) --"
  run_case allreduce     perf_allreduce     $SIZE
  run_case allgather     perf_allgather     $SIZE
  run_case alltoall      perf_alltoall      $SIZE
  run_case alltoallv     perf_alltoallv     $SIZE
  run_case sendrecv      perf_sendrecv      $SIZE
  run_case reducescatter perf_reducescatter $SIZE
  run_case broadcast     perf_broadcast     $SIZE -r 0
  run_case gather        perf_gather        $SIZE -r 0
  run_case scatter       perf_scatter       $SIZE -r 0
  run_case reduce        perf_reduce        $SIZE -r 0
  echo
fi

if [ "${SKIP_RMA:-0}" != "1" ]; then
  echo "-- p2p engine (requires exactly 2 ranks) --"
  run_case p2p_engine perf_p2p_engine -b 4K -e 64M -f "$STEP" -w "$WARMUP" -n "$ITERS"
  echo

  echo "-- one-sided RMA (requires exactly 2 ranks and -R 2) --"
  CASE_ENV="$RMA_ENV"
  run_case put               perf_put               $RMA_SIZE -R 2
  run_case get               perf_get               $RMA_SIZE -R 2
  run_case one_side_register perf_one_side_register $RMA_SIZE -R 2
  CASE_ENV=""
  echo
fi

if [ "${SKIP_INTERNAL:-0}" != "1" ]; then
  # perf_core_sendrecv is intentionally not run here: it drives the internal
  # flagcxHetero* API directly (no public equivalent, no CI coverage) and
  # deadlocks in uniRunner's first op on Iluvatar. Set SKIP_INTERNAL=1 to drop
  # the remaining internal case as well.
  echo "-- internal APIs (outside the supported host API set) --"
  run_case ipc_sendrecv perf_ipc_sendrecv $SIZE
  echo
fi

echo "-- largest size measured per operation --"
for f in "$LOG_DIR"/*.log; do
  [ -f "$f" ] || continue
  line=$(grep -E 'Comm size:' "$f" | tail -n 1)
  if [ -n "$line" ]; then
    printf '%-22s %s\n' "$(basename "$f" .log)" "$line"
    printf '%-22s %s\n' "$(basename "$f" .log)" "$line" >>"$SUMMARY"
  fi
done

echo
printf 'PASS=%d FAIL=%d  logs: %s\n' "$((TOTAL - FAILED))" "$FAILED" "$LOG_DIR"
printf 'PASS=%d FAIL=%d\n' "$((TOTAL - FAILED))" "$FAILED" >>"$SUMMARY"

[ "$FAILED" -eq 0 ]
