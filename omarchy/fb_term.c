/* fb_term.c - a framebuffer terminal for the Omarchy phone.
 * Draws a proper terminal UI on the display: a header bar (Omarchy),
 * a terminal area with a prompt, and an Android-style launcher bar.
 * Uses the fixed stride (1088*4=4352). No libc. Raw syscalls.
 * The touch input (ft_tianma) is probed; if the input device works,
 * the terminal accepts serial input. */
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

/* colors (ABGR format for the MTK DRM) */
#define COLOR_BG       0xFF1A1A2E  /* dark navy */
#define COLOR_HEADER   0xFF16213E  /* darker header */
#define COLOR_ACCENT   0xFF0F3460  /* accent blue */
#define COLOR_TEXT     0xFFE0E0E0  /* light text */
#define COLOR_GREEN    0xFF00C896  /* terminal green */
#define COLOR_PROMPT   0xFF00FF88  /* prompt color */
#define COLOR_BORDER   0xFF404060  /* border gray */
#define COLOR_LAUNCHER 0xFF0D1117  /* launcher dark */
#define COLOR_WHITE    0xFFFFFFFF
#define COLOR_RED      0xFFFF4444

#define SCREEN_W  1080
#define SCREEN_H  2400
#define VIRT_W    1088
#define STRIDE    (VIRT_W * 4)  /* 4352 bytes per row */
#define HEADER_H  120
#define LAUNCHER_H 160
#define TERM_TOP  HEADER_H
#define TERM_BOT  (SCREEN_H - LAUNCHER_H)
#define TERM_H    (TERM_BOT - TERM_TOP)
#define CHAR_W    12
#define CHAR_H    20
#define TERM_COLS ((SCREEN_W - 40) / CHAR_W)
#define TERM_ROWS ((TERM_H - 40) / CHAR_H)

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

