#include <exyde/initrd.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* cpio newc layout (SVR4 without CRC):
 *
 *   struct header {
 *     char c_magic[6];     "070701"
 *     char c_ino[8];       hex ASCII
 *     char c_mode[8];
 *     char c_uid[8];
 *     char c_gid[8];
 *     char c_nlink[8];
 *     char c_mtime[8];
 *     char c_filesize[8];  <-- offset 54
 *     char c_devmajor[8];
 *     char c_devminor[8];
 *     char c_rdevmajor[8];
 *     char c_rdevminor[8];
 *     char c_namesize[8];  <-- offset 94
 *     char c_chksum[8];
 *   };                     total 110 bytes
 *
 *   followed by the file name (c_namesize bytes, NUL-terminated),
 *   padded up to a 4-byte boundary, then the file body
 *   (c_filesize bytes), also padded up to 4 bytes.
 *
 *   The archive is terminated by an entry whose name is
 *   "TRAILER!!!". */

#define CPIO_HDR_SIZE   110u
#define CPIO_OFF_FSIZE   54u
#define CPIO_OFF_NSIZE   94u
#define CPIO_MAGIC      "070701"

static const uint8_t *g_base;
static size_t         g_size;
static int            g_ready;

static int hexdigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex8(const char *p, uint32_t *out) {
    uint32_t v = 0;
    for (int i = 0; i < 8; ++i) {
        int d = hexdigit(p[i]);
        if (d < 0) return -1;
        v = (v << 4) | (uint32_t)d;
    }
    *out = v;
    return 0;
}

static int parse_env_hex_u64(const char *s, uint64_t *out) {
    if (!s || s[0] != '0' || (s[1] != 'x' && s[1] != 'X')) return -1;
    uint64_t v = 0;
    for (int i = 2; s[i]; ++i) {
        int d = hexdigit(s[i]);
        if (d < 0) return -1;
        v = (v << 4) | (uint64_t)d;
    }
    *out = v;
    return 0;
}

static size_t align4(size_t n) { return (n + 3u) & ~(size_t)3u; }

int initrd_init(void) {
    const char *b = getenv("EXYDE_INITRD_BASE");
    const char *s = getenv("EXYDE_INITRD_SIZE");
    if (!b || !s) { errno = ENOENT; return -1; }

    uint64_t base_u = 0, size_u = 0;
    if (parse_env_hex_u64(b, &base_u) != 0 ||
        parse_env_hex_u64(s, &size_u) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (size_u < 6u) { errno = EINVAL; return -1; }

    const uint8_t *base = (const uint8_t *)(uintptr_t)base_u;
    if (memcmp(base, CPIO_MAGIC, 6) != 0) { errno = EINVAL; return -1; }

    g_base  = base;
    g_size  = (size_t)size_u;
    g_ready = 1;
    return 0;
}

int initrd_find(const char *path, const void **out_data, size_t *out_size) {
    if (!g_ready) { errno = ENOENT; return -1; }
    if (!path || !path[0]) { errno = EINVAL; return -1; }

    size_t off = 0;
    while (off + CPIO_HDR_SIZE <= g_size) {
        const uint8_t *h = g_base + off;
        if (memcmp(h, CPIO_MAGIC, 6) != 0) { errno = EINVAL; return -1; }

        uint32_t namesize = 0, filesize = 0;
        if (parse_hex8((const char *)h + CPIO_OFF_NSIZE, &namesize) != 0 ||
            parse_hex8((const char *)h + CPIO_OFF_FSIZE, &filesize) != 0) {
            errno = EINVAL;
            return -1;
        }

        size_t name_off = off + CPIO_HDR_SIZE;
        if (name_off + namesize > g_size) { errno = EINVAL; return -1; }
        const char *name = (const char *)(g_base + name_off);

        if (namesize == 11 && memcmp(name, "TRAILER!!!", 10) == 0) {
            errno = ENOENT;
            return -1;
        }

        /* Both the header+name block and the file body are padded
         * so that the *cumulative* offset is a multiple of 4. */
        size_t data_off = align4(name_off + namesize);
        if (data_off + filesize > g_size) { errno = EINVAL; return -1; }

        if (namesize > 1 && strcmp(name, path) == 0) {
            *out_data = (const void *)(g_base + data_off);
            *out_size = (size_t)filesize;
            return 0;
        }

        off = align4(data_off + filesize);
    }

    errno = ENOENT;
    return -1;
}
