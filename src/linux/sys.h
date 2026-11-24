/* TitanOs - appels système Linux (x86_64), sans libc
 *
 * Les numéros d'appels et les structures viennent des en-têtes officiels du noyau
 * (paquet linux-headers, extrait dans build/hdr par build.py). */
#pragma once
#include "../core/base.h"
#include <asm/unistd.h>
#include <asm/stat.h>
#include <asm/fcntl.h>
#include <asm/mman.h>
#include <linux/mman.h>
#include <asm/poll.h>
#include <asm/signal.h>
#include <asm/errno.h>
#include <asm/ioctls.h>
#include <linux/types.h>
#include <linux/reboot.h>
#include <linux/time.h>

static inline long sc6(long n, long a, long b, long c, long d, long e, long f) {
    long r;
    register long r10 __asm__("r10") = d;
    register long r8 __asm__("r8") = e;
    register long r9 __asm__("r9") = f;
    __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return r;
}
static inline long sc0(long n) { return sc6(n, 0, 0, 0, 0, 0, 0); }
static inline long sc1(long n, long a) { return sc6(n, a, 0, 0, 0, 0, 0); }
static inline long sc2(long n, long a, long b) { return sc6(n, a, b, 0, 0, 0, 0); }
static inline long sc3(long n, long a, long b, long c) { return sc6(n, a, b, c, 0, 0, 0); }
static inline long sc4(long n, long a, long b, long c, long d) { return sc6(n, a, b, c, d, 0, 0); }
static inline long sc5(long n, long a, long b, long c, long d, long e) { return sc6(n, a, b, c, d, e, 0); }

#ifndef AT_FDCWD
#define AT_FDCWD -100
#endif
#define AT_REMOVEDIR_ 0x200
#define AT_SYMLINK_NOFOLLOW_ 0x100
#define WNOHANG_ 1

