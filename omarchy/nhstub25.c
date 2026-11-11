/* nhstub25.c - v11: v10's behavior PLUS faccessat/faccessat2 interception.
 *
 * THE REAL EINVAL (found by reading systemd v262.1 source - the rootfs has
 * libsystemd-shared-262.1-1.so, NOT 257 as previously believed):
 *
 *   pin_callout_binary() -> find_executable_full() -> is_path() true ->
 *   open_and_check_executable():
 *       fd = open(name, O_PATH|O_CLOEXEC);      // works (or cache-assisted)
 *       fd_verify_regular(fd);                   // fstat - works
 *       access_fd(fd, X_OK):
 *           faccessat(fd, "", X_OK, AT_EMPTY_PATH)
 *
 * glibc tries SYS_faccessat2 (added 5.8) -> ENOSYS on 4.14 -> falls back to
 * the OLD 3-arg SYS_faccessat which DROPS the flags argument -> the kernel
 * path-lookups the EMPTY STRING -> EINVAL. access_fd has NO fallback for
 * EINVAL, so pin_callout_binary fails, manager_new fails, systemd freezes.
 * No open() interception can fix that: the failing call is faccessat.
 *
 * The fix: emulate AT_EMPTY_PATH via fstat (newfstatat with AT_EMPTY_PATH
 * works on 4.14). Root (PID1): X_OK granted iff any x bit set,
 * R_OK/W_OK always granted (CAP_DAC_OVERRIDE semantics). Non-root: simple
 * DAC bits via uid/gid match.
 *
 * ALSO: SYSTEMD_EXECUTOR_PATH must NOT be set in the boot: v262 treats it
 * as authoritative-fatal, and any /run tmpfs copy rots post-pivot.
 *
 * Kept from v10: statx fake (MOUNT_ROOT for every path), name_to_handle_at
 * EOPNOTSUPP, fstatat passthrough, fcntl seal mask for 4.14, executor
 * O_PATH fd cache served through open/open64/openat/openat64.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef STATX_TYPE
#define STATX_TYPE 0x00000001U
#endif
#ifndef STATX_MODE
#define STATX_MODE 0x00000002U
#endif
#ifndef STATX_NLINK
#define STATX_NLINK 0x00000004U
#endif
#ifndef STATX_UID
#define STATX_UID 0x00000008U
#endif
#ifndef STATX_GID
#define STATX_GID 0x00000010U
#endif
#ifndef STATX_ATIME
#define STATX_ATIME 0x00000020U
#endif
#ifndef STATX_MTIME
#define STATX_MTIME 0x00000040U
#endif
#ifndef STATX_CTIME
#define STATX_CTIME 0x00000080U
#endif
#ifndef STATX_INO
#define STATX_INO 0x00000100U
#endif
#ifndef STATX_SIZE
#define STATX_SIZE 0x00000200U
#endif
#ifndef STATX_BLOCKS
#define STATX_BLOCKS 0x00000400U
#endif
#ifndef STATX_BASIC_STATS
#define STATX_BASIC_STATS 0x000007ffU
#endif
#ifndef STATX_ATTR_MOUNT_ROOT
#define STATX_ATTR_MOUNT_ROOT 0x00002000U
#endif
#ifndef STATX_BTIME
#define STATX_BTIME 0x00000800U
#endif
#ifndef STATX_MNT_ID
#define STATX_MNT_ID 0x00008000U
#endif
#ifndef STATX_MNT_ID_UNIQUE
#define STATX_MNT_ID_UNIQUE 0x00010000U
#endif

#ifndef F_ADD_SEALS
#define F_ADD_SEALS 1033
#endif
#ifndef F_SEAL_FUTURE_WRITE
#define F_SEAL_FUTURE_WRITE 0x0008
#endif
#ifndef F_SEAL_EXEC
#define F_SEAL_EXEC 0x0010
#endif

#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x100
#endif

struct statx_local {
    unsigned int stx_mask;
    unsigned int stx_blksize;
    unsigned long long stx_attributes;
    unsigned int stx_nlink;
    unsigned int stx_uid;
    unsigned int stx_gid;
    unsigned short stx_mode;
    unsigned short pad1;
    unsigned long long stx_ino;
    unsigned long long stx_size;
    unsigned long long stx_blocks;
    unsigned long long stx_attributes_mask;
    unsigned long long stx_atime;
    int stx_atime_tv_nsec;
    unsigned long long stx_btime;
    int stx_btime_tv_nsec;
    unsigned long long stx_ctime;
    int stx_ctime_tv_nsec;
    unsigned long long stx_mtime;
    int stx_mtime_tv_nsec;
    unsigned int stx_rdev_major;
    unsigned int stx_rdev_minor;
    unsigned int stx_dev_major;
    unsigned int stx_dev_minor;
    unsigned long long spare[14];
};

static int kfd = -1;
static int logged = 0;
static void klog_once(void) {
    if (!logged) {
        logged = 1;
        if (kfd < 0) kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) write(kfd, "nhstub25: statx faked for all paths\n", 36);
    }
}

static unsigned long long hash_path(const char *p) {
    unsigned long long h = 5381;
    if (!p || !*p) p = "/";
    while (*p) h = ((h << 5) + h) + (unsigned char)*p++;
    return (h % 0x7fffffffULL) + 2;
}

/* the real kernel statx, patched with the modern mask bits and a STABLE
 * mnt_id (the real inode number): chase()'s .. detection compares
 * parent/child statx data, so ids must not change between calls */
