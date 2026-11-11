/* nhstub13.c - v11: v10's behavior PLUS faccessat/faccessat2 interception.
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
#include <sys/stat.h>
#include <sys/syscall.h>
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
        if (kfd >= 0) write(kfd, "nhstub13: statx faked for all paths\n", 36);
    }
}

static unsigned long long fake_ino = 100;

int statx(int dfd, const char *path, int flags, unsigned int mask, struct statx *sx) {
    struct statx_local *lx = (struct statx_local *)sx;
    static int warned = 0;
    if (!warned) {
        warned = 1;
        if (kfd < 0) kfd = open("/dev/kmsg", O_WRONLY);
        if (kfd >= 0) write(kfd, "nhstub13: statx faked for all paths\n", 36);
    }
    memset(lx, 0, sizeof *lx);
    lx->stx_mask = STATX_BASIC_STATS | STATX_TYPE;
    lx->stx_attributes_mask = STATX_ATTR_MOUNT_ROOT;
    lx->stx_attributes = STATX_ATTR_MOUNT_ROOT;
    lx->stx_mode = S_IFDIR | 0755;
    lx->stx_nlink = 2;
    lx->stx_ino = fake_ino++;
    lx->stx_uid = 0;
    lx->stx_gid = 0;
    lx->stx_size = 4096;
    lx->stx_blocks = 8;
    lx->stx_blksize = 4096;
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
        errno = ENOENT; /* a definitive rot-EUNATCH means "not reachable" */
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
        n = snprintf(buf, sizeof buf, "nhstub13: %s = fd %d\n", what, fd);
    else
        n = snprintf(buf, sizeof buf, "nhstub13: %s = errno %d (%s)\n", what, err, strerror(err));
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
                         "nhstub13: executor O_PATH served from cache (dup of %d, serve #%d)\n",
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
                             "nhstub13: executor O_PATH reopened %s = fd %d (real open errno was %d)\n",
                             exec_candidates[i], fd, real_fail);
                if (kfd >= 0) write(kfd, buf, n);
                return fd;
            }
        }
    }
    kmsg_str("nhstub13: executor O_PATH ALL CANDIDATES FAILED\n");
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
static int raw_openat(int dfd, const char *path, int flags, int mode) {
    return (int)syscall(SYS_openat, dfd, path, flags, mode);
}

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
                                 "nhstub13: executor O_PATH real open OK (%s) - caching\n", path);
                write(kfd, buf, n);
                cached_exec_fd = (int)syscall(SYS_fcntl, fd, F_DUPFD_CLOEXEC, 0);
            }
            return fd;
        }
        return serve_executor_opath(flags & O_CLOEXEC, errno);
    }
    if (flags & O_PATH)
        return opath_retry(path, flags, mode);
    return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
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
                                 "nhstub13: executor O_PATH real open OK (%s) - caching\n", path);
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
    return (int)syscall(SYS_openat, dfd, path, flags, mode);
}

int open64(const char *path, int flags, ...) {
    return open(path, flags, (flags & (O_CREAT | O_TMPFILE)) ? (mode_t)0 : 0);
}

int openat64(int dfd, const char *path, int flags, ...) {
    return openat(dfd, path, flags, 0);
}

__attribute__((constructor)) static void nhstub13_init(void) {
    if (getpid() != 1) return;
    kfd = open("/dev/kmsg", O_WRONLY);
    if (kfd < 0) return;
    kmsg_str("nhstub13: loaded in PID1 (faccessat fix + executor cache)\n");
    cached_exec_fd = (int)syscall(SYS_openat, AT_FDCWD, "/usr/lib/systemd/systemd-executor", O_PATH | O_CLOEXEC, 0);
    cache_log("cache /usr/lib/systemd/systemd-executor", cached_exec_fd, errno);
    if (cached_exec_fd < 0) {
        cached_exec_fd = (int)syscall(SYS_openat, AT_FDCWD, "/usr/lib/systemd/systemd", O_PATH | O_CLOEXEC, 0);
        cache_log("cache fallback /usr/lib/systemd/systemd", cached_exec_fd, errno);
    }
    kmsg_str("nhstub13: constructor done\n");
}


