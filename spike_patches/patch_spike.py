#!/usr/bin/env python3
"""Small source modifications to Spike for Assignment 3.

  step     -> riscv/sim.cc      : instruction-stepping hook      (Task 3)
  cachelog -> riscv/cachesim.cc : time-stamped cache-miss log    (Task 5)
  all      -> both

usage: python3 patch_spike.py <spike-source-dir> {step|cachelog|all}
If Spike's source does not look as expected, the script stops and prints
what it found instead of patching blindly.
"""
import re
import sys
from pathlib import Path

TAG = "[A3]"


def die(msg):
    sys.exit("patch_spike.py: ERROR: " + msg)


def add_after_include(src, header, text, fname):
    m = re.search(r'^#include "' + re.escape(header) + r'".*$', src, flags=re.M)
    if not m:
        die(f'{fname}: no line #include "{header}"')
    return src[:m.end()] + "\n" + text + src[m.end():]


def patch_step(root):
    f = root / "riscv" / "sim.cc"
    src = f.read_text()
    if TAG in src:
        print(f"{f}: already patched")
        return
    idle = src.find("void sim_t::idle()")
    hits = list(re.finditer(r"^([ \t]*)step\(INTERLEAVE\);[ \t]*$", src, flags=re.M))
    if idle < 0 or len(hits) != 1 or hits[0].start() < idle:
        for n, line in enumerate(src.splitlines(), 1):
            if "INTERLEAVE" in line or "sim_t::idle" in line:
                print(f"  sim.cc:{n}: {line}")
        die("sim.cc does not have exactly one 'step(INTERLEAVE);' inside sim_t::idle()")
    ind = hits[0].group(1)
    new = (f"{ind}if (spike_step_hook) {{     // {TAG} SystemC drives the ISS:\n"
           f"{ind}  spike_step_hook();       //   wait for the next clock edge,\n"
           f"{ind}  step(1);                 //   then execute exactly ONE instruction\n"
           f"{ind}}} else {{\n"
           f"{ind}  step(INTERLEAVE);        // original free-running Spike\n"
           f"{ind}}}")
    src = src[:hits[0].start()] + new + src[hits[0].end():]
    src = add_after_include(src, "sim.h",
        f"\n// {TAG} Instruction-stepping hook. nullptr = normal Spike. The SystemC\n"
        f"// wrapper sets it to a function that blocks until the next clock edge.\n"
        f"void (*spike_step_hook)() = nullptr;\n", "sim.cc")
    f.write_text(src)
    print(f"{f}: patched (instruction stepping)")


def patch_cachelog(root):
    f = root / "riscv" / "cachesim.cc"
    src = f.read_text()
    if TAG in src:
        print(f"{f}: already patched")
        return
    hits = list(re.finditer(r"^([ \t]*)store \? write_misses\+\+ : read_misses\+\+;[ \t]*$",
                            src, flags=re.M))
    if len(hits) != 1:
        for n, line in enumerate(src.splitlines(), 1):
            if "misses" in line:
                print(f"  cachesim.cc:{n}: {line}")
        die("cachesim.cc: expected exactly one 'store ? write_misses++ : read_misses++;'")
    ind = hits[0].group(1)
    new = hits[0].group(0) + "\n" + (
        f"{ind}if (spike_miss_log)    // {TAG} log the miss with the current SystemC time\n"
        f"{ind}  fprintf(spike_miss_log, \"%.0f,%s,%c,0x%llx\\n\",\n"
        f"{ind}          spike_time_ns ? spike_time_ns() : -1.0, name.c_str(),\n"
        f"{ind}          store ? 'W' : 'R', (unsigned long long)addr);")
    src = src[:hits[0].start()] + new + src[hits[0].end():]
    src = add_after_include(src, "cachesim.h",
        f"#include <cstdio>\n"
        f"// {TAG} Cache-miss log. Both stay nullptr in normal Spike; the SystemC\n"
        f"// wrapper opens the file and provides the current simulation time.\n"
        f"FILE*   spike_miss_log = nullptr;\n"
        f"double (*spike_time_ns)() = nullptr;\n", "cachesim.cc")
    f.write_text(src)
    print(f"{f}: patched (cache-miss log)")


def main():
    if len(sys.argv) != 3 or sys.argv[2] not in ("step", "cachelog", "all"):
        die("usage: patch_spike.py <spike-source-dir> {step|cachelog|all}")
    root = Path(sys.argv[1])
    if sys.argv[2] in ("step", "all"):
        patch_step(root)
    if sys.argv[2] in ("cachelog", "all"):
        patch_cachelog(root)


main()
