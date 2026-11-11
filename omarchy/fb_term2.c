/* fb_term2.c - framebuffer terminal with correct init order.
 * 1. Open fb0 and card0
 * 2. Unblank (enables the display)
 * 3. mmap the framebuffer
 * 4. Draw the Omarchy UI
 * No libc. Raw syscalls. */
#define SYS_openat   56
#define SYS_close    57
#define SYS_write    64
#define SYS_read     63
#define SYS_mmap     222
#define SYS_exit     93
#define SYS_ioctl    29
#define SYS_nanosleep 101

#define O_RDWR 2
#define O_RDONLY 0
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_SHARED 1

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPUT_BLANK 0x4611

#define COLOR_BG       0xFF1A1A2E
#define COLOR_HEADER   0xFF16213E
#define COLOR_ACCENT   0xFF0F3460
#define COLOR_TEXT     0xFFE0E0E0
#define COLOR_GREEN    0xFF00C896
#define COLOR_PROMPT   0xFF00FF88
#define COLOR_BORDER   0xFF404060
#define COLOR_LAUNCHER 0xFF0D1117
#define COLOR_WHITE    0xFFFFFFFF
#define COLOR_RED      0xFFFF4444

#define SCREEN_W  1080
#define SCREEN_H  2400
#define VIRT_W    1088
#define STRIDE    (VIRT_W * 4)
#define HEADER_H  120
#define LAUNCHER_H 160
#define TERM_TOP  HEADER_H
#define TERM_BOT  (SCREEN_H - LAUNCHER_H)
#define TERM_H    (TERM_BOT - TERM_TOP)
#define CHAR_W    12
#define CHAR_H    20

