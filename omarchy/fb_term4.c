/* fb_term4.c - the LIVE Omarchy terminal on the display.
 * - pty + fork + the real bash (chroot'd to the Omarchy rootfs)
 * - a text grid with a minimal ANSI parser, rendered to fb0
 * - the fts_ts touchscreen: a soft keyboard (qwerty + shift/ctrl/esc/arrows)
 * No libc. Raw syscalls. aarch64. Build: tiny.lds + max-page-size=4096. */
#define SYS_openat    56
#define SYS_close     57
#define SYS_read      63
#define SYS_write     64
#define SYS_ioctl     29
#define SYS_mmap      222
#define SYS_exit      93
#define SYS_nanosleep 101
#define SYS_poll      7
#define SYS_wait4     260
#define SYS_pipe2     291
#define SYS_fork      1079
#define SYS_clone     220
#define SYS_setsid    157
#define SYS_execve    221
#define SYS_dup3      24
#define SYS_chroot    51
#define SYS_chdir     12
#define SYS__exit     93

#define O_RDWR 2
#define O_RDONLY 0
#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define FBIOGET_FSCREENINFO 0x4602
#define FBIOPUT_BLANK 0x4611
#define TIOCGPTN 0x80045430
#define TIOCSPTLCK 0x40045431
#define TIOCSCTTY 0x540E

/* layout */
#define SCREEN_W  1080
#define SCREEN_H  2400
#define VIRT_W    1088
#define STRIDE    (VIRT_W * 4)
#define CELL_W    24
#define CELL_H    36
#define COLS      45
#define TERM_TOP  48
#define KB_Y      1748
#define ROWS      ((KB_Y - TERM_TOP - 8) / CELL_H)
#define FONT_S    4            /* the font pixel scale */

/* colors */
#define C_BG      0xFF10141F
#define C_FG      0xFFD8DEE9
#define C_HDR     0xFF16213E
#define C_GREEN   0xFF00C896
#define C_KEY     0xFF232A3C
#define C_KEYON   0xFF00C896
#define C_CURSOR  0xFF00C896

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
    long n = 0; while (s[n]) n++;
    sys3(SYS_write, 1, (long)s, n);
}
static long open_(const char *p, long fl) { return sys3(SYS_openat, -100, (long)p, fl); }
static void die(const char *m) { puts_(m); sys6(SYS_exit, 1,0,0,0,0,0); }

void *memcpy(void *d, const void *s, unsigned long n) {
    unsigned char *dd = d; const unsigned char *ss = s;
    while (n--) *dd++ = *ss++;
    return d;
}
static void *memzero(void *d, unsigned long n) {
    unsigned char *dd = d;
    while (n--) *dd++ = 0;
    return d;
}
static long slen(const char *s) { long n = 0; while (s[n]) n++; return n; }
static void puts_hex2(const char *tag, long v) {
    char buf[40]; int i = 0;
    while (tag[i]) { buf[i] = tag[i]; i++; }
    buf[i++] = '0'; buf[i++] = 'x';
    for (int sh = 60; sh >= 0; sh -= 4) {
        int d = (int)((v >> sh) & 0xf);
        buf[i++] = d < 10 ? '0' + d : 'a' + d - 10;
    }
    buf[i++] = '\n';
    sys3(SYS_write, 1, (long)buf, i);
}