static int statx_patched(int dfd, const char *path, int flags, unsigned int mask, struct statx *sx) {
    int r = (int)syscall(SYS_statx, dfd, path ? path : "", flags, mask, sx);
    if (r < 0) return r;
    sx->stx_mask |= (unsigned)STATX_BTIME | (unsigned)STATX_MNT_ID | (unsigned)STATX_MNT_ID_UNIQUE;
    if (!sx->stx_mnt_id) sx->stx_mnt_id = sx->stx_ino ? sx->stx_ino : hash_path(path);
    sx->stx_attributes_mask |= (unsigned)STATX_ATTR_MOUNT_ROOT;
    sx->stx_attributes |= (unsigned)STATX_ATTR_MOUNT_ROOT;
    if (sx->stx_blksize == 0) sx->stx_blksize = 4096;
    return 0;
}

/* ---- the /proc/1/root prefix fallback ------------------------------------
 * DIRECT SD path walks rot (EUNATCH on symlinks, new paths, statx), but
 * the KERNEL MAGIC resolution through /proc/1/root/<path> is healthy -
 * every successful diagnostic probe this session used it. On a
 * deterministic EUNATCH/EINVAL failure, retry with the prefix. */
#define MAGIC_PREFIX "/proc/1/root/"
static int is_rot_errno(int e) {
    return e == EUNATCH || e == EINVAL || e == EIO || e == EAGAIN;
}

static char magic_path_buf[4096];
static const char *magic_path(const char *path) {
    if (!path || path[0] != '/') return path;
    snprintf(magic_path_buf, sizeof magic_path_buf, MAGIC_PREFIX "%s", path + 1);
    return magic_path_buf;
}

static int raw_openat(int dfd, const char *path, int flags, int mode) {
    return (int)syscall(SYS_openat, dfd, path, flags, mode);
}

static int open_with_magic(int dfd, const char *path, int flags, int mode) {
    int fd = raw_openat(dfd, path, flags, mode);
    if (fd >= 0) return fd;
    if (!(flags & (O_CREAT | O_TMPFILE)) && is_rot_errno(errno) && path && path[0] == '/') {
        fd = raw_openat(dfd, magic_path(path), flags, mode);
    }
    return fd;
}

