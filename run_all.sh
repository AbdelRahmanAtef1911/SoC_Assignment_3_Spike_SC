#!/usr/bin/env bash
# Reproduces every result of Assignment 3 (this is the Docker CMD).
set -euo pipefail
cd "$(dirname "$0")"
OUT=results
mkdir -p "$OUT"
CACHES="--ic=64:8:64 --dc=64:8:64 --l2=512:8:64"   # 32 KiB L1-I, 32 KiB L1-D, 256 KiB L2, 64 B lines

make -s all

echo "=== Task 1: Spike called as a C function ==="
./build/run_spike build/hello.riscv | tee "$OUT/task1_run_spike.txt"

echo; echo "=== Task 2: MESS on the stand-alone spike executable ==="
spike $CACHES build/mess.riscv | tee "$OUT/task2_mess_spike.txt"

echo; echo "=== Tasks 3/4: SystemC clock steps Spike one instruction per edge ==="
./build/spike_sc --trace-steps 8 -- spike -l build/hello.riscv > "$OUT/task4_stepping.txt" 2>&1
head -n 20 "$OUT/task4_stepping.txt"; echo "..."; tail -n 2 "$OUT/task4_stepping.txt"

echo; echo "=== Task 5: MESS inside SystemC @ 1 GHz, time-stamped cache-miss log ==="
./build/spike_sc --freq-ghz 1 --miss-log "$OUT/miss_log.csv" -- spike $CACHES build/mess.riscv \
    | tee "$OUT/task5_mess_systemc.txt"
head -n 5 "$OUT/miss_log.csv"

echo; echo "=== Task 6: inter-miss histogram ==="
python3 scripts/plot_histogram.py --log "$OUT/miss_log.csv" --elf build/mess.riscv \
    --program-output "$OUT/task5_mess_systemc.txt" --outdir "$OUT" | tee "$OUT/stats.txt"

if [ -f expected/stats.txt ]; then
  echo
  if diff -q expected/stats.txt "$OUT/stats.txt" > /dev/null; then
    echo "REPRODUCED: results/stats.txt is identical to expected/stats.txt"
  else
    echo "DIFFERENT from expected/stats.txt:"; diff expected/stats.txt "$OUT/stats.txt" || true
    exit 1
  fi
fi