static inline long sys_read(int fd, void *b, size_t n) { return sc3(__NR_read, fd, (long)b, (long)n); }
static inline long sys_write(int fd, const void *b, size_t n) { return sc3(__NR_write, fd, (long)b, (long)n); }
static inline int sys_open(const char *p, int fl, int mode) { return (int)sc4(__NR_openat, AT_FDCWD, (long)p, fl, mode); }
static inline int sys_close(int fd) { return (int)sc1(__NR_close, fd); }
static inline long sys_ioctl(int fd, unsigned long req, void *arg) { return sc3(__NR_ioctl, fd, (long)req, (long)arg); }
static inline long sys_lseek(int fd, long off, int wh) { return sc3(__NR_lseek, fd, off, wh); }
static inline void *sys_mmap(void *a, size_t n, int prot, int fl, int fd, long off) {
    return (void *)sc6(__NR_mmap, (long)a, (long)n, prot, fl, fd, off);
}
static inline int sys_munmap(void *a, size_t n) { return (int)sc2(__NR_munmap, (long)a, (long)n); }
static inline int sys_poll(struct pollfd *p, int n, int ms) { return (int)sc3(__NR_poll, (long)p, n, ms); }
static inline long sys_getdents64(int fd, void *b, size_t n) { return sc3(__NR_getdents64, fd, (long)b, (long)n); }
static inline int sys_mkdir(const char *p, int mode) { return (int)sc3(__NR_mkdirat, AT_FDCWD, (long)p, mode); }
static inline int sys_unlink(const char *p) { return (int)sc3(__NR_unlinkat, AT_FDCWD, (long)p, 0); }
static inline int sys_rmdir(const char *p) { return (int)sc3(__NR_unlinkat, AT_FDCWD, (long)p, AT_REMOVEDIR_); }
static inline int sys_rename(const char *a, const char *b) { return (int)sc4(__NR_renameat, AT_FDCWD, (long)a, AT_FDCWD, (long)b); }
static inline int sys_stat(const char *p, struct stat *st) { return (int)sc4(__NR_newfstatat, AT_FDCWD, (long)p, (long)st, 0); }
static inline int sys_symlink(const char *t, const char *l) { return (int)sc3(__NR_symlinkat, (long)t, AT_FDCWD, (long)l); }
static inline long sys_readlink(const char *p, char *b, size_t n) { return sc4(__NR_readlinkat, AT_FDCWD, (long)p, (long)b, (long)n); }
static inline int sys_access(const char *p, int m) { return (int)sc3(__NR_faccessat, AT_FDCWD, (long)p, m); }
static inline int sys_chdir(const char *p) { return (int)sc1(__NR_chdir, (long)p); }
static inline int sys_mount(const char *s, const char *t, const char *fs, unsigned long fl, const void *data) {
    return (int)sc5(__NR_mount, (long)s, (long)t, (long)fs, (long)fl, (long)data);
}
static inline int sys_umount2(const char *t, int fl) { return (int)sc2(__NR_umount2, (long)t, fl); }
static inline int sys_fork(void) { return (int)sc0(__NR_fork); }
static inline int sys_execve(const char *p, char *const *argv, char *const *envp) { return (int)sc3(__NR_execve, (long)p, (long)argv, (long)envp); }
static inline int sys_wait4(int pid, int *st, int opt) { return (int)sc4(__NR_wait4, pid, (long)st, opt, 0); }
static inline int sys_kill(int pid, int sig) { return (int)sc2(__NR_kill, pid, sig); }
static inline __attribute__((noreturn)) void sys_exit(int code) { sc1(__NR_exit_group, code); __builtin_unreachable(); }
static inline int sys_getpid(void) { return (int)sc0(__NR_getpid); }
static inline int sys_setsid(void) { return (int)sc0(__NR_setsid); }
static inline int sys_dup2(int a, int b) { return (int)sc3(__NR_dup3, a, b, 0); }
static inline int sys_pipe2(int *fds, int fl) { return (int)sc2(__NR_pipe2, (long)fds, fl); }
static inline int sys_finit_module(int fd, const char *params, int fl) { return (int)sc3(__NR_finit_module, fd, (long)params, fl); }
static inline int sys_socket(int d, int t, int p) { return (int)sc3(__NR_socket, d, t, p); }
static inline int sys_bind(int fd, const void *a, int len) { return (int)sc3(__NR_bind, fd, (long)a, len); }
static inline long sys_recv(int fd, void *b, size_t n, int fl) { return sc6(__NR_recvfrom, fd, (long)b, (long)n, fl, 0, 0); }
static inline int sys_reboot(unsigned cmd) { return (int)sc4(__NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2, cmd, 0); }
static inline void sys_sync(void) { sc0(__NR_sync); }
static inline int sys_sethostname(const char *n, size_t l) { return (int)sc2(__NR_sethostname, (long)n, (long)l); }
static inline int sys_clock_gettime(int clk, struct timespec *ts) { return (int)sc2(__NR_clock_gettime, clk, (long)ts); }
static inline int sys_nanosleep(const struct timespec *ts) { return (int)sc2(__NR_nanosleep, (long)ts, 0); }
static inline int sys_rt_sigaction(int sig, const void *act, void *old) { return (int)sc4(__NR_rt_sigaction, sig, (long)act, (long)old, 8); }
static inline int sys_fchmod(int fd, int mode) { return (int)sc2(__NR_fchmod, fd, mode); }

/* ---- sys.c : petites aides ---- */
void   sleep_ms(int ms);
int    read_file(const char *path, char *buf, int n);          /* lit un petit fichier (texte), terminé par 0 */
int    write_file(const char *path, const char *s);            /* écrit une chaîne */
void   mkdirs(const char *path);                               /* crée le dossier et ses parents */
bool   path_exists(const char *path);
bool   is_dir(const char *path);
/* parcours d'un dossier : appelle cb(nom, type d_type, ctx) pour chaque entrée sauf . et .. */
int    for_each_dir(const char *path, void (*cb)(const char *name, int type, void *ctx), void *ctx);
void   klog(const char *fmt, ...);                              /* message dans le journal du noyau */
char  *trim(char *s);
extern char **g_envp;
