/* pid1.c - the immortal init for the camellian omarchy pivot.
 * PID 1 that CANNOT crash: no memory access, just wait4() + nanosleep.
 * - Reaps every orphan (zombies never pile up)
 * - Fatal signals to init are ignored by the kernel by design
 * - Replaces the systemd manager, whose mount-table assert on the
 *   4.14 kernel SEGVs PID 1 and kernel-panics the phone ~3 min in.
 * Static, nostdlib, raw syscalls. aarch64. */
#define SYS_wait4     260
#define SYS_nanosleep 101

void _start(void) {
    static struct { long sec, nsec; } ts = { 1, 0 };
    for (;;) {
        register long x8 __asm__ ("x8") = SYS_wait4;
        register long x0 __asm__ ("x0") = -1;      /* any child */
        register long x1 __asm__ ("x1") = 0;       /* status */
        register long x2 __asm__ ("x2") = 0;       /* options */
        register long x3 __asm__ ("x3") = 0;       /* rusage */
        __asm__ volatile ("svc 0" : "+r"(x0) :
            "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3) : "memory", "cc");
        if (x0 < 0) {
            /* no children right now - nap 1s */
            register long a8 __asm__ ("x8") = SYS_nanosleep;
            register long a0 __asm__ ("x0") = (long)&ts;
            register long a1 __asm__ ("x1") = 0;
            __asm__ volatile ("svc 0" : "+r"(a0) :
                "r"(a8), "r"(a0), "r"(a1) : "memory", "cc");
        }
    }
}