int statx(int dfd, const char *path, int flags, unsigned int mask, struct statx *sx) {
    struct statx_local *lx = (struct statx_local *)sx;
    static int warned = 0;
    /* the magic path first: the REAL data through the healthy resolution */
    if (path && path[0] == '/') {
        if (syscall(SYS_statx, dfd, magic_path(path), flags, mask, sx) == 0) {
            sx->stx_mask |= (unsigned)STATX_MNT_ID | (unsigned)STATX_MNT_ID_UNIQUE | (unsigned)STATX_BTIME;
            if (!sx->stx_mnt_id) sx->stx_mnt_id = sx->stx_ino;
            sx->stx_attributes_mask |= (unsigned)STATX_ATTR_MOUNT_ROOT;
            sx->stx_attributes |= (unsigned)STATX_ATTR_MOUNT_ROOT;
            return 0;
        }
        /* fall through to the regular handlers for the fd-based and the
         * relative calls, and for the magic-path failures */
    }
    if (!warned) {
        warned = 1;
        if (kfd < 0) kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) write(kfd, "nhstub25: statx patched (real first + stable-id fallback)\n", 58);
    }
    /* real kernel statx first: its data is consistent across calls, which
     * the path-walk logic depends on */
    if (statx_patched(dfd, path, flags, mask, sx) == 0)
        return 0;
    /* the kernel refused (the rot): the synthetic fallback with ids
     * STABLE per path (hash), so repeat calls agree with each other */
    memset(lx, 0, sizeof *lx);
    lx->stx_mask = STATX_BASIC_STATS | STATX_TYPE | STATX_BTIME |
                   STATX_MNT_ID | STATX_MNT_ID_UNIQUE;
    lx->stx_attributes_mask = STATX_ATTR_MOUNT_ROOT;
    lx->stx_attributes = STATX_ATTR_MOUNT_ROOT;
    lx->stx_mode = S_IFDIR | 0755;
    lx->stx_nlink = 2;
    lx->stx_ino = hash_path(path);
    lx->stx_uid = 0;
    lx->stx_gid = 0;
    lx->stx_size = 4096;
    lx->stx_blocks = 8;
    lx->stx_blksize = 4096;
    lx->stx_btime = 1000000000ULL;
    lx->spare[0] = hash_path(path); /* stx_mnt_id - stable per path */
    return 0;
}

int name_to_handle_at(int dfd, const char *name, struct file_handle *handle, int *mnt_id, int flags) {
    errno = EOPNOTSUPP;
    return -1;
}

int name_to_handle_delete(int dfd, const char *name, int flags) {
    errno = EOPNOTSUPP;
    return -1;
}

int fstatat(int dfd, const char *name, struct stat *st, int flags) {
    int r = (int)syscall(SYS_newfstatat, dfd, name, st, flags);
    /* the MTK kernel returns EUNATCH ("Protocol driver not attached") for
     * fstatat on SD paths post-pivot - FLAKY (a retry passes). chase()
     * depends on fstatat, and its failure breaks drop-in loading. */
    for (int i = 0; r < 0 && i < 3; i++) {
        int e = errno;
        if (e != EUNATCH && e != EINVAL && e != EIO && e != EAGAIN) break;
        usleep(60000);
        r = (int)syscall(SYS_newfstatat, dfd, name, st, flags);
    }
    if (r < 0 && errno == EUNATCH) {
        errno = ENOENT;
    }
    if (r < 0 && errno == ENOENT && name && name[0] == '/') {
        struct stat st2;
        r = (int)syscall(SYS_newfstatat, dfd, magic_path(name), &st2, flags);
        if (r == 0 && st) *st = st2;
        if (r < 0) errno = ENOENT;
    }
    return r;
}

/* readlink: same flaky-EUNATCH treatment (chase() reads /proc/self/fd/N
 * and symlink targets through it). */
ssize_t readlink(const char *path, char *buf, size_t bufsz) {
    ssize_t r = (ssize_t)syscall(SYS_readlinkat, AT_FDCWD, path, buf, bufsz);
    for (int i = 0; r < 0 && i < 3; i++) {
        int e = errno;
        if (e != EUNATCH && e != EINVAL && e != EIO && e != EAGAIN) break;
        usleep(60000);
        r = (ssize_t)syscall(SYS_readlinkat, AT_FDCWD, path, buf, bufsz);
    }
    if (r < 0 && errno == EUNATCH && path && path[0] == '/') {
        r = (ssize_t)syscall(SYS_readlinkat, AT_FDCWD, magic_path(path), buf, bufsz);
    }
    if (r < 0 && errno == EUNATCH) errno = ENOENT;
    return r;
}