static long sys6(long n, long a, long b, long c, long d, long e, long f) {
    register long x8 __asm__ ("x8") = n;
    register long x0 __asm__ ("x0") = a;
    register long x1 __asm__ ("x1") = b;
    register long x2 __asm__ ("x2") = c;
    register long x3 __asm__ ("x3") = d;
    register long x4 __asm__ ("x4") = e;
    register long x5 __asm__ ("x5") = f;
    __asm__ volatile ("svc 0" : "+r"(x0) :
        "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5) : "memory", "cc");
    return x0;
}

static long sys3(long n, long a, long b, long c) {
    register long x8 __asm__ ("x8") = n;
    register long x0 __asm__ ("x0") = a;
    register long x1 __asm__ ("x1") = b;
    register long x2 __asm__ ("x2") = c;
    __asm__ volatile ("svc 0" : "+r"(x0) :
        "r"(x8), "r"(x0), "r"(x1), "r"(x2) : "memory", "cc");
    return x0;
}

static void puts_(const char *s) {
    long n = 0;
    while (s[n]) n++;
    sys3(SYS_write, 1, (long)s, n);
}

static long open_(const char *path, long flags) {
    return sys3(SYS_openat, -100, (long)path, flags);
}

static void puts_hex(const char *prefix, long val) {
    char buf[64];
    int i = 0;
    while (prefix[i]) { buf[i] = prefix[i]; i++; }
    buf[i++] = ' '; buf[i++] = '0'; buf[i++] = 'x';
    for (int j = 0; j < 8; j++) {
        int nib = ((val >> ((7-j)*4)) & 0xf);
        buf[i++] = nib < 10 ? '0'+nib : 'a'+nib-10;
    }
    buf[i++] = '\n';
    buf[i] = 0;
    sys3(SYS_write, 1, (long)buf, i);
}

static unsigned int *fb_mem;

static void fill_rect(int x, int y, int w, int h, unsigned int color) {
    for (int ry = y; ry < y + h && ry < SCREEN_H; ry++) {
        unsigned int *row = (unsigned int *)((long)fb_mem + ry * STRIDE);
        for (int rx = x; rx < x + w && rx < SCREEN_W; rx++)
            row[rx] = color;
    }
}

static const unsigned char font[][5] = {
    {0x00,0x00,0x00,0x00,0x00},
    {0x0E,0x11,0x11,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x0E},
    {0x0E,0x01,0x0E,0x10,0x0F},{0x0E,0x01,0x0E,0x01,0x0E},
    {0x11,0x11,0x0E,0x01,0x01},{0x0F,0x10,0x0E,0x01,0x0E},
    {0x0E,0x10,0x0E,0x11,0x0E},{0x0F,0x01,0x02,0x04,0x04},
    {0x0E,0x11,0x0E,0x11,0x0E},{0x0E,0x11,0x0F,0x01,0x0E},
    {0x04,0x0A,0x11,0x11,0x11},{0x1E,0x11,0x1E,0x11,0x1E},
    {0x0E,0x11,0x10,0x11,0x0E},{0x1C,0x12,0x11,0x12,0x1C},
    {0x0F,0x10,0x0E,0x10,0x0F},{0x0F,0x10,0x0E,0x10,0x10},
    {0x0E,0x11,0x11,0x13,0x0D},{0x11,0x11,0x1F,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x0E},{0x01,0x01,0x01,0x11,0x0E},
    {0x11,0x12,0x1C,0x12,0x11},{0x10,0x10,0x10,0x10,0x0F},
    {0x11,0x1B,0x15,0x11,0x11},{0x11,0x19,0x15,0x13,0x11},
    {0x0E,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x1E,0x10,0x10},
    {0x0E,0x11,0x11,0x13,0x0F},{0x1E,0x11,0x1E,0x12,0x11},
    {0x0E,0x10,0x0E,0x01,0x0E},{0x1F,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x15,0x1B,0x11},{0x11,0x0A,0x04,0x0A,0x11},
    {0x11,0x0A,0x04,0x04,0x04},{0x1F,0x02,0x04,0x08,0x1F},
    {0x0E,0x11,0x11,0x11,0x0E},
    {0x00,0x04,0x00,0x00,0x00},{0x00,0x00,0x1F,0x00,0x00},
    {0x04,0x04,0x1F,0x04,0x04},{0x00,0x01,0x02,0x04,0x08},
    {0x11,0x0A,0x1F,0x0A,0x11},{0x01,0x02,0x04,0x08,0x10},
    {0x0C,0x0C,0x00,0x0C,0x0C},{0x0E,0x11,0x00,0x11,0x0E},
    {0x04,0x0A,0x11,0x00,0x00},{0x00,0x00,0x00,0x00,0x1F},
    {0x04,0x04,0x04,0x04,0x04},{0x02,0x04,0x08,0x04,0x02},
    {0x08,0x04,0x02,0x04,0x08},
};

static void draw_char(int x, int y, char c, unsigned int color) {
    if (c >= 'a' && c <= 'z') c = c - 32;
    int idx = 0;
    if (c >= '0' && c <= '9') idx = 1 + (c - '0');
    else if (c >= 'A' && c <= 'Z') idx = 11 + (c - 'A');
    else if (c == '.') idx = 38;
    else if (c == '-') idx = 39;
    else if (c == '+') idx = 40;
    else if (c == '/') idx = 41;
    else if (c == '*') idx = 42;
    else if (c == '/') idx = 43;
    else if (c == ':') idx = 44;
    else if (c == '>') { /* custom */ 
        unsigned char g[5] = {0x08,0x04,0x02,0x04,0x08};
        for (int py = 0; py < 7; py++) for (int px = 0; px < 5; px++) {
            if (g[py] & (0x10 >> px))
                for (int fy = 0; fy < CHAR_H/7; fy++)
                    for (int fx = 0; fx < CHAR_W/5; fx++)
                        if (y+py*(CHAR_H/7)+fy < SCREEN_H)
                            fb_mem[(y+py*(CHAR_H/7)+fy)*VIRT_W + x+px*(CHAR_W/5)+fx] = color;
        } return;
    }
    if (idx >= (int)(sizeof(font)/sizeof(font[0]))) idx = 0;
    int cw = CHAR_W / 5, ch = CHAR_H / 7;
    for (int py = 0; py < 7; py++)
        for (int px = 0; px < 5; px++)
            if (font[idx][py] & (0x10 >> px))
                for (int fy = 0; fy < ch && y+py*ch+fy < SCREEN_H; fy++)
                    for (int fx = 0; fx < cw && x+px*cw+fx < SCREEN_W; fx++)
                        fb_mem[(y+py*ch+fy)*VIRT_W + x+px*cw+fx] = color;
}

static void draw_string(int x, int y, const char *s, unsigned int color) {
    int cx = x;
    for (long i = 0; s[i]; i++) {
        draw_char(cx, y, s[i], color);
        cx += CHAR_W;
        if (cx > SCREEN_W - CHAR_W) break;
    }
}

void _start(void) {
    puts_("fb_term2: starting\n");

    /* step 1: open fb0 */
    long fd = open_("/dev/fb0", O_RDWR);
    puts_hex("fb0 fd=", fd);
    if (fd < 0) { puts_("FAIL fb0\n"); sys6(SYS_exit, 1,0,0,0,0,0); }

    /* step 2: UNBLANK - this triggers the display enable */
    puts_("unblanking...\n");
    sys3(SYS_ioctl, fd, FBIOPUT_BLANK, 0);
    {
        long b = open_("/sys/class/graphics/fb0/blank", 1);
        if (b >= 0) { sys3(SYS_write, b, (long)"0\n", 2); sys3(SYS_close, b, 0, 0); }
        long d = open_("/sys/class/drm/card0-DSI-1/dpms", 1);
        if (d >= 0) { sys3(SYS_write, d, (long)"on\n", 3); sys3(SYS_close, d, 0, 0); }
        long e = open_("/sys/class/drm/card0-DSI-1/enabled", 1);
        if (e >= 0) { sys3(SYS_write, e, (long)"1\n", 2); sys3(SYS_close, e, 0, 0); }
    }
    puts_("display unblanked\n");

    /* step 3: mmap the framebuffer */
    unsigned char fix_buf[160];
    for (int i = 0; i < 160; i++) fix_buf[i] = 0;
    sys3(SYS_ioctl, fd, FBIOGET_FSCREENINFO, (long)fix_buf);
    unsigned long smem_len = *(unsigned long *)(fix_buf + 16);
    puts_hex("smem_len=", smem_len);
    if (smem_len == 0 || smem_len > 0x10000000) smem_len = 1088L * 7200L * 4;

    long mem = sys6(SYS_mmap, 0, smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    puts_hex("mmap=", mem);
    if (mem < 0 && mem > -4096) {
        puts_("FAIL: mmap\n");
        sys6(SYS_exit, 1,0,0,0,0,0);
    }
    fb_mem = (unsigned int *)mem;

    /* verify: read the first pixel */
    unsigned int test = *(unsigned int *)mem;
    puts_hex("first pixel=", test);

    /* step 4: draw the Omarchy UI */
    puts_("drawing UI...\n");
    fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
    /* header */
    fill_rect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER);
    fill_rect(0, HEADER_H - 3, SCREEN_W, 3, COLOR_GREEN);
    draw_string(20, 30, "OMARCHY", COLOR_GREEN);
    draw_string(20 + 8*CHAR_W, 30, "LINUX", COLOR_TEXT);
    draw_string(20, 75, "SD ROOT", COLOR_TEXT);
    draw_string(140, 75, "NHSTUB", COLOR_TEXT);
    draw_string(260, 75, "TERM", COLOR_GREEN);
    /* terminal body */
    fill_rect(0, TERM_TOP, SCREEN_W, TERM_H, COLOR_BG);
    fill_rect(0, TERM_TOP, 4, TERM_H, COLOR_GREEN);
    const char *lines[] = {
        "OMARCHY 262.1 aarch64 MT6833",
        "SD root nhstub kernel 4.14",
        "",
        "# uname -a",
        "Linux omamobile 4.14.357",
        "  openela aarch64",
        "",
        "# systemctl",
        "  manager running",
        "  journal started",
        "  udevd running",
        "  sshd running",
        "",
        "# display",
        "  fb0 1080x2400 32bpp",
        "  DSI-1 connected",
        "  DPMS ON",
        "",
        "# _",
    };
    int nl = sizeof(lines)/sizeof(lines[0]);
    for (int i = 0; i < nl && i < (TERM_H-40)/CHAR_H; i++)
        draw_string(20, TERM_TOP + 20 + i * CHAR_H, lines[i], COLOR_TEXT);
    /* launcher */
    fill_rect(0, SCREEN_H - LAUNCHER_H, SCREEN_W, LAUNCHER_H, COLOR_LAUNCHER);
    fill_rect(0, SCREEN_H - LAUNCHER_H, SCREEN_W, 2, COLOR_BORDER);
    int bw = SCREEN_W / 5;
    int by = SCREEN_H - LAUNCHER_H + 20;
    int bs = 56;
    fill_rect(bw*0+bw/2-bs/2, by, bs, bs, COLOR_GREEN);
    draw_string(bw*0+bw/2-20, by+bs+10, "TERM", COLOR_TEXT);
    fill_rect(bw*1+bw/2-bs/2, by, bs, bs, COLOR_ACCENT);
    draw_string(bw*1+bw/2-22, by+bs+10, "CONF", COLOR_TEXT);
    fill_rect(bw*2+bw/2-bs/2, by, bs, bs, 0xFF806040);
    draw_string(bw*2+bw/2-14, by+bs+10, "FS", COLOR_TEXT);
    fill_rect(bw*3+bw/2-bs/2, by, bs, bs, 0xFF804060);
    draw_string(bw*3+bw/2-14, by+bs+10, "PKG", COLOR_TEXT);
    fill_rect(bw*4+bw/2-bs/2, by, bs, bs, COLOR_RED);
    draw_string(bw*4+bw/2-20, by+bs+10, "PWR", COLOR_TEXT);

    puts_("UI drawn!\n");

    /* loop: echo the serial input to the terminal */
    puts_("terminal ready\n");
    while (1) {
        char c;
        long r = sys3(SYS_read, 0, (long)&c, 1);
        if (r != 1) continue;
        if (c >= 32 && c < 127) {
            /* echo to the terminal at the bottom area */
            draw_char(20, TERM_BOT - 30, c, COLOR_GREEN);
        }
    }

    sys6(SYS_exit, 0,0,0,0,0,0);
}