/* the framebuffer */
static unsigned int *fb;
static void fill_rect(int x, int y, int w, int h, unsigned int c) {
    for (int ry = y; ry < y + h && ry < SCREEN_H; ry++) {
        unsigned int *row = (unsigned int *)((long)fb + (long)ry * STRIDE);
        for (int rx = x; rx < x + w && rx < SCREEN_W; rx++) row[rx] = c;
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
static void draw_char(int x, int y, char c, unsigned int col) {
    if (c >= 'a' && c <= 'z') c = c - 32;
    int idx = 0;
    if (c >= '0' && c <= '9') idx = 1 + (c - '0');
    else if (c >= 'A' && c <= 'Z') idx = 11 + (c - 'A');
    else if (c == '.') idx = 37;
    else if (c == '-') idx = 38;
    else if (c == '+') idx = 39;
    else if (c == '/') idx = 40;
    else if (c == '*' || c == '#') idx = 41;
    else if (c == '_') idx = 46;
    else if (c == '>') idx = 49;
    else if (c == '<') idx = 48;
    if (idx >= (int)(sizeof(font)/sizeof(font[0]))) idx = 0;
    for (int py = 0; py < 5; py++)
        for (int px = 0; px < 5; px++)
            if (font[idx][py] & (0x10 >> px))
                for (int fy = 0; fy < FONT_S; fy++)
                    for (int fx = 0; fx < FONT_S; fx++) {
                        int yy = y + py*FONT_S + fy, xx = x + px*FONT_S + fx;
                        if (yy < SCREEN_H && xx < SCREEN_W)
                            fb[(long)yy * VIRT_W + xx] = col;
                    }
}
static void draw_str(int x, int y, const char *s, unsigned int col) {
    for (long i = 0; s[i]; i++) { draw_char(x, y, s[i], col); x += 6 * FONT_S; }
}

/* the text grid */
static char grid[ROWS][COLS];
static int cx = 0, cy = 0;

static void scroll_up(void) {
    memcpy(grid[0], grid[1], (ROWS-1)*COLS);
    memzero(grid[ROWS-1], COLS);
}
static void put_cell(char c) {
    if (c < 0x20 || c > 0x7e) c = '?';
    grid[cy][cx] = c;
    cx++;
    if (cx >= COLS) { cx = 0; cy++; if (cy >= ROWS) { cy = ROWS-1; scroll_up(); } }
}
static void do_newline(void) {
    cy++;
    if (cy >= ROWS) { cy = ROWS-1; scroll_up(); }
}

/* render: the whole terminal area + the cursor */
static void render(void) {
    fill_rect(0, 0, SCREEN_W, TERM_TOP, C_HDR);
    fill_rect(0, TERM_TOP-3, SCREEN_W, 3, C_GREEN);
    draw_str(8, 10, "OMARCHY", C_GREEN);
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int px = c * CELL_W, py = TERM_TOP + r * CELL_H;
            fill_rect(px, py, CELL_W, CELL_H, C_BG);
            if (grid[r][c] != 0 && grid[r][c] != ' ')
                draw_char(px + 2, py + 4, grid[r][c], C_FG);
        }
    }
    /* the cursor: an underline in the current cell */
    if (cx < COLS && cy < ROWS)
        fill_rect(cx*CELL_W, TERM_TOP + cy*CELL_H + CELL_H - 6, CELL_W-2, 4, C_CURSOR);
}

/* the ANSI parser state */
static int esc = 0, csi = 0, p1 = 0, p2 = 0, got1 = 0;

static void feed(char c) {
    if (esc) {
        if (c == '[') { csi = 1; p1 = 0; p2 = 0; got1 = 0; return; }
        if (c == 'c') { memzero(grid, sizeof(grid)); cx = 0; cy = 0; }
        esc = 0; return;
    }
    if (csi) {
        if (c >= '0' && c <= '9') { if (!got1) p1 = p1*10 + (c-'0'); else p2 = p2*10 + (c-'0'); return; }
        if (c == ';') { got1 = 1; return; }
        csi = 0;
        if (c == 'A') { cy -= (p1 ? p1 : 1); if (cy < 0) cy = 0; }
        else if (c == 'B') { cy += (p1 ? p1 : 1); if (cy >= ROWS) cy = ROWS-1; }
        else if (c == 'C') { cx += (p1 ? p1 : 1); if (cx >= COLS) cx = COLS-1; }
        else if (c == 'D') { cx -= (p1 ? p1 : 1); if (cx < 0) cx = 0; }
        else if (c == 'H' || c == 'f') { cx = got1 ? p2 : 0; cy = p1 ? p1-1 : 0;
            if (cx >= COLS) cx = COLS-1; if (cy >= ROWS) cy = ROWS-1;
            if (cx < 0) cx = 0; if (cy < 0) cy = 0; }
        else if (c == 'J') { memzero(grid, sizeof(grid)); cx = 0; cy = 0; }
        else if (c == 'K') { for (int i = cx; i < COLS; i++) grid[cy][i] = 0; }
        /* m (SGR) and anything else: ignored */
        return;
    }
    if (c == 0x1b) { esc = 1; return; }
    if (c == '\n') { do_newline(); return; }
    if (c == '\r') { cx = 0; return; }
    if (c == '\b') { if (cx > 0) cx--; return; }
    if (c == '\t') { cx = (cx + 4) & ~3; if (cx >= COLS) { cx = 0; do_newline(); } return; }
    if (c == 0x07) return; /* BEL */
    put_cell(c);
}