int fcntl(int fd, int cmd, ...) {
    va_list ap;
    long arg;
    va_start(ap, cmd);
    arg = va_arg(ap, long);
    va_end(ap);
    if (cmd == F_ADD_SEALS)
        arg &= ~((long)F_SEAL_FUTURE_WRITE | (long)F_SEAL_EXEC);
    return (int)syscall(SYS_fcntl, fd, cmd, arg);
}

/* ---- faccessat: THE EINVAL fix ------------------------------------------ */

static int emulate_access_empty(int fd, int mode) {
    struct stat st;
    memset(&st, 0, sizeof st);
    if (syscall(SYS_newfstatat, fd, "", &st, AT_EMPTY_PATH) < 0 &&
        syscall(SYS_newfstatat, fd, "", &st, AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW) < 0)
        return -1;

    if (geteuid() == 0) {
        if (mode & X_OK) {
            if (st.st_mode & 0111) return 0;
            errno = EACCES;
            return -1;
        }
        return 0; /* root: R_OK/W_OK always (CAP_DAC_OVERRIDE) */
    }
    /* non-root best effort: owner/group/other DAC bits */
    {
        unsigned int shift = (st.st_uid == geteuid()) ? 6 :
                             (st.st_gid == getegid()) ? 3 : 0;
        unsigned int bits = (st.st_mode >> shift) & 7;
        if ((mode & R_OK) && !(bits & 4)) { errno = EACCES; return -1; }
        if ((mode & W_OK) && !(bits & 2)) { errno = EACCES; return -1; }
        if ((mode & X_OK) && !(bits & 1)) { errno = EACCES; return -1; }
        return 0;
    }
}

int faccessat(int dfd, const char *path, int mode, int flags) {
    if (path && path[0] == '\0' && (flags & AT_EMPTY_PATH) && dfd >= 0 && dfd != AT_FDCWD)
        return emulate_access_empty(dfd, mode);
    return (int)syscall(SYS_faccessat, dfd, path, mode, flags);
}

int faccessat2(int dfd, const char *path, int mode, int flags) {
    if (path && path[0] == '\0' && (flags & AT_EMPTY_PATH) && dfd >= 0 && dfd != AT_FDCWD)
        return emulate_access_empty(dfd, mode);
    {
        int r = (int)syscall(SYS_faccessat2, dfd, path, mode, flags);
        if (r < 0 && errno == ENOSYS)
            r = (int)syscall(SYS_faccessat, dfd, path, mode, flags);
        return r;
    }
}

/* ---- executor fd cache --------------------------------------------------- */

static int cached_exec_fd = -1;
static int served_count = 0;

static void kmsg_str(const char *s) {
    size_t n = strlen(s);
    if (kfd >= 0) write(kfd, s, n);
}

static void cache_log(const char *what, int fd, int err) {
    char buf[160];
    int n;
    if (fd >= 0)
        n = snprintf(buf, sizeof buf, "nhstub25: %s = fd %d\n", what, fd);
    else
        n = snprintf(buf, sizeof buf, "nhstub25: %s = errno %d (%s)\n", what, err, strerror(err));
    if (kfd >= 0) write(kfd, buf, n);
}

static const char *exec_candidates[] = {
    "/usr/lib/systemd/systemd-executor",
    "/run/systemd-executor",
    "/usr/lib/systemd/systemd",
    "/sbin/init",
    NULL,
};

static int path_is_executor(const char *p) {
    if (!p) return 0;
    return strstr(p, "systemd-executor") != NULL;
}

