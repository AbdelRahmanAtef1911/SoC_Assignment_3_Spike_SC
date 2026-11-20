// Minimal bare-metal program: printf goes through HTIF (tohost/fromhost) to Spike.
int printf(const char *fmt, ...);

int main(void)
{
    printf("Hello from RISC-V! This program runs inside Spike.\n");
    return 0;
}
