// Task 1 -- run the Spike ISS as a C function call inside our own program
// (not by executing the `spike` binary). spike_main() is Spike's main(),
// renamed at build time (see Makefile rule for build/spike_lib.cc).
#include <cstdio>
#include <vector>

int spike_main(int argc, char **argv);

int main(int argc, char **argv)
{
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s program.riscv [spike options...]\n", argv[0]);
        return 1;
    }
    // Spike's argv: argv[0], options..., program last
    std::vector<char *> args{(char *)"spike"};
    for (int i = 2; i < argc; ++i) args.push_back(argv[i]);
    args.push_back(argv[1]);
    args.push_back(nullptr);

    std::printf("[host] calling spike_main() with %zu arguments\n", args.size() - 1);
    std::fflush(stdout);
    int rc = spike_main((int)args.size() - 1, args.data());
    std::printf("[host] spike_main() returned %d -- control is back in our C program\n", rc);
    return rc;
}