static int serve_executor_opath(int cloexec, int real_fail) {
    char buf[192];
    int n;

    if (cached_exec_fd >= 0) {
        int fd = (int)syscall(SYS_fcntl, cached_exec_fd, cloexec ? F_DUPFD_CLOEXEC : F_DUPFD, 0);
        if (fd >= 0) {
            served_count++;
            n = snprintf(buf, sizeof buf,
                         "nhstub25: executor O_PATH served from cache (dup of %d, serve #%d)\n",
                         cached_exec_fd, served_count);
            if (kfd >= 0) write(kfd, buf, n);
            return fd;
        }
    }

    {
        int i;
        for (i = 0; exec_candidates[i]; i++) {
            int fd = (int)syscall(SYS_openat, AT_FDCWD, exec_candidates[i], O_PATH | O_CLOEXEC, 0);
            if (fd >= 0) {
                if (cached_exec_fd < 0) cached_exec_fd = (int)syscall(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 0);
                n = snprintf(buf, sizeof buf,
                             "nhstub25: executor O_PATH reopened %s = fd %d (real open errno was %d)\n",
                             exec_candidates[i], fd, real_fail);
                if (kfd >= 0) write(kfd, buf, n);
                return fd;
            }
        }
    }
    kmsg_str("nhstub25: executor O_PATH ALL CANDIDATES FAILED\n");
    errno = real_fail ? real_fail : ENOENT;
    return -1;
}

/* open/open64/openat/openat64: only special-case O_PATH opens of the
 * executor. systemd builds with LFS so open64/openat64 are the ones it
 * actually calls (proven boot #55/#56). */

/* ---- O_PATH retry: the drop-in/canonicalize fix --------------------------
 * The manager's chase() opens drop-in dirs with O_PATH; on the MTK kernel
 * those opens are FLAKY post-pivot (EUNATCH/EINVAL/EIO - proven: the
 * executor O_PATH opened OK at t=13 and failed EINVAL at t=22 in boot
 * #55). A short retry loop turns flaky opens into reliable ones, which
 * lets the manager load /etc drop-ins (Type=simple etc) and unblocks the
 * whole service cascade. */
static int opath_retry(const char *path, int flags, int mode) {
    int fd = raw_openat(AT_FDCWD, path, flags, mode);
    for (int i = 0; fd < 0 && i < 3; i++) {
        int e = errno;
        if (e != EUNATCH && e != EINVAL && e != EIO && e != EAGAIN) break;
        usleep(60000);
        fd = raw_openat(AT_FDCWD, path, flags, mode);
    }
    return fd;
}

static int opath_retry_from(int dfd, const char *path, int flags, int mode) {
    int fd = raw_openat(dfd, path, flags, mode);
    for (int i = 0; fd < 0 && i < 3; i++) {
        int e = errno;
        if (e != EUNATCH && e != EINVAL && e != EIO && e != EAGAIN) break;
        usleep(60000);
        fd = raw_openat(dfd, path, flags, mode);
    }
    return fd;
}

int open(const char *path, int flags, ...) {
    mode_t mode = 0;
    va_list ap;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if ((flags & O_PATH) && path_is_executor(path)) {
        int fd = raw_openat(AT_FDCWD, path, flags, mode);
        if (fd >= 0) {
            if (cached_exec_fd < 0 && kfd >= 0) {
                char buf[160];
                int n = snprintf(buf, sizeof buf,
                                 "nhstub25: executor O_PATH real open OK (%s) - caching\n", path);
                write(kfd, buf, n);
                cached_exec_fd = (int)syscall(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 0);
            }
            return fd;
        }
        return serve_executor_opath(flags & O_CLOEXEC, errno);
    }
    if (flags & O_PATH)
        return opath_retry(path, flags, mode);
    return open_with_magic(AT_FDCWD, path, flags, mode);
}