/* the soft keyboard */
#define KB_ROWS 4
#define KB_COLS 10
#define KB_CW   (SCREEN_W / KB_COLS)
#define KB_CH   ((SCREEN_H - KB_Y) / KB_ROWS)
static const char *kb[KB_ROWS] = {
    "qwertyuiop",
    "asdfghjkl-",
    "zxcvbnm.,/",
    "         "
};
static int shift = 0, ctrl = 0;
static int inpipe[2], outpipe[2];   /* the shell transport pipes */
static long pm = -1;   /* the pty master - the keyboard writes here */

static void draw_kb(void) {
    fill_rect(0, KB_Y, SCREEN_W, SCREEN_H - KB_Y, 0xFF0B0E18);
    fill_rect(0, KB_Y, SCREEN_W, 2, 0xFF404060);
    for (int r = 0; r < KB_ROWS; r++) {
        for (int c = 0; c < KB_COLS; c++) {
            int x = c * KB_CW, y = KB_Y + 2 + r * KB_CH;
            fill_rect(x+2, y+4, KB_CW-4, KB_CH-8, C_KEY);
            char k = kb[r][c];
            char lbl[2]; lbl[1] = 0;
            if (r == 2 && c == 0) { draw_str(x+6, y+18, "SHF", shift ? C_KEYON : C_FG); continue; }
            if (r == 3 && c == 0) { draw_str(x+6, y+18, "CTL", ctrl ? C_KEYON : C_FG); continue; }
            if (r == 3 && c == 1) { draw_str(x+6, y+18, "ESC", C_FG); continue; }
            if (r == 3 && c >= 2 && c <= 6) { continue; } /* the space bar strip */
            if (r == 3 && c == 7) { draw_str(x+6, y+18, "RET", C_GREEN); continue; }
            if (r == 3 && c == 8) { draw_str(x+6, y+18, "UP", C_FG); continue; }
            if (r == 3 && c == 9) { draw_str(x+6, y+18, "DN", C_FG); continue; }
            if (r == 2 && c == 9) { draw_str(x+6, y+18, "<-", C_FG); continue; }
            if (k == ' ') { continue; }
            lbl[0] = shift ? (k >= 'a' && k <= 'z' ? k-32 : k) : k;
            draw_char(x + KB_CW/2 - 8, y + 16, lbl[0], C_FG);
        }
    }
}

/* write a char (or a sequence) to the pty */
static void send_key(int r, int c) {
    char k = kb[r][c];
    char seq[4]; long n = 0;
    if (r == 2 && c == 0) { shift = !shift; draw_kb(); return; }
    if (r == 3 && c == 0) { ctrl = !ctrl; draw_kb(); return; }
    if (r == 3 && c == 1) { seq[n++] = 0x1b; }                       /* ESC */
    else if (r == 3 && c >= 2 && c <= 6) { seq[n++] = ' '; }         /* space */
    else if (r == 3 && c == 7) { seq[n++] = '\r'; }                  /* enter */
    else if (r == 3 && c == 8) { seq[n++] = 0x1b; seq[n++] = '['; seq[n++] = 'A'; }
    else if (r == 3 && c == 9) { seq[n++] = 0x1b; seq[n++] = '['; seq[n++] = 'B'; }
    else if (r == 2 && c == 9) { seq[n++] = 0x7f; }                  /* backspace */
    else {
        if (k == ' ') { seq[n++] = ' '; }
        else {
            char ch = (shift && k >= 'a' && k <= 'z') ? k-32 : k;
            seq[n++] = ctrl ? (ch & 0x1f) : ch;
        }
    }
    if (!n) return;
    shift = 0; ctrl = 0;
    if (inpipe[1] >= 0) sys3(SYS_write, inpipe[1], (long)seq, n);
}

