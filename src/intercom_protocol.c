#define _XOPEN_SOURCE 700
#include "intercom_protocol.h"
#include "adapter/live_b64.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

uint64_t le_ic_now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}
int le_ic_optin_path(const char *config, char *out, size_t size)
{
    char resolved[4096];
    return config && config[0] && realpath(config, resolved) &&
        snprintf(out, size, "%s.intercom", resolved) < (int)size;
}
int le_ic_lease_active(const char *path, uint64_t now)
{
    struct stat st; char b[64], tail; unsigned long long deadline;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC); ssize_t n;
    if (fd < 0) return 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || (st.st_mode & 077) ||
        st.st_uid != geteuid()) { close(fd); return 0; }
    n = read(fd, b, sizeof b - 1); close(fd);
    if (n <= 0) return 0;
    b[n] = 0;
    if (sscanf(b, "%llu %c", &deadline, &tail) != 1) return 0;
    return deadline > now && deadline - now <= 2100u;
}
int le_ic_lease_write(const char *path, uint64_t now)
{
    char b[64], temporary[512]; int n = snprintf(b, sizeof b, "%llu\n",
                               (unsigned long long)(now + 2000u));
    int fd; struct stat st; int rc;
    if (snprintf(temporary, sizeof temporary, "%s.%ld.tmp", path, (long)getpid()) >= (int)sizeof temporary) return -1;
    fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return -1;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        fchmod(fd, 0600)) { close(fd); return -1; }
    rc = write(fd, b, (size_t)n) == n ? 0 : -1;
    if (close(fd)) rc = -1;
    if (!rc && rename(temporary, path)) rc = -1;
    if (rc) unlink(temporary);
    return rc;
}
int le_ic_ws_decode(const unsigned char *p, size_t n, unsigned char *out,
                    size_t *length, unsigned *opcode, size_t *consumed)
{
    size_t z, h = 2, i; unsigned op;
    if (n < 2) return 0;
    op = p[0] & 15u;
    if ((p[0] & 0xf0u) != 0x80u || !(p[1] & 128u) ||
        (op != 1 && op != 2 && op != 8 && op != 9 && op != 10)) return -1;
    z = p[1] & 127u;
    if (z == 127) return -1;
    if (z == 126) {
        if (n < 4) return 0;
        z = (size_t)p[2] * 256u + p[3]; h = 4;
        if (z < 126) return -1;
    }
    if (z > LE_IC_WS_MAX || (op >= 8 && (z > 125 || (op == 8 && z == 1)))) return -1;
    if (n < h + 4 + z) return 0;
    for (i = 0; i < z; i++) out[i] = p[h + 4 + i] ^ p[h + i % 4];
    out[z] = 0; *length = z; *opcode = op; *consumed = h + 4 + z;
    return 1;
}
size_t le_ic_ws_encode(unsigned char *p, size_t cap, unsigned op,
                       const void *body, size_t n)
{
    size_t h = n < 126 ? 2 : 4;
    if (n > LE_IC_WS_MAX || cap < h + n) return 0;
    p[0] = (unsigned char)(128u | op);
    if (h == 2) p[1] = (unsigned char)n;
    else { p[1] = 126; p[2] = (unsigned char)(n >> 8); p[3] = (unsigned char)n; }
    if (n) memcpy(p + h, body, n);
    return h + n;
}
