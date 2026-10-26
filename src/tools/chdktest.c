/*
 * chdktest.c - chdk/soup_dev.c on the desktop, against a card that behaves
 * as badly as the camera's is allowed to.
 *
 * soup_dev.c is the PowerShot develop, and the only thing standing between
 * it and the camera is the handful of sd_* calls. They are answered here:
 *
 *   - every read returns a random fraction of what was asked for, the
 *     fault that silently corrupted the 5D's first frames;
 *   - the heap is capped (default 1 MB, about what CHDK leaves free on a
 *     DIGIC 4 PowerShot), so an allocation the camera would refuse is
 *     refused here too.
 *
 * The frame is then developed a second time with nothing streamed -- the
 * whole file in RAM, the decoder reading the camera's own header -- and
 * the image data of the two JPEGs must be byte-identical. That is what
 * proves the rebuilt header and the offset mapping in soup_dev.c hand the
 * decoder exactly the bytes it would have read from the original file.
 *
 * With no arguments it checks the module's vsnprintf instead (chdk/
 * vsnprintf.c, built as se_vsnprintf) against the system's, for every
 * format the engine and the module use.
 *
 * usage: chdktest
 *        chdktest <camera.JPG> <stockdir> <stock> <out.jpg> [scale] [heap KB]
 */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "soup_dev.h"
#include "look.h"
#include "jpeg_dec.h"
#include "jpeg_enc.h"

/* ---- the card ---------------------------------------------------------- */
#define MAX_H 8
static FILE *files[MAX_H];
static long  reads, short_reads;

static int slot(FILE *f)
{
    int i;
    if (!f) return -1;
    for (i = 0; i < MAX_H; i++) if (!files[i]) { files[i] = f; return i; }
    fclose(f);
    return -1;
}

int  sd_open(const char *p)   { return slot(fopen(p, "rb")); }
int  sd_create(const char *p) { return slot(fopen(p, "wb")); }
void sd_close(int h)          { fclose(files[h]); files[h] = 0; }
void sd_remove(const char *p) { remove(p); }
int  sd_rename(const char *a, const char *b) { return rename(a, b); }
int  sd_seek(int h, int pos)  { return fseek(files[h], pos, SEEK_SET) ? -1 : pos; }

int sd_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (int)st.st_size : -1;
}

int sd_read(int h, void *buf, int n)
{
    int want = n;
    reads++;
    if (n > 1 && (rand() & 1)) { want = 1 + rand() % n; short_reads++; }
    return (int)fread(buf, 1, (size_t)want, files[h]);
}

int sd_write(int h, const void *buf, int n)
{
    return (int)fwrite(buf, 1, (size_t)n, files[h]);
}

/* ---- the heap ---------------------------------------------------------- */
static size_t heap_cap, heap_used, heap_peak;

void *sd_alloc(size_t n)
{
    size_t *p;
    if (heap_used + n > heap_cap) {
        fprintf(stderr, "heap: refused %zu KB with %zu KB in use (cap %zu KB)\n",
                n >> 10, heap_used >> 10, heap_cap >> 10);
        return NULL;
    }
    if (!(p = malloc(n + sizeof *p))) return NULL;
    *p = n;
    heap_used += n;
    if (heap_used > heap_peak) heap_peak = heap_used;
    return p + 1;
}

void sd_free(void *q)
{
    size_t *p = (size_t *)q - 1;
    heap_used -= *p;
    free(p);
}

void sd_progress(int pct) { if (pct % 25 == 0) fprintf(stderr, " %d%%", pct); }

static void log_line(void *ctx, const char *msg) { fprintf(stderr, "log: %s\n", msg); }

/* ---- the reference: the whole file in RAM, no streaming ---------------- */
typedef struct { se_jpeg *enc; se_look_run *run; FILE *f; } ref_ctx;

