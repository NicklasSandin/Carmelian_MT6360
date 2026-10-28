/* SPDX-License-Identifier: MIT */
/* Write-probe + read-probe specific addresses on /dev/i2c-5 */
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
    long fd = sys4(56, AT_FDCWD, (long)"/dev/i2c-5", O_RDWR, 0);
    if (fd < 0) { print("open failed\n"); sys3(93, 1, 0, 0); }
    unsigned addrs[4] = {0x1a, 0x34, 0x4e, 0x64};
    for (unsigned i = 0; i < 4; i++) {
        unsigned a = addrs[i];
        print("0x"); printh(a); print(": ");
        if (sys4(29, fd, I2C_SLAVE, a, 0) < 0) { print("set-slave fail\n"); continue; }
        char buf[2] = {0, 0};
        long w = sys4(65 /*write*/, fd, (long)buf, 1, 0);
        long r = sys3(63, fd, (long)buf, 1);
        print("write="); print(w >= 0 ? "ACK" : "NACK");
        print(" read="); print(r >= 0 ? "ACK" : "NACK");
        if (r >= 0) { print(" data="); printh((unsigned char)buf[0]); }
        print("\n");
    }
    sys3(57, fd, 0, 0);
    sys3(93, 0, 0, 0);
}
