# SoC Assignment 3 — Spike ISS inside SystemC, MESS, cache-miss histogram

COE 407/661 System on Chip, KFUPM.

| Task | Where |
|---|---|
| 1. Spike run as a C function (not the executable) | `host/run_spike.cpp`, Makefile rule `build/spike_lib.cc` |
| 2. MESS (BSC) RISC-V sources compiled and run on Spike | `sw/mess/import_mess.sh`, `sw/mess/mess_spike.c` |
| 3. Instruction stepping (Spike modified) | `spike_patches/patch_spike.py` (`step`), `spike_patches/spike_a3.diff` |
| 4. ISS inside a SystemC module, 1 instruction per clock edge | `systemc/spike_sc.cpp` |
| 5. Cache-miss detection + time-stamped log | `spike_patches/patch_spike.py` (`cachelog`) |
| 6. 1 GHz run + inter-miss histogram | `run_all.sh`, `scripts/plot_histogram.py` |

## Reproduce

```bash
git clone https://github.com/AbdelRahmanAtef1911/SoC_Assignment_3_Spike_SC.git
cd SoC_Assignment_3_Spike_SC
docker build -t soc-a3 .
docker run --rm -v "$PWD/results:/repo/results" soc-a3
```
Windows PowerShell: `docker run --rm -v "${PWD}\results:/repo/results" soc-a3`

The build takes about 10–20 minutes (SystemC + Spike from source). The run prints every task's output and
ends with `REPRODUCED: results/stats.txt is identical to expected/stats.txt`.
Figures are written to `results/` and should match `expected/hist_*.png`.

Caches: L1-I and L1-D 32 KiB 8-way (`64:8:64`), L2 256 KiB 8-way (`512:8:64`), 64 B lines.
Clock: 1 GHz, so 1 instruction = 1 ns (Spike is functional, CPI = 1).