static int ref_sink(void *ctx, const uint8_t *d, size_t n)
{
    return fwrite(d, 1, n, ((ref_ctx *)ctx)->f) == n ? 0 : -1;
}
static int ref_emit(void *ctx, const uint8_t *row, int w)
{
    return se_jpeg_write_rows(((ref_ctx *)ctx)->enc, row, 1) == SE_OK ? 0 : -1;
}
static int ref_rows(void *ctx, const uint8_t *rgb, int nrows, int w)
{
    return se_look_rows(((ref_ctx *)ctx)->run, rgb, nrows, ref_emit, ctx) == SE_OK ? 0 : -1;
}

static int reference(const char *in, const char *out, const se_stock *st, int scale)
{
    static uint8_t mem[64 << 20];
    se_arena     a;
    se_jdec_info ji;
    se_jpeg_exif ex;
    se_look      lk;
    ref_ctx      R;
    long         n;
    uint8_t     *buf, *strip;
    int          ow, oh;
    FILE        *f = fopen(in, "rb");

    if (!f) return -1;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) return -1;
    fclose(f);

    se_arena_init(&a, mem, sizeof mem);
    if (se_jpeg_dec_info(buf, (size_t)n, &ji) != SE_OK) return -1;
    se_jpeg_scaled_size(ji.width, ji.height, scale, &ow, &oh);
    se_look_from_stock(st, scale, &lk);
    se_look_fit(&lk, ow, SE_LOOK_BUDGET);

    R.f   = fopen(out, "wb");
    R.enc = SE_NEW(&a, se_jpeg, 1);
    strip = SE_NEW(&a, uint8_t, (size_t)8 * ow * 3);
    if (se_look_begin(&R.run, &lk, ow, &a) != SE_OK) return -1;

    memset(&ex, 0, sizeof ex);
    if (se_jpeg_begin(R.enc, ow, oh, 90, strip, &ex, ref_sink, &R) != SE_OK) return -1;
    if (se_jpeg_decode_scaled(buf, (size_t)n, scale, &a, ref_rows, &R, &ji) != SE_OK) return -1;
    if (se_look_end(R.run, ref_emit, &R) != SE_OK) return -1;
    if (se_jpeg_end(R.enc) != SE_OK) return -1;
    fclose(R.f);
    free(buf);
    return 0;
}

static long slurp(const char *p, uint8_t **out)
{
    FILE *f = fopen(p, "rb");
    long  n;
    if (!f) return -1;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    *out = malloc((size_t)n);
    n = (long)fread(*out, 1, (size_t)n, f);
    fclose(f);
    return n;
}

/* Where the image starts: the first DQT. The EXIF ahead of it differs by
 * design -- the reference is given none -- so only what follows is compared. */
static long image_start(const uint8_t *j, long n)
{
    long i;
    for (i = 2; i + 1 < n; i++) if (j[i] == 0xFF && j[i + 1] == 0xDB) return i;
    return n;
}

/* The first printable run in the EXIF that starts with `prefix`. */
static void exif_str(const uint8_t *j, long n, const char *prefix, char *dst, size_t cap)
{
    long i, lim = image_start(j, n);
    size_t k = strlen(prefix);
    dst[0] = '\0';
    for (i = 0; i + (long)k < lim; i++)
        if (memcmp(j + i, prefix, k) == 0) { snprintf(dst, cap, "%s", (const char *)j + i); return; }
}

/* ---- vsnprintf --------------------------------------------------------- */
int se_vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

static int bad, cases;

static void check(const char *fmt, ...)
{
    char    mine[128], ref[128];
    va_list a;

    va_start(a, fmt); se_vsnprintf(mine, sizeof mine, fmt, a); va_end(a);
    va_start(a, fmt); vsnprintf(ref, sizeof ref, fmt, a);      va_end(a);
    cases++;
    if (strcmp(mine, ref)) { bad++; printf("  MISMATCH '%s': mine=%s ref=%s\n", fmt, mine, ref); }
}

