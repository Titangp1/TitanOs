/* TitanOs - petites aides au-dessus des appels système */
#include "sys.h"

char **g_envp;

void sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000 };
    sys_nanosleep(&ts);
}

int read_file(const char *path, char *buf, int n) {
    int fd = sys_open(path, O_RDONLY | O_CLOEXEC, 0);
    if (fd < 0) { if (n) buf[0] = 0; return -1; }
    int total = 0;
    while (total < n - 1) {
        long r = sys_read(fd, buf + total, (size_t)(n - 1 - total));
        if (r <= 0) break;
        total += (int)r;
    }
    buf[total] = 0;
    sys_close(fd);
    return total;
}

int write_file(const char *path, const char *s) {
    int fd = sys_open(path, O_WRONLY | O_CLOEXEC, 0);
    if (fd < 0) return -1;
    long r = sys_write(fd, s, strlen(s));
    sys_close(fd);
    return r < 0 ? -1 : 0;
}

void mkdirs(const char *path) {
    char p[512];
    strlcpy(p, path, sizeof p);
    for (char *s = p + 1; *s; s++)
        if (*s == '/') { *s = 0; sys_mkdir(p, 0755); *s = '/'; }
    sys_mkdir(p, 0755);
}

bool path_exists(const char *path) { struct stat st; return sys_stat(path, &st) == 0; }
bool is_dir(const char *path) { struct stat st; return sys_stat(path, &st) == 0 && (st.st_mode & 0170000) == 0040000; }

struct dirent64_ { uint64_t ino; int64_t off; uint16_t reclen; uint8_t type; char name[]; };

int for_each_dir(const char *path, void (*cb)(const char *, int, void *), void *ctx) {
    int fd = sys_open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC, 0);
    if (fd < 0) return -1;
    char buf[8192];
    int count = 0;
    for (;;) {
        long n = sys_getdents64(fd, buf, sizeof buf);
        if (n <= 0) break;
        for (long off = 0; off < n;) {
            struct dirent64_ *d = (struct dirent64_ *)(buf + off);
            off += d->reclen;
            if (!strcmp(d->name, ".") || !strcmp(d->name, "..")) continue;
            cb(d->name, d->type, ctx);
            count++;
        }
    }
    sys_close(fd);
    return count;
}

void klog(const char *fmt, ...) {
    char buf[300];
    va_list ap;
    va_start(ap, fmt);
    int n = snprintf(buf, 16, "<6>titanos: ");
    n += vsnprintf(buf + n, sizeof buf - (size_t)n - 1, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof buf - 2) n = (int)sizeof buf - 2;
    buf[n++] = '\n';
    int fd = sys_open("/dev/kmsg", O_WRONLY | O_CLOEXEC, 0);
    if (fd >= 0) { sys_write(fd, buf, (size_t)n); sys_close(fd); }
}

char *trim(char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n') s++;
    int l = (int)strlen(s);
    while (l > 0 && (s[l - 1] == ' ' || s[l - 1] == '\n' || s[l - 1] == '\t' || s[l - 1] == '\r')) s[--l] = 0;
    return s;
}
