// mess_spike.c -- bare-metal driver that runs the MESS RISC-V kernels on Spike.
// MESS (BSC) RISC-V sources, SiFive HiFive Unmatched port:
//   stream_mpi/src/utils.c  STREAM_copy_<rd%>()  traffic generator  (compiled, see import_mess.sh)
//   stream_mpi/src/nop.c    nop_()               pause routine      (compiled)
//   ptr_chase/loop_template.h -> loop.h          pointer-chase load (their awk expansion)
// Copied verbatim below: shuffle() + walk_generator() (ptr_chase/arraygen.c) and the
// pointer-chase asm block (ptr_chase/ptr_chase.c).
// Replaced (needs an OS): MPI, getopt, perf/PAPI, array.dat file I/O, posix_memalign, timers.
// Phase markers: each phase starts with one load from its own never-touched line of
// mess_marker[] -> a compulsory D$ miss that marks the phase boundary in the miss log.
#include <stdint.h>
#include <sys/types.h>
#include "utils.h"

int printf(const char *fmt, ...);

#define CACHE_LINE     64
#define ARRAY_ELEMS    65536    /* pointer-chase array: 65536 x 64 B = 4 MiB  (>> 256 KiB L2) */
#define LOOP_ACCESSES  1024     /* loop.h = loop_template.h repeated 1024 times (Makefile)  */
#define ITERS          128      /* 128 x 1024 = 131072 loads = 2 laps of the random walk   */
#define TG_ELEMS       256000   /* doubles per traffic array = 2 MB; multiple of 400, since */
                                /* one kernel loop iteration advances 3200 B               */
#define N_MARKERS      7

/* No C library on bare metal: rand()/srand() as a fixed-seed 64-bit LCG (31-bit output). */
#define RAND_MAX 2147483647
static uint64_t rand_state;
static void srand(unsigned s) { rand_state = s; }
static int rand(void)
{
    rand_state = rand_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((rand_state >> 33) & RAND_MAX);
}

struct line{
    struct line *next; // 8 byte
    uint8_t pad[CACHE_LINE-8];
};

static struct line array[ARRAY_ELEMS] __attribute__((aligned(CACHE_LINE)));
static uint64_t seq[ARRAY_ELEMS-1];
static uint64_t res[ARRAY_ELEMS];
static double a[TG_ELEMS] __attribute__((aligned(64)));
static double b[TG_ELEMS] __attribute__((aligned(64)));
uint64_t mess_marker[16 * CACHE_LINE / 8] __attribute__((aligned(64)));
static uint64_t marker_instret[N_MARKERS];

/* ---------- copied verbatim from MESS ptr_chase/arraygen.c ---------- */
void shuffle(uint64_t *array, uint64_t n) {
    srand(0);
    if (n > 1)
    {
        for (uint64_t i = 0; i < n - 1; i++)
        {
          uint64_t j = i + rand() / (RAND_MAX / (n - i) + 1);
          uint64_t t = array[j];
          array[j] = array[i];
          array[i] = t;
        }
    }
}

void walk_generator(){
    for(uint64_t j = 1; j < ARRAY_ELEMS; j++){
        seq[j - 1] = j;
    }
    shuffle(seq, ARRAY_ELEMS - 1);
    res[0] = seq[0];
    int n = res[0];
    for(uint64_t j = 0; j < ARRAY_ELEMS - 1; j++){
        res[n] = seq[j];
        n = res[n];
    }
    for(uint64_t j = 0; j < ARRAY_ELEMS; j++){
        array[j].next = (struct line *) (res[j] * CACHE_LINE);
    }
}
/* -------------------------------------------------------------------- */

/* ---------- pointer-chase asm block copied verbatim from MESS ptr_chase/ptr_chase.c ---------- */
static void __attribute__((noinline)) pointer_chase(void)
{
        register uint64_t i;
        register struct line *start;
        register struct line *next;
        i=ITERS;
        start = array;
        next = 0;
        asm __volatile__ (
            "start_loop:"
            "li x20, 1;"
            #include "loop.h"
            "sub %0, %0, x20;"
            "bgt %0 , x0, start_loop;"
            :
            : "r" (i), "r" (start), "r" (next)
            : "x20", "x21"
        );
}
/* --------------------------------------------------------------------------------------------- */

static inline uint64_t minstret(void)
{
    uint64_t x;
    __asm__ volatile ("csrr %0, minstret" : "=r"(x));
    return x;
}

static void __attribute__((noinline)) phase(int id, const char *name)
{
    printf("[mess] phase %d: %s\n", id, name);
    uint64_t now = minstret();
    __asm__ volatile ("" ::: "memory");
    (void)*(volatile uint64_t *)&mess_marker[id * (CACHE_LINE / 8)];   /* marker miss */
    __asm__ volatile ("" ::: "memory");
    marker_instret[id] = now;
}

struct tg_config {
    const char *name;
    void (*kernel)(double *, double *, ssize_t *, const int *const);
    int pause;
};

int main(void)
{
    /* MESS traffic-generator points: read ratio (kernel) x pause (nops), from their config ranges */
    struct tg_config tg[4] = {
        { "MESS traffic generator STREAM_copy_100, pause 0",  STREAM_copy_100, 0  },
        { "MESS traffic generator STREAM_copy_100, pause 50", STREAM_copy_100, 50 },
        { "MESS traffic generator STREAM_copy_70, pause 0",   STREAM_copy_70,  0  },
        { "MESS traffic generator STREAM_copy_70, pause 50",  STREAM_copy_70,  50 },
    };

    phase(0, "init: MESS walk_generator() builds the random pointer-chase walk (4 MiB)");
    walk_generator();

    phase(1, "MESS pointer-chase: 128 iterations x 1024 dependent loads");
    pointer_chase();

    for (int k = 0; k < 4; k++) {
        phase(2 + k, tg[k].name);
        ssize_t n = TG_ELEMS;
        int pause = tg[k].pause;
        tg[k].kernel(a, b, &n, &pause);      /* one sweep over a (loads) and b (stores) */
    }
    phase(6, "done");

    for (int k = 0; k < N_MARKERS; k++)
        printf("[mess] marker %d minstret=%ld\n", k, (long)marker_instret[k]);
    return 0;
}