static int vsnprintf_check(void)
{
    check("%d", 0); check("%d", -1); check("%d", 2147483647); check("%d", (int)(-2147483647 - 1));
    check("%u", 4294967295u); check("%x", 0xdeadbeef); check("%X", 0xdeadbeef);
    check("%5d|", 42); check("%-5d|", 42); check("%05d|", 42); check("%05d|", -42); check("%-05d|", 42);
    check("%s|", "hi"); check("%8s|", "hi"); check("%-8s|", "hi"); check("%c|", 'Z');
    check("%d%%", 99); check("%18s%s", "Film stock", "Velvia"); check("%-18s%s", "Film stock", "Velvia");
    check("%s -> %s, %s, %d s", "A/DCIM/100CANON/IMG_0001.JPG", "A/FILM/100CANON/IMG_0001.JPG", "VELVIA", 61);
    check("%s: %s (largest free block %d KB)", "IMG_0001.JPG", "out of arena memory", 812);
    check("develop: %u KB would not fit", 1234u); check("%08x", 0x1234);
    printf("vsnprintf: %d cases, %d mismatches\n", cases, bad);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    static se_stock stocks[SD_MAX_STOCKS];
    char     tmp[1024], ref[1024], model[32], when[24];
    long     sa, sb;
    sd_opts  o = { 2, 90 };
    uint8_t *a, *b;
    long     na, nb;
    int      n, i, pick = -1;
    clock_t  t0;
    se_err   rc;

    if (argc == 1) return vsnprintf_check();
    if (argc < 5) {
        fprintf(stderr, "usage: chdktest\n"
                        "       chdktest <camera.JPG> <stockdir> <stock> <out.jpg> [scale] [heap KB]\n");
        return 2;
    }
    if (argc > 5) o.scale = atoi(argv[5]);
    heap_cap = (size_t)(argc > 6 ? atoi(argv[6]) : 1024) << 10;
    srand(1);
    se_set_log(log_line, NULL);

    n = sd_load_stocks(argv[2], stocks, SD_MAX_STOCKS);
    for (i = 0; i < n; i++) if (strcasecmp(stocks[i].name, argv[3]) == 0) pick = i;
    if (pick < 0) { fprintf(stderr, "no stock '%s' among %d\n", argv[3], n); return 1; }
    printf("stocks: %d loaded, heap after: %zu KB (peak %zu KB)\n", n, heap_used >> 10, heap_peak >> 10);
    heap_peak = 0;

    snprintf(tmp, sizeof tmp, "%s.tmp", argv[4]);
    t0 = clock();
    fprintf(stderr, "develop %s with %s at 1/%d:", argv[1], stocks[pick].name, o.scale);
    rc = sd_develop(argv[1], tmp, argv[4], &stocks[pick], &o);
    fprintf(stderr, "\n");
    printf("develop: rc=%d (%s), %.2f s, heap peak %zu KB, arena peak %zu KB, "
           "%ld reads of which %ld short\n",
           rc, se_strerror(rc), (double)(clock() - t0) / CLOCKS_PER_SEC,
           heap_peak >> 10, sd_last_peak >> 10, reads, short_reads);
    if (rc != SE_OK) return 1;
    if (heap_used) { printf("FAIL: %zu bytes still allocated\n", heap_used); return 1; }

    na = slurp(argv[4], &a);
    exif_str(a, na, "Canon ", model, sizeof model);
    exif_str(a, na, "20", when, sizeof when);
    printf("exif: model '%s', datetime '%s'\n", model, when);

    snprintf(ref, sizeof ref, "%s.ref.jpg", argv[4]);
    if (reference(argv[1], ref, &stocks[pick], o.scale) != 0) {
        printf("FAIL: the reference develop did not run\n"); return 1;
    }
    nb = slurp(ref, &b);
    sa = image_start(a, na);
    sb = image_start(b, nb);
    if (na - sa != nb - sb || memcmp(a + sa, b + sb, (size_t)(na - sa)) != 0) {
        printf("FAIL: streamed and in-RAM develops differ (%ld vs %ld image bytes)\n", na - sa, nb - sb);
        return 1;
    }
    remove(ref);
    printf("PASS: %ld image bytes, identical to the in-RAM develop\n", na - sa);
    return 0;
}