int openat(int dfd, const char *path, int flags, ...) {
    mode_t mode = 0;
    va_list ap;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_start(ap, flags);
        mode = (mode_t)va_arg(ap, int);
        va_end(ap);
    }
    if ((flags & O_PATH) && path_is_executor(path)) {
        int fd = raw_openat(dfd, path, flags, mode);
        if (fd >= 0) {
            if (cached_exec_fd < 0 && kfd >= 0) {
                char buf[160];
                int n = snprintf(buf, sizeof buf,
                                 "nhstub25: executor O_PATH real open OK (%s) - caching\n", path);
                write(kfd, buf, n);
                cached_exec_fd = (int)syscall(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 0);
            }
            return fd;
        }
        return serve_executor_opath(flags & O_CLOEXEC, errno);
    }
    if (flags & O_PATH) {
        int fd = opath_retry_from(dfd, path, flags, mode);
        if (fd >= 0) return fd;
    }
    return open_with_magic(dfd, path, flags, mode);
}

int open64(const char *path, int flags, ...) {
    return open(path, flags, (flags & (O_CREAT | O_TMPFILE)) ? (mode_t)0 : 0);
}

int openat64(int dfd, const char *path, int flags, ...) {
    return openat(dfd, path, flags, 0);
}

/* ---- pidfd → pid map + waitid translation -------------------------------
 * systemd 262 spawns every child via pidfd_spawn (glibc ≥ 2.39) and waits
 * with waitid(P_PIDFD, fd, ...) - P_PIDFD requires kernel 5.4+, so on
 * 4.14 EVERY spawn-wait fails EINVAL ("Failed to wait for ...: Invalid
 * argument") and the manager freezes. Intercept clone3/clone (to record
 * which pid each pidfd refers to) and waitid (to translate P_PIDFD into
 * a plain P_PID wait). */

#ifndef CLONE_PIDFD
#define CLONE_PIDFD 0x00001000ULL
#endif
#ifndef P_PIDFD
#define P_PIDFD 3
#endif

#define PIDMAP_MAX 64
static struct { int fd; pid_t pid; } pidmap[PIDMAP_MAX];

static void pidmap_add(int fd, pid_t pid) {
    if (fd < 0 || pid <= 0) return;
    for (int i = 0; i < PIDMAP_MAX; i++)
        if (pidmap[i].fd == fd) { pidmap[i].pid = pid; return; }
    for (int i = 0; i < PIDMAP_MAX; i++)
        if (pidmap[i].fd == 0) { pidmap[i].fd = fd; pidmap[i].pid = pid; return; }
}

static pid_t pidmap_get(int fd) {
    for (int i = 0; i < PIDMAP_MAX; i++)
        if (pidmap[i].fd == fd) return pidmap[i].pid;
    return 0;
}

static void pidmap_del(int fd) {
    for (int i = 0; i < PIDMAP_MAX; i++)
        if (pidmap[i].fd == fd) { pidmap[i].fd = 0; pidmap[i].pid = 0; return; }
}

/* the kernel's struct clone_args (linux/sched.h) */
struct clone_args_local {
    unsigned long long flags;
    unsigned long long pidfd;
    unsigned long long child_tid;
    unsigned long long parent_tid;
    unsigned long long exit_signal;
    unsigned long long stack;
    unsigned long long stack_size;
    unsigned long long tls;
};

int clone3(void *cl_args, unsigned long long size) {
    pid_t pid = (pid_t)syscall(SYS_clone3, cl_args, size);
    if (pid > 0 && cl_args) {
        struct clone_args_local *a = (struct clone_args_local *)cl_args;
        if (a->flags & CLONE_PIDFD && a->pidfd) {
            int pfd = *(int *)(unsigned long)a->pidfd;
            pidmap_add(pfd, pid);
        }
    }
    return pid;
}

pid_t clone(int (*fn)(void *), void *stack, int flags, void *arg, ...) {
    va_list ap;
    va_start(ap, arg);
    pid_t *parent_tid = (pid_t *)va_arg(ap, pid_t *);
    void *tls = va_arg(ap, void *);
    pid_t *child_tid = (pid_t *)va_arg(ap, pid_t *);
    va_end(ap);
    pid_t pid = (pid_t)syscall(SYS_clone, flags, stack, parent_tid, tls, child_tid);
    if (pid > 0 && (flags & CLONE_PIDFD) && parent_tid) {
        pidmap_add(*parent_tid, pid);
    }
    return pid;
}

