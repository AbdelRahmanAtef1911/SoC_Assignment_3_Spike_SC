# Assignment 3 -- Spike ISS inside SystemC + MESS + cache-miss histogram
# build:  docker build -t soc-a3 .
# run:    docker run --rm -v "$PWD/results:/repo/results" soc-a3
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake wget ca-certificates git python3 python3-numpy python3-matplotlib \
        autoconf automake device-tree-compiler libboost-regex-dev libboost-system-dev \
        gcc-riscv64-unknown-elf picolibc-riscv64-unknown-elf \
    && rm -rf /var/lib/apt/lists/*

# ---- SystemC 2.3.3, C++20 (same as the course image) ----
ENV SYSTEMC_HOME=/opt/systemc
RUN mkdir -p /tmp/sc && cd /tmp/sc \
 && wget -q https://accellera.org/images/downloads/standards/systemc/systemc-2.3.3.tar.gz \
 && tar xzf systemc-2.3.3.tar.gz && mkdir systemc-2.3.3/build && cd systemc-2.3.3/build \
 && cmake .. -DCMAKE_INSTALL_PREFIX=$SYSTEMC_HOME -DCMAKE_CXX_STANDARD=20 \
 && make -j"$(nproc)" && make install && rm -rf /tmp/sc
ENV CPLUS_INCLUDE_PATH=/opt/systemc/include LIBRARY_PATH=/opt/systemc/lib LD_LIBRARY_PATH=/opt/systemc/lib

# ---- pinned sources (same commits as the reported results) ----
ARG SPIKE_COMMIT=e53c8ccf3448ae459877ed3236cb39b2a1d493bf
ARG RVTESTS_COMMIT=3df703cd52d5f0d8224e3c50bf03c6cb70efff98
ARG MESS_COMMIT=bdf1beb8f7619e7bdff5b5a58f2dc78a6bd1830a
# HTTP/1.1 avoids 'RPC failed; curl 92 HTTP/2 stream ... CANCEL' on unstable networks
RUN git config --global http.version HTTP/1.1 && git config --global http.postBuffer 524288000
RUN git clone https://github.com/riscv-software-src/riscv-isa-sim.git /opt/spike \
 && cd /opt/spike && git checkout "$SPIKE_COMMIT" && git submodule update --init --recursive
RUN git clone https://github.com/riscv-software-src/riscv-tests.git /opt/riscv-tests \
 && cd /opt/riscv-tests && git checkout "$RVTESTS_COMMIT" && git submodule update --init --recursive
# MESS benchmark (BSC): only the RISC-V folder is needed (the full repo is large)
RUN git clone --filter=blob:none --no-checkout https://github.com/bsc-mem/Mess-benchmark.git /opt/Mess-benchmark \
 && cd /opt/Mess-benchmark && git sparse-checkout set "CPU/Actual-hardware/RISC-V" && git checkout "$MESS_COMMIT"

# ---- modify Spike (Tasks 3 and 5) and build it ----
COPY spike_patches /repo/spike_patches
RUN python3 /repo/spike_patches/patch_spike.py /opt/spike all \
 && mkdir -p /opt/spike/build && cd /opt/spike/build \
 && ../configure --prefix=/opt/riscv && make -j"$(nproc)" && make install
ENV PATH=/opt/riscv/bin:$PATH

# ---- our code ----
COPY . /repo
WORKDIR /repo
RUN make all
# run every task at build time: the build FAILS if results differ from expected/
RUN bash run_all.sh

CMD ["bash", "run_all.sh"]
