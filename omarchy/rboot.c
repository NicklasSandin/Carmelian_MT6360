/* rboot.c - reboot to bootloader via RESTART2, NO BCB/misc write.
 * The MTK kernel's restart handler passes the reason string through the
 * PMIC/RTC scratch to LK, which enters fastboot. Unlike adb reboot
 * bootloader (Android init writes bootonce-bootloader to misc), this
 * leaves misc untouched - the BCB-zero rule stays satisfied.
 * Static, nostdlib, pure syscalls. aarch64. */
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#define LINUX_REBOOT_CMD_RESTART2 0xa1b2c3d4

#define SYS_sync   162
#define SYS_reboot 142
#define SYS_pause  34
#define SYS_write  64

static inline long sys4(long n, long a, long b, long c, long d) {
    register long x8 __asm__ ("x8") = n;
    register long x0 __asm__ ("x0") = a;
    register long x1 __asm__ ("x1") = b;
    register long x2 __asm__ ("x2") = c;
    register long x3 __asm__ ("x3") = d;
    __asm__ volatile ("svc 0"
                      : "+r"(x0)
                      : "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3)
                      : "memory", "cc");
    return x0;
}

static const char msg[] = "rboot: RESTART2 bootloader\n";
static const char reason[] = "bootloader";

void _start(void) {
    sys4(SYS_write, 1, (long)msg, sizeof(msg) - 1, 0);
    sys4(SYS_sync, 0, 0, 0, 0);
    sys4(SYS_sync, 0, 0, 0, 0);
    sys4(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
         LINUX_REBOOT_CMD_RESTART2, (long)reason);
    /* if the kernel refused the reason, fall back to a plain forced reboot */
    sys4(SYS_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, 0x1234567, 0);
    for (;;)
        sys4(SYS_pause, 0, 0, 0, 0);
}
