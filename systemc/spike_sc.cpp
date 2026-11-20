// spike_sc.cpp -- Spike RISC-V ISS inside a SystemC module.
// A 1 GHz sc_clock triggers the ISS: one rising edge = one RISC-V instruction.
//
//   usage: spike_sc [--freq-ghz F] [--miss-log FILE] [--trace-steps N] -- spike [spike options] prog.riscv
#include <systemc.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// ---- symbols provided by our (modified) Spike --------------------------------
int spike_main(int argc, char **argv);                       // Spike's main(), renamed  (Task 1)
extern void (*spike_step_hook)();                            // riscv/sim.cc      [A3]   (Task 3)
extern FILE *spike_miss_log __attribute__((weak));           // riscv/cachesim.cc [A3]   (Task 5)
extern double (*spike_time_ns)() __attribute__((weak));      // riscv/cachesim.cc [A3]   (Task 5)

SC_MODULE(SpikeISS) {
    sc_in<bool> clk;

    uint64_t instructions = 0;   // number of clock-triggered instruction steps
    int exit_code = -1;

    SC_HAS_PROCESS(SpikeISS);
    SpikeISS(sc_module_name name, std::vector<std::string> spike_args, uint64_t trace_steps)
        : sc_module(name), args(std::move(spike_args)), trace(trace_steps)
    {
        instance = this;
        spike_step_hook = &SpikeISS::step_hook;   // install the hook inside Spike
        SC_THREAD(iss_thread);
        sensitive << clk.pos();                   // static sensitivity: rising clock edge
        set_stack_size(64 * 1024 * 1024);         // the whole Spike call stack lives here
    }

private:
    std::vector<std::string> args;
    uint64_t trace;
    static SpikeISS *instance;

    // Called by Spike (sim_t::idle) before every instruction.
    // wait() suspends this SC_THREAD -- and with it Spike's entire call stack --
    // until the next rising edge of clk. Then Spike executes one instruction.
    static void step_hook()
    {
        sc_core::wait();
        SpikeISS *self = instance;
        self->instructions++;
        if (self->instructions <= self->trace)
            std::cout << "[SC] @" << sc_time_stamp() << " clock edge -> Spike executes instruction #"
                      << self->instructions << std::endl;
    }

    void iss_thread()
    {
        std::vector<char *> argv;
        for (auto &a : args) argv.push_back(a.data());
        argv.push_back(nullptr);

        exit_code = spike_main((int)args.size(), argv.data());   // runs until the program exits

        std::cout << "[SC] @" << sc_time_stamp() << " Spike returned " << exit_code << " after "
                  << instructions << " clock-triggered instructions" << std::endl;
        sc_stop();
    }
};
SpikeISS *SpikeISS::instance = nullptr;

static void usage(const char *p)
{
    std::cerr << "usage: " << p << " [--freq-ghz F] [--miss-log FILE] [--trace-steps N]"
              << " -- spike [spike options] program.riscv\n";
}

int sc_main(int argc, char *argv[])
{
    double freq_ghz = 1.0;
    std::string miss_log_path;
    uint64_t trace_steps = 0;

    int i = 1;
    for (; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--") { ++i; break; }
        if (a == "--freq-ghz" && i + 1 < argc)         freq_ghz = std::atof(argv[++i]);
        else if (a == "--miss-log" && i + 1 < argc)    miss_log_path = argv[++i];
        else if (a == "--trace-steps" && i + 1 < argc) trace_steps = std::strtoull(argv[++i], nullptr, 10);
        else { usage(argv[0]); return 1; }
    }
    std::vector<std::string> spike_args(argv + i, argv + argc);
    if (spike_args.empty()) { usage(argv[0]); return 1; }

    if (!miss_log_path.empty()) {
        if (&spike_miss_log == nullptr) {
            std::cerr << "ERROR: Spike is not patched for miss logging (run patch_spike.py ... cachelog)\n";
            return 1;
        }
        spike_miss_log = std::fopen(miss_log_path.c_str(), "w");
        if (!spike_miss_log) { std::perror(miss_log_path.c_str()); return 1; }
        std::fprintf(spike_miss_log, "time_ns,cache,type,addr\n");
        // Spike asks SystemC for the current time whenever it logs a miss
        spike_time_ns = []() -> double { return sc_core::sc_time_stamp() / sc_core::sc_time(1, sc_core::SC_NS); };
    }

    sc_clock clk("clk", sc_time(1.0 / freq_ghz, SC_NS));   // 1 GHz -> 1 ns period
    SpikeISS iss("spike_iss", spike_args, trace_steps);
    iss.clk(clk);

    std::cout << "[SC] clock " << freq_ghz << " GHz (period " << clk.period() << "), Spike args:";
    for (auto &s : spike_args) std::cout << ' ' << s;
    std::cout << std::endl;

    auto t0 = std::chrono::steady_clock::now();
    sc_start();
    double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    if (!miss_log_path.empty() && spike_miss_log) {
        std::fclose(spike_miss_log);
        spike_miss_log = nullptr;
    }
    std::cout << "[SC] simulated time " << sc_time_stamp() << ", " << iss.instructions
              << " instructions, wall " << wall << " s ("
              << (wall > 0 ? iss.instructions / wall / 1e6 : 0.0) << " MIPS)" << std::endl;
    return iss.exit_code;
}
