/* SPDX-License-Identifier: MIT */
typedef unsigned long ulong;
#define AT_FDCWD (-100)
#define O_RDWR 2
#define I2C_SLAVE 0x0703
static long sys4(long n, long a, long b, long c, long d) {
    register long x8 __asm__("x8") = n;
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8), "r"(x1), "r"(x2), "r"(x3) : "memory");
    return x0;
}
static long sys3(long n, long a, long b, long c) { return sys4(n, a, b, c, 0); }
static void print(const char *s) { long n=0; while (s[n]) n++; sys3(64, 1, (long)s, n); }
static void printh(unsigned v) { const char *h="0123456789abcdef"; char o[4]={'0','x',h[(v>>4)&0xf],h[v&0xf]}; sys3(64,1,(long)o,4); }
void _start(void) {
    unsigned buses[2] = {4, 5};
    for (unsigned bi = 0; bi < 2; bi++) {
        char path[16] = "/dev/i2c-";
        path[9] = '0'+buses[bi]; path[10] = 0;
        long fd = sys4(56, AT_FDCWD, (long)path, O_RDWR, 0);
        if (fd < 0) { print(path); print(": open failed\n"); continue; }
        print(path); print(":");
        for (unsigned a = 3; a < 0x78; a++) {
            if (sys4(29, fd, I2C_SLAVE, a, 0) < 0) continue;
            char buf[1];
            if (sys3(63, fd, (long)buf, 1) >= 0) { print(" "); printh(a); }
        }
        print("\n");
        sys3(57, fd, 0, 0);
    }
    sys3(93, 0, 0, 0);
}