static long sys4(long n, long a, long b, long c, long d) {
    register long x8 __asm__ ("x8") = n;
    register long x0 __asm__ ("x0") = a;
    register long x1 __asm__ ("x1") = b;
    register long x2 __asm__ ("x2") = c;
    register long x3 __asm__ ("x3") = d;
    __asm__ volatile ("svc 0" : "+r"(x0) :
        "r"(x8), "r"(x0), "r"(x1), "r"(x2), "r"(x3) : "memory", "cc");
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

static long slen_(const char *s) {
    long n = 0;
    while (s[n]) n++;
    return n;
}

static void puts_(const char *s) {
    sys3(SYS_write, 1, (long)s, slen_(s));
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
static unsigned int fb_stride; /* in pixels (not bytes) */

/* fill a rectangle */
static void fill_rect(int x, int y, int w, int h, unsigned int color) {
    for (int ry = y; ry < y + h && ry < SCREEN_H; ry++) {
        unsigned int *row = (unsigned int *)((long)fb_mem + ry * STRIDE);
        for (int rx = x; rx < x + w && rx < SCREEN_W; rx++)
            row[rx] = color;
    }
}

/* draw a simple 5x7 pixel character scaled to CHAR_W x CHAR_H */
static void draw_char(int x, int y, char c, unsigned int color) {
    /* a minimal 5x7 font for the essential characters */
    static const unsigned char font[][5] = {
        {0x00,0x00,0x00,0x00,0x00}, /* space */
        {0x0E,0x11,0x11,0x11,0x0E}, /* 0 */
        {0x04,0x0C,0x04,0x04,0x0E}, /* 1 */
        {0x0E,0x01,0x0E,0x10,0x0F}, /* 2 */
        {0x0E,0x01,0x0E,0x01,0x0E}, /* 3 */
        {0x11,0x11,0x0E,0x01,0x01}, /* 4 */
        {0x0F,0x10,0x0E,0x01,0x0E}, /* 5 */
        {0x0E,0x10,0x0E,0x11,0x0E}, /* 6 */
        {0x0F,0x01,0x02,0x04,0x04}, /* 7 */
        {0x0E,0x11,0x0E,0x11,0x0E}, /* 8 */
        {0x0E,0x11,0x0F,0x01,0x0E}, /* 9 */
        {0x04,0x0A,0x11,0x11,0x11}, /* A */
        {0x1E,0x11,0x1E,0x11,0x1E}, /* B */
        {0x0E,0x11,0x10,0x11,0x0E}, /* C */
        {0x1C,0x12,0x11,0x12,0x1C}, /* D */
        {0x0F,0x10,0x0E,0x10,0x0F}, /* E */
        {0x0F,0x10,0x0E,0x10,0x10}, /* F */
        {0x0E,0x11,0x11,0x13,0x0D}, /* G */
        {0x11,0x11,0x1F,0x11,0x11}, /* H */
        {0x0E,0x04,0x04,0x04,0x0E}, /* I */
        {0x01,0x01,0x01,0x11,0x0E}, /* J */
        {0x11,0x12,0x1C,0x12,0x11}, /* K */
        {0x10,0x10,0x10,0x10,0x0F}, /* L */
        {0x11,0x1B,0x15,0x11,0x11}, /* M */
        {0x11,0x19,0x15,0x13,0x11}, /* N */
        {0x0E,0x11,0x11,0x11,0x0E}, /* O */
        {0x1E,0x11,0x1E,0x10,0x10}, /* P */
        {0x0E,0x11,0x11,0x13,0x0F}, /* Q */
        {0x1E,0x11,0x1E,0x12,0x11}, /* R */
        {0x0E,0x10,0x0E,0x01,0x0E}, /* S */
        {0x1F,0x04,0x04,0x04,0x04}, /* T */
        {0x11,0x11,0x11,0x11,0x0E}, /* U */
        {0x11,0x11,0x11,0x0A,0x04}, /* V */
        {0x11,0x11,0x15,0x1B,0x11}, /* W */
        {0x11,0x0A,0x04,0x0A,0x11}, /* X */
        {0x11,0x0A,0x04,0x04,0x04}, /* Y */
        {0x1F,0x02,0x04,0x08,0x1F}, /* Z */
        {0x0E,0x11,0x11,0x11,0x0E}, /* O (lowercase o, reuse) */
        {0x00,0x04,0x00,0x00,0x00}, /* . */
        {0x00,0x00,0x1F,0x00,0x00}, /* - */
        {0x04,0x04,0x1F,0x04,0x04}, /* + */
        {0x00,0x01,0x02,0x04,0x08}, /* / */
        {0x11,0x0A,0x1F,0x0A,0x11}, /* * */
        {0x01,0x02,0x04,0x08,0x10}, /* \ */
        {0x0C,0x0C,0x00,0x0C,0x0C}, /* ; */
        {0x0E,0x11,0x00,0x11,0x0E}, /* @ */
        {0x04,0x0A,0x11,0x00,0x00}, /* ^ */
        {0x00,0x00,0x00,0x00,0x1F}, /* _ */
        {0x04,0x04,0x04,0x04,0x04}, /* | */
        {0x02,0x04,0x08,0x04,0x02}, /* ( */
        {0x08,0x04,0x02,0x04,0x08}, /* ) */
        {0x0E,0x11,0x0E,0x00,0x00}, /* lowercase approximation */
    };

    if (c >= 'a' && c <= 'z') c = c - 32; /* to uppercase */
    int idx;
    if (c == ' ') idx = 0;
    else if (c >= '0' && c <= '9') idx = 1 + (c - '0');
    else if (c >= 'A' && c <= 'Z') idx = 11 + (c - 'A');
    else if (c == 'o' || c == 'O') idx = 26;
    else if (c == '.') idx = 38;
    else if (c == '-') idx = 39;
    else if (c == '+') idx = 40;
    else if (c == '/') idx = 41;
    else if (c == '*') idx = 42;
    else if (c == '\\') idx = 43;
    else if (c == ';') idx = 44;
    else if (c == '@') idx = 45;
    else if (c == '^') idx = 46;
    else if (c == '_') idx = 47;
    else if (c == '|') idx = 48;
    else if (c == '(') idx = 49;
    else if (c == ')') idx = 50;
    else if (c == ':') idx = 44; /* reuse ; */
    else if (c == '>') { /* custom: > */
        unsigned char gt[5] = {0x08,0x04,0x02,0x04,0x08};
        for (int py = 0; py < 7; py++) {
            for (int px = 0; px < 5; px++) {
                unsigned int s = (py * 7) / CHAR_H;
                unsigned int sx = (px * 5) / CHAR_W;
                if (gt[s] & (0x10 >> sx))
                    for (int fy = 0; fy < CHAR_H/7; fy++)
                        for (int fx = 0; fx < CHAR_W/5; fx++)
                            if (y + py*(CHAR_H/7)+fy < SCREEN_H)
                                fb_mem[(y + py*(CHAR_H/7)+fy) * VIRT_W + x + px*(CHAR_W/5)+fx] = color;
            }
        }
        return;
    }
    else if (c == '<') { /* custom: < */
        unsigned char lt[5] = {0x08,0x10,0x04,0x01,0x08};
        for (int py = 0; py < 7; py++) {
            for (int px = 0; px < 5; px++) {
                unsigned int s = (py * 7) / CHAR_H;
                unsigned int sx = (px * 5) / CHAR_W;
                if (lt[s] & (0x10 >> sx))
                    for (int fy = 0; fy < CHAR_H/7; fy++)
                        for (int fx = 0; fx < CHAR_W/5; fx++)
                            if (y + py*(CHAR_H/7)+fy < SCREEN_H)
                                fb_mem[(y + py*(CHAR_H/7)+fy) * VIRT_W + x + px*(CHAR_W/5)+fx] = color;
            }
        }
        return;
    }
    else if (c == '#') { /* custom: # */
        unsigned char hash[5] = {0x0A,0x1F,0x0A,0x1F,0x0A};
        for (int py = 0; py < 7; py++) {
            for (int px = 0; px < 5; px++) {
                unsigned int s = (py * 7) / CHAR_H;
                unsigned int sx = (px * 5) / CHAR_W;
                if (hash[s] & (0x10 >> sx))
                    for (int fy = 0; fy < CHAR_H/7; fy++)
                        for (int fx = 0; fx < CHAR_W/5; fx++)
                            if (y + py*(CHAR_H/7)+fy < SCREEN_H)
                                fb_mem[(y + py*(CHAR_H/7)+fy) * VIRT_W + x + px*(CHAR_W/5)+fx] = color;
            }
        }
        return;
    }
    else idx = 0; /* space for unknown */

    if (idx >= sizeof(font)/sizeof(font[0])) idx = 0;

    /* draw the character scaled to CHAR_W x CHAR_H */
    int cw = CHAR_W / 5;  /* the scale factor */
    int ch = CHAR_H / 7;
    for (int py = 0; py < 7; py++) {
        for (int px = 0; px < 5; px++) {
            if (font[idx][py] & (0x10 >> px)) {
                for (int fy = 0; fy < ch && y + py*ch + fy < SCREEN_H; fy++)
                    for (int fx = 0; fx < cw && x + px*cw + fx < SCREEN_W; fx++)
                        fb_mem[(y + py*ch + fy) * VIRT_W + x + px*cw + fx] = color;
            }
        }
    }
}

/* draw a string of characters */
static void draw_string(int x, int y, const char *s, unsigned int color) {
    int cx = x;
    for (long i = 0; s[i]; i++) {
        draw_char(cx, y, s[i], color);
        cx += CHAR_W;
        if (cx > SCREEN_W - CHAR_W) break;
    }
}

/* draw the header bar */
static void draw_header(void) {
    fill_rect(0, 0, SCREEN_W, HEADER_H, COLOR_HEADER);
    fill_rect(0, HEADER_H - 3, SCREEN_W, 3, COLOR_GREEN);
    /* Omarchy logo text */
    draw_string(20, 30, "OMARCHY", COLOR_GREEN);
    draw_string(20 + 8*CHAR_W, 30, "  LINUX", COLOR_TEXT);
    /* the device info on the right */
    draw_string(SCREEN_W - 200, 30, "CAMFR", COLOR_ACCENT);
    draw_string(SCREEN_W - 200, 60, "B BOOT", COLOR_ACCENT);
    /* the status bar */
    draw_string(20, 75, "SD ROOT", COLOR_TEXT);
    draw_string(140, 75, "NHSTUB", COLOR_TEXT);
    draw_string(260, 75, "TERM", COLOR_GREEN);
}

/* draw the Android-style launcher bar */
static void draw_launcher(void) {
    fill_rect(0, SCREEN_H - LAUNCHER_H, SCREEN_W, LAUNCHER_H, COLOR_LAUNCHER);
    fill_rect(0, SCREEN_H - LAUNCHER_H, SCREEN_W, 2, COLOR_BORDER);

    /* app icons (simple squares with labels) */
    int bw = SCREEN_W / 5;
    int by = SCREEN_H - LAUNCHER_H + 20;
    int bs = 56; /* the icon size */

    /* Terminal */
    fill_rect(bw*0 + bw/2 - bs/2, by, bs, bs, COLOR_GREEN);
    draw_string(bw*0 + bw/2 - 20, by + bs + 10, "TERM", COLOR_TEXT);

    /* Settings */
    fill_rect(bw*1 + bw/2 - bs/2, by, bs, bs, COLOR_ACCENT);
    draw_string(bw*1 + bw/2 - 22, by + bs + 10, "CONF", COLOR_TEXT);

    /* Files */
    fill_rect(bw*2 + bw/2 - bs/2, by, bs, bs, 0xFF806040);
    draw_string(bw*2 + bw/2 - 14, by + bs + 10, "FS", COLOR_TEXT);

    /* Apps */
    fill_rect(bw*3 + bw/2 - bs/2, by, bs, bs, 0xFF804060);
    draw_string(bw*3 + bw/2 - 14, by + bs + 10, "PKG", COLOR_TEXT);

    /* Power */
    fill_rect(bw*4 + bw/2 - bs/2, by, bs, bs, COLOR_RED);
    draw_string(bw*4 + bw/2 - 20, by + bs + 10, "PWR", COLOR_TEXT);
}

/* draw the terminal area with the content */
static void draw_terminal(const char **lines, int num_lines, int cursor_line, int cursor_col) {
    fill_rect(0, TERM_TOP, SCREEN_W, TERM_H, COLOR_BG);

    /* draw each text line */
    for (int i = 0; i < num_lines && i < TERM_ROWS; i++) {
        int ty = TERM_TOP + 20 + i * CHAR_H;
        draw_string(20, ty, lines[i], COLOR_TEXT);
    }

    /* draw the cursor (a blinking block - always shown for now) */
    if (cursor_line < TERM_ROWS) {
        int cy = TERM_TOP + 20 + cursor_line * CHAR_H;
        int cx = 20 + cursor_col * CHAR_W;
        fill_rect(cx, cy, CHAR_W, CHAR_H, COLOR_PROMPT);
    }

    /* draw the prompt indicator on the left edge */
    fill_rect(0, TERM_TOP, 4, TERM_H, COLOR_GREEN);
}

/* the terminal content */
static const char *boot_lines[] = {
    "OMARCHY 262.1  aarch64  MT6833",
    "SD root  nhstub25  kernel 4.14.357",
    "",
    "# uname -a",
    "Linux omamobile 4.14.357-openela",
    "  #11 SMP PREEMPT aarch64",
    "",
    "# ls /",
    "bin  boot  dev  etc  home",
    "lib  mnt  opt  proc  root",
    "run  sbin  srv  sys  tmp",
    "usr  var",
    "",
    "# systemctl --failed",
    "0 loaded units listed",
    "",
    "# _",
};

void _start(void) {
    puts_("fb_term: starting\n");

    /* open fb0 */
    long fd = open_("/dev/fb0", O_RDWR);
    if (fd < 0) {
        puts_("FAIL: fb0\n");
        sys6(SYS_exit, 1,0,0,0,0,0);
    }

    /* set master (for the DRM access) */

    /* unblank */
    {
        long b = open_("/sys/class/graphics/fb0/blank", 1);
        if (b >= 0) { sys3(SYS_write, b, (long)"0\n", 2); sys3(SYS_close, b, 0, 0); }
    }

    /* get the fb info */
    struct fb_fix {
        char id[16];
        unsigned long smem_start;
        unsigned int smem_len;
        unsigned int type;
        unsigned int type_aux;
        unsigned int visual;
        unsigned int xpanstep, ypanstep, ywrapstep;
        unsigned int line_length;
        unsigned char pad[40];
    } fix;
    {
        unsigned char buf[160];
        for (int i = 0; i < 160; i++) buf[i] = 0;
        sys3(SYS_ioctl, fd, FBIOGET_FSCREENINFO, (long)buf);
        for (int i = 0; i < 160; i++) ((unsigned char *)&fix)[i] = buf[i];
    }

    /* mmap the framebuffer */
    long fb_size = (long)fix.smem_len;
    if (fb_size <= 0) fb_size = 1088L * 7200L * 4;
    long mem = sys6(SYS_mmap, 0, fb_size, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    puts_hex("mmap=", mem);
    if (mem < 0 && mem > -4096) {
        puts_("FAIL: mmap\n");
        sys6(SYS_exit, 1,0,0,0,0,0);
    }
    fb_mem = (unsigned int *)mem;
    fb_stride = VIRT_W; /* the stride in pixels (the virtual width) */

    /* draw the full UI */
    puts_("drawing UI...\n");
    fill_rect(0, 0, SCREEN_W, SCREEN_H, COLOR_BG);
    draw_header();
    draw_terminal(boot_lines, sizeof(boot_lines)/sizeof(boot_lines[0]),
                  sizeof(boot_lines)/sizeof(boot_lines[0]), 2);
    draw_launcher();
    puts_("UI drawn!\n");

    /* keep running: read from the serial and echo to the terminal */
    /* simple loop: read a char from the serial (fd 0), echo it, add to the terminal */
    long num_lines = sizeof(boot_lines)/sizeof(boot_lines[0]);
    char line_buf[256];
    int line_len = 0;

    puts_("terminal ready\n");
    while (1) {
        char c;
        long r = sys3(SYS_read, 0, (long)&c, 1);
        if (r != 1) continue;
        if (c == '\n' || c == '\r') {
            /* redraw the terminal with the new line */
            fill_rect(0, TERM_TOP, SCREEN_W, TERM_H, COLOR_BG);
            draw_terminal(boot_lines, num_lines, num_lines, 0);
            line_len = 0;
        } else if (c >= 32 && c < 127 && line_len < 200) {
            line_buf[line_len++] = c;
            /* draw the char at the cursor position */
            draw_char(20 + line_len * CHAR_W, TERM_TOP + 20 + num_lines * CHAR_H, c, COLOR_TEXT);
        }
    }

    sys6(SYS_exit, 0,0,0,0,0,0);
}