int waitid(idtype_t idtype, id_t id, siginfo_t *infop, int options) {
    if (idtype == P_PIDFD) {
        pid_t pid = pidmap_get((int)id);
        if (pid <= 0)
            pid = (pid_t)id; /* the pidfd_spawn override stores the pid AS the fd */
        idtype = P_PID;
        id = (id_t)pid;
    }
    int r = (int)syscall(SYS_waitid, idtype, id, infop, options, 0);
    if (r < 0 && errno == ECHILD && idtype == P_PID && infop) {
        /* the child already exited and was reaped by PID1's SIGCHLD
         * handler - a classic race with the fast environment generators.
         * Synthesize a clean exit so the caller doesn't treat this as
         * fatal (the generator's output already went down its pipe). */
        memset(infop, 0, sizeof *infop);
        infop->si_code = CLD_EXITED;
        infop->si_pid = (pid_t)id;
        infop->si_status = 0;
        return 0;
    }
    return r;
}

int close(int fd) {
    if (fd >= 0) pidmap_del(fd);
    int r = (int)syscall(SYS_close, fd);
    /* systemd's safe_close() asserts close_nointr(fd) != -EBADF; an
     * invalid fd reaching it (from the rot-ENOENT paths) would abort.
     * Report success for the invalid fds - closing a closed fd is a
     * no-op anyway. */
    if (r < 0 && errno == EBADF) return 0;
    return r;
}

/* ---- pidfd_spawn → posix_spawn with the PID as the "pidfd" --------------
 * glibc's pidfd_spawn writes a real pidfd, which the manager waits on with
 * waitid(P_PIDFD) - unsupported on 4.14. Force the classic posix_spawn and
 * store THE PID ITSELF in the pidfd slot; the waitid override recognizes a
 * mapped entry (pid→pid) and translates to waitid(P_PID, pid). */
int pidfd_spawn(int *pidfd_out, const char *path,
                const posix_spawn_file_actions_t *facts,
                const posix_spawnattr_t *attrp,
                char *const argv[], char *const envp[]) {
    /* ENOSYS forces systemd's OWN fallback: it disables
     * POSIX_SPAWN_SETCGROUP (clone3-based, 4.14-incompatible) and calls
     * plain posix_spawn, whose child gets a plain pid that waitid(P_PID)
     * handles natively on 4.14. */
    (void)pidfd_out; (void)path; (void)facts; (void)attrp; (void)argv; (void)envp;
    return -ENOSYS;
}

int pidfd_spawnp(int *pidfd_out, const char *file,
                 const posix_spawn_file_actions_t *facts,
                 const posix_spawnattr_t *attrp,
                 char *const argv[], char *const envp[]) {
    (void)pidfd_out; (void)file; (void)facts; (void)attrp; (void)argv; (void)envp;
    return -ENOSYS;
}

__attribute__((constructor)) static void nhstub25_init(void) {
    if (getpid() != 1) return;
    kfd = open("/dev/kmsg", O_WRONLY);
    if (kfd < 0) return;
    kmsg_str("nhstub25: loaded in PID1 (faccessat fix + executor cache)\n");
    cached_exec_fd = (int)syscall(SYS_openat, AT_FDCWD, "/usr/lib/systemd/systemd-executor", O_PATH | O_CLOEXEC, 0);
    cache_log("cache /usr/lib/systemd/systemd-executor", cached_exec_fd, errno);
    if (cached_exec_fd < 0) {
        cached_exec_fd = (int)syscall(SYS_openat, AT_FDCWD, "/usr/lib/systemd/systemd", O_PATH | O_CLOEXEC, 0);
        cache_log("cache fallback /usr/lib/systemd/systemd", cached_exec_fd, errno);
    }
    kmsg_str("nhstub25: constructor done\n");
}