void _start(void) {
    puts_("fb_term4: starting\n");

    long fd = open_("/dev/fb0", O_RDWR);
    if (fd < 0) die("FAIL fb0\n");
    sys3(SYS_ioctl, fd, FBIOPUT_BLANK, 0);
    {
        long b = open_("/sys/class/graphics/fb0/blank", 1);
        if (b >= 0) { sys3(SYS_write, b, (long)"0\n", 2); sys3(SYS_close, b, 0, 0); }
    }
    unsigned char fix_buf[160];
    memzero(fix_buf, 160);
    sys3(SYS_ioctl, fd, FBIOGET_FSCREENINFO, (long)fix_buf);
    unsigned long smem_len = *(unsigned int *)(fix_buf + 24);
    if (smem_len == 0 || smem_len > 0x10000000) smem_len = 1088L * 7200L * 4;
    long mem = sys6(SYS_mmap, 0, (long)smem_len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (mem < 0 && mem > -4096) {
        /* the fbdev sometimes refuses the full remap after another process
         * released it - fall back to the visible area, then to a small map */
        puts_hex2("mmap retry small", mem);
        mem = sys6(SYS_mmap, 0, 1088L * 2400L * 4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    }
    if (mem < 0 && mem > -4096) {
        mem = sys6(SYS_mmap, 0, 1088L * 400L * 4, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    }
    puts_hex2("mmap=", mem);
    if (mem < 0 && mem > -4096) die("FAIL mmap\n");
    fb = (unsigned int *)mem;

    /* the transport: TWO PIPES - the pty/devpts is broken on this kernel
     * (the master/slave never flow - boots #124-#129). The input pipe =
     * the keyboard/serial; the output pipe = the display. The sh = static
     * busybox /bin/sh from the initramfs - no chroot, no dynamic linker. */
    sys3(SYS_pipe2, (long)inpipe, 0, 0);
    sys3(SYS_pipe2, (long)outpipe, 0, 0);
    puts_hex2("inpipe1=", inpipe[1]); puts_hex2("outpipe0=", outpipe[0]);
    long pid = sys6(SYS_clone, 17 /* SIGCHLD */, 0, 0, 0, 0, 0);
    if (pid < 0) { puts_hex2("clone fail", pid); die("FAIL clone\n"); }
    if (pid == 0) {
        /* the child: the session + the stdio = the pipes + the static sh */
        sys3(SYS_setsid, 0,0,0);
        sys3(SYS_close, 0, 0, 0);
        sys3(SYS_close, 1, 0, 0);
        sys3(SYS_close, 2, 0, 0);
        sys3(SYS_dup3, inpipe[0], 0, 0);
        sys3(SYS_dup3, outpipe[1], 1, 0);
        sys3(SYS_dup3, outpipe[1], 2, 0);
        sys3(SYS_close, inpipe[0], 0, 0);
        sys3(SYS_close, inpipe[1], 0, 0);
        sys3(SYS_close, outpipe[1], 0, 0);
        sys3(SYS_close, outpipe[0], 0, 0);
        sys3(SYS_close, fd, 0, 0);
        static char *argv2[] = { "sh", 0 };
        static char *envv2[] = { "TERM=dumb", "PATH=/bin:/sbin", "PS1=omarchy> ", 0 };
        sys3(SYS_execve, (long)"/bin/sh", (long)argv2, (long)envv2);
        sys6(SYS__exit, 1, 0,0,0,0,0);
    }
    /* the parent: drop the child's pipe ends */
    sys3(SYS_close, inpipe[0], 0, 0);
    sys3(SYS_close, outpipe[1], 0, 0);
    puts_hex2("child pid=", pid);

    /* the touch input + the axis calibration */
    long tfd = open_("/dev/input/event2", O_RDONLY);
    puts_hex2("touch fd=", tfd);
    int txmin = 0, txmax = 1079, tymin = 0, tymax = 2399;
    if (tfd >= 0) {
        /* EVIOCGABS(0)=X and EVIOCGABS(1)=Y: {value,min,max,fuzz,flat,res} */
        int ai[6];
        memzero(ai, 24);
        sys3(SYS_ioctl, tfd, 0x80404540 + 0, (long)ai);
        txmin = ai[1]; txmax = ai[2];
        memzero(ai, 24);
        sys3(SYS_ioctl, tfd, 0x80404540 + 1, (long)ai);
        tymin = ai[1]; tymax = ai[2];
        puts_hex2("txmin=", txmin); puts_hex2("txmax=", txmax);
        puts_hex2("tymin=", tymin); puts_hex2("tymax=", tymax);
    }
    if (txmax <= txmin) { txmin = 0; txmax = 1079; }
    if (tymax <= tymin) { tymin = 0; tymax = 2399; }

    render();
    draw_kb();

    /* the main loop: poll the pty + the touch */
    static struct { int fd; short events, revents; } pfds[2];
    for (;;) {
        /* the child reaper: log the exit status when the sh dies */
        static int st = 0;
        long wpid = sys6(SYS_wait4, -1, (long)&st, 1 /* WNOHANG */, 0, 0, 0);
        if (wpid == pid) {
            puts_hex2("CHILD EXITED raw status:", st);
            if ((st & 0x7f) == 0) puts_hex2("exit code:", (st >> 8) & 0xff);
            else puts_hex2("killed by signal:", st & 0x7f);
        }
        pfds[0].fd = (int)outpipe[0];  pfds[0].events = 1; pfds[0].revents = 0;
        pfds[1].fd = (int)tfd; pfds[1].events = 1; pfds[1].revents = 0;
        long r = sys3(SYS_poll, (long)pfds, 2, 2000);
        if (r < 0) { sys3(SYS_nanosleep, 0, 0, 0); continue; }
        int dirty = 0;
        if (pfds[0].revents & 1) {
            char buf[512];
            long n = sys3(SYS_read, outpipe[0], (long)buf, 512);
            if (n == 0) {
                /* the pty master EOF = the child died - stop the hot spin */
                static struct { long sec, nsec; } ts1 = { 1, 0 };
                sys3(SYS_nanosleep, (long)&ts1, 0, 0);
                continue;
            }
            for (long i = 0; i < n; i++) feed(buf[i]);
            if (n > 0) dirty = 1;
            /* the diagnostic: the first 10 master reads = logged */
            static int mreads = 0;
            if (mreads < 10 && n > 0) {
                mreads++;
                puts_hex2("mread#", n);
                buf[n < 60 ? n : 60] = 0;
                for (long i = 0; i < (n < 60 ? n : 60); i++)
                    if (buf[i] < 0x20 || buf[i] > 0x7e) buf[i] = '.';
                puts_(buf); puts_("\n");
            }
        }
        if (tfd >= 0 && (pfds[1].revents & 1)) {
            static unsigned char evbuf[32*24];
            static int down = 0;
            long n = sys3(SYS_read, tfd, (long)evbuf, 32*24);
            int tx = -1, ty = -1, lift = 0;
            static int evcount = 0;
            for (long off = 0; off + 24 <= n; off += 24) {
                short type = *(short *)(evbuf + off + 16);
                short code = *(short *)(evbuf + off + 18);
                int val = *(int *)(evbuf + off + 20);
                if (type == 3 && code == 0x35) tx = val;
                else if (type == 3 && code == 0x36) ty = val;
                else if (type == 1 && code == 0x14a) {
                    if (val == 1) { down = 1; evcount = 0; tx = -1; ty = -1; }
                    else if (down) { lift = 1; down = 0; }
                }
                if (down) evcount++;
            }
            /* only a REAL down->lift transition is a tap; the panel streams
             * phantom single-event glitches - a real touch = many events */
            if (lift && evcount >= 3 && tx >= 0 && ty >= 0) {
                /* the raw axis values -> the screen pixels */
                int px = (tx - txmin) * SCREEN_W / (txmax - txmin);
                int py = (ty - tymin) * SCREEN_H / (tymax - tymin);
                if (py >= KB_Y) {
                    int kr = (py - KB_Y - 2) / KB_CH;
                    int kc = px / KB_CW;
                    if (kr >= 0 && kr < KB_ROWS && kc >= 0 && kc < KB_COLS) {
                        if (kr == 3 && kc >= 2 && kc <= 6) kc = 4; /* the space strip */
                        send_key(kr, kc);
                        dirty = 1;
                    }
                }
            }
        }
        if (dirty) {
            render();
            /* the throttle: the phantom-tap storms cannot spin the CPU -
             * the serial RX dies under the render load */
            static struct { long sec, nsec; } tsr = { 0, 300000000 };
            sys3(SYS_nanosleep, (long)&tsr, 0, 0);
        }
    }
}
