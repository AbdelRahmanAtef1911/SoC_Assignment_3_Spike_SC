# Assignment 3 -- builds the RISC-V programs and the host programs (Spike as a library)
.RECIPEPREFIX = >

SPIKE_SRC    ?= /opt/spike
SPIKE_BLD    ?= $(SPIKE_SRC)/build
RVTESTS      ?= /opt/riscv-tests
PICOLIBC     ?= /usr/lib/picolibc/riscv64-unknown-elf
SYSTEMC_HOME ?= /opt/systemc
BUILD        ?= build

# ---------- RISC-V side: bare metal, riscv-tests runtime ----------
RVCC    = riscv64-unknown-elf-gcc
RVFLAGS = -march=rv64gc -mabi=lp64d -mcmodel=medany -static -std=gnu99 -O2 \
          -fno-common -fno-builtin-printf -fno-tree-loop-distribute-patterns \
          -U_FORTIFY_SOURCE -DPREALLOCATE=1 \
          -I$(RVTESTS)/env -I$(RVTESTS)/benchmarks/common -isystem $(PICOLIBC)/include
RVLINK  = -static -nostdlib -nostartfiles -T $(RVTESTS)/benchmarks/common/test.ld \
          -L$(PICOLIBC)/lib -lm -lgcc
RVRT    = $(RVTESTS)/benchmarks/common/crt.S $(RVTESTS)/benchmarks/common/syscalls.c

# ---------- host side: Spike linked into our own programs ----------
CXX       = g++
CXXFLAGS  = -std=c++20 -O2 -w
SPIKE_INC = -I$(BUILD) -I$(SPIKE_BLD) -I$(SPIKE_SRC) -I$(SPIKE_SRC)/riscv -I$(SPIKE_SRC)/fesvr \
            -I$(SPIKE_SRC)/softfloat -I$(SPIKE_SRC)/fdt -I$(SPIKE_SRC)/disasm \
            -iquote $(SPIKE_SRC)/spike_main
SPIKE_ARS  = $(wildcard $(addprefix $(SPIKE_BLD)/lib,$(addsuffix .a,riscv disasm softfloat fesvr fdt)))
# --whole-archive keeps Spike's self-registering devices/extensions (static constructors)
SPIKE_LIBS = -Wl,--whole-archive $(SPIKE_BLD)/libriscv.a -Wl,--no-whole-archive \
             -Wl,--start-group $(SPIKE_ARS) -Wl,--end-group \
             -lboost_regex -lboost_system -ldl -lpthread

all: $(BUILD)/hello.riscv $(BUILD)/mess.riscv $(BUILD)/run_spike $(BUILD)/spike_sc

$(BUILD):
> mkdir -p $@

# ---------- RISC-V programs ----------
$(BUILD)/hello.riscv: sw/hello.c | $(BUILD)
> $(RVCC) $(RVFLAGS) -o $@ $< $(RVRT) $(RVLINK)

$(BUILD)/mess.riscv: sw/mess/mess.c sw/mess/mess_kernels.S | $(BUILD)
> $(RVCC) $(RVFLAGS) -o $@ $^ $(RVRT) $(RVLINK)

# ---------- Task 1: Spike's main() turned into a callable function spike_main() ----------
$(BUILD)/spike_lib.cc: $(SPIKE_SRC)/spike_main/spike.cc | $(BUILD)
> sed -E 's/^int main\(/int spike_main(/' $< > $@
> grep -q '^int spike_main(' $@ || (echo "ERROR: main() not found in spike.cc" && false)

$(BUILD)/spike_lib.o: $(BUILD)/spike_lib.cc
> $(CXX) $(CXXFLAGS) $(SPIKE_INC) -c $< -o $@

$(BUILD)/run_spike: host/run_spike.cpp $(BUILD)/spike_lib.o $(SPIKE_BLD)/libriscv.a
> $(CXX) $(CXXFLAGS) host/run_spike.cpp $(BUILD)/spike_lib.o -o $@ $(SPIKE_LIBS)
> chmod +x $@

# ---------- Tasks 3-5: Spike inside a SystemC module ----------
$(BUILD)/spike_sc: systemc/spike_sc.cpp $(BUILD)/spike_lib.o $(SPIKE_BLD)/libriscv.a
> $(CXX) $(CXXFLAGS) -I$(SYSTEMC_HOME)/include systemc/spike_sc.cpp $(BUILD)/spike_lib.o -o $@ \
    -L$(SYSTEMC_HOME)/lib -Wl,-rpath,$(SYSTEMC_HOME)/lib -lsystemc $(SPIKE_LIBS)
> chmod +x $@

clean:
> rm -rf $(BUILD)

.PHONY: all clean
