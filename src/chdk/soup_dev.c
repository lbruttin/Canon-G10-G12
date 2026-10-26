/*
 * soup_dev.c - the PowerShot develop. See soup_dev.h.
 */
#include "se_libc.h"
#include "soup_dev.h"
#include "look.h"
#include "jpeg_dec.h"
#include "jpeg_enc.h"

size_t sd_last_peak;

/* ===================================================================== */
/* Reading                                                                */
/* ===================================================================== */

/*
 * Seek and read until `n` bytes are in hand.
 *
 * The 5D taught this one the hard way: a read that returns less than it
 * was asked for is allowed, and treating it as complete corrupted every
 * frame until it was found. A zero is retried a few times, because a busy
 * card and the end of the file look the same from here.
 */
#define SD_READ_RETRIES 8

static int read_at(int h, int off, void *buf, int n)
{
    uint8_t *p = (uint8_t *)buf;
    int done = 0, idle = 0;

    while (done < n) {
        int k;
        if (sd_seek(h, off + done) < 0) break;
        k = sd_read(h, p + done, n - done);
        if (k > 0) { done += k; idle = 0; continue; }
        if (k < 0 || ++idle > SD_READ_RETRIES) break;
    }
    return done;
}

/* Read a whole small file into `buf`, NUL-terminated. Returns its length. */
static int read_text(const char *path, char *buf, int cap)
{
    int size = sd_size(path), h, got;

    if (size <= 0) return -1;
    if (size > cap - 1) size = cap - 1;
    if ((h = sd_open(path)) == -1) return -1;
    got = read_at(h, 0, buf, size);
    sd_close(h);
    if (got != size) return -1;
    buf[got] = '\0';
    return got;
}

/* ===================================================================== */
/* Stocks                                                                 */
/* ===================================================================== */

#define SD_INDEX_MAX 2048
#define SD_STOCK_MAX 8192
#define SD_PATH_MAX  128

static void str_copy(char *dst, size_t n, const char *src)
{
    size_t i;
    for (i = 0; i + 1 < n && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

int sd_load_stocks(const char *dir, se_stock *out, int max)
{
    char  path[SD_PATH_MAX];
    char *text, *body, *line;
    int   got, i, n = 0;

    text = (char *)sd_alloc(SD_INDEX_MAX + SD_STOCK_MAX);
    if (!text) goto fallback;
    body = text + SD_INDEX_MAX;

    str_copy(path, sizeof path, dir);
    str_copy(path + strlen(path), sizeof path - strlen(path), "/INDEX.TXT");
    got = read_text(path, text, SD_INDEX_MAX);

    for (i = 0, line = text; got > 0 && i <= got && n < max; i++) {
        size_t dl;
        int    len;

        if (text[i] != '\n' && text[i] != '\r' && text[i] != '\0') continue;
        text[i] = '\0';
        if (!line[0]) { line = text + i + 1; continue; }

        str_copy(path, sizeof path, dir);
        dl = strlen(path);
        str_copy(path + dl, sizeof path - dl, "/");
        str_copy(path + dl + 1, sizeof path - dl - 1, line);
        line = text + i + 1;

        len = read_text(path, body, SD_STOCK_MAX);
        if (len <= 0) { se_log("stocks: %s unreadable", path); continue; }

        se_stock_init(&out[n]);
        if (se_stock_parse(body, (size_t)len, &out[n]) != SE_OK) {
            se_log("stocks: %s unparsable", path);
            continue;
        }
        if (!out[n].name[0]) str_copy(out[n].name, SE_STOCK_NAME, path + dl + 1);
        n++;
    }
    sd_free(text);

fallback:
    if (n == 0) {
        se_stock_init(&out[0]);
        str_copy(out[0].name, SE_STOCK_NAME, "Standard");
        str_copy(out[0].desc, SE_STOCK_DESC, "no INDEX.TXT");
        n = 1;
    }
    return n;
}

/* ===================================================================== */
/* The camera JPEG's header                                               */
/* ===================================================================== */

/*
 * The decoder wants the markers up to the scan in one buffer. A camera
 * JPEG puts its EXIF there first -- up to 64 KB per APP1, and the G10
 * writes two -- so reading "the first N KB" would mean holding a thumbnail
 * we never look at, in the one resource there is least of.
 *
 * So the header is rebuilt instead: SOI, then every table segment, then
 * the SOS, with the APPn and COM segments left on the card. That is a few
 * KB. The decoder's reads past the scan header are mapped back onto the
 * file by `jpeg_read`, so it never knows the two differ.
 */
#define SD_HDR_MAX   8192
/* IFD0 and its strings sit at the front. Canon's own put them in the first
 * few hundred bytes; a file re-saved by Windows had them past 2 KB. */
#define SD_EXIF_READ 4096

typedef struct {
    int      h;
    uint8_t *hdr;
    int      hdrlen;     /* the rebuilt header, ending with the SOS    */
    int      scan;       /* file offset of the first entropy-coded byte */
    char     model[32];
    char     datetime[24];
} jpeg_src;

/* IFD0's Model and DateTime, so the output says what took it and when. */
static void exif_ifd0(const uint8_t *t, int n, jpeg_src *s)
{
    se_rd r;
    int   be, count, i;

    if (n < 8) return;
    if (t[0] == 'I' && t[1] == 'I') be = 0;
    else if (t[0] == 'M' && t[1] == 'M') be = 1;
    else return;

    se_rd_init(&r, t, (size_t)n, be);
    se_rd_seek(&r, 4);
    se_rd_seek(&r, se_rd_u32(&r));
    count = se_rd_u16(&r);
    for (i = 0; i < count && !r.err; i++) {
        unsigned tag  = se_rd_u16(&r);
        unsigned type = se_rd_u16(&r);
        uint32_t len  = se_rd_u32(&r);
        uint32_t off  = se_rd_u32(&r);
        char    *dst  = tag == 0x0110 ? s->model : tag == 0x0132 ? s->datetime : 0;
        size_t   cap  = tag == 0x0110 ? sizeof s->model : sizeof s->datetime;

        if (!dst || type != 2 || len <= 4 || off >= (uint32_t)n) continue;
        if (len > (uint32_t)n - off) len = (uint32_t)n - off;
        if (len > cap) len = (uint32_t)cap;
        memcpy(dst, t + off, len);
        dst[len - 1] = '\0';
    }
}

static se_err read_header(jpeg_src *s, uint8_t *exif)
{
    uint8_t m[4];
    int     pos = 2, len;

    if (read_at(s->h, 0, m, 2) != 2 || m[0] != 0xFF || m[1] != 0xD8)
        return SE_E_BADFILE;
    s->hdr[0] = 0xFF; s->hdr[1] = 0xD8;
    s->hdrlen = 2;

    for (;;) {
        if (read_at(s->h, pos, m, 4) != 4) return SE_E_TRUNCATED;
        if (m[0] != 0xFF) return SE_E_CORRUPT;
        if (m[1] == 0xFF) { pos++; continue; }          /* fill byte */
        len = (m[2] << 8) | m[3];
        if (len < 2) return SE_E_CORRUPT;

        if ((m[1] >= 0xE0 && m[1] <= 0xEF) || m[1] == 0xFE) {
            if (m[1] == 0xE1 && !s->model[0] && len > 8 + 8) {
                int n = SE_MIN(len - 2, SD_EXIF_READ);
                if (read_at(s->h, pos + 4, exif, n) == n &&
                    strncmp((const char *)exif, "Exif", 4) == 0 &&
                    exif[4] == 0 && exif[5] == 0)
                    exif_ifd0(exif + 6, n - 6, s);
            }
            pos += 2 + len;
            continue;
        }

        if (s->hdrlen + 2 + len > SD_HDR_MAX) return SE_E_UNSUPPORTED;
        if (read_at(s->h, pos, s->hdr + s->hdrlen, 2 + len) != 2 + len)
            return SE_E_TRUNCATED;
        s->hdrlen += 2 + len;
        pos       += 2 + len;
        if (m[1] == 0xDA) { s->scan = pos; return SE_OK; }
    }
}

/* The decoder's offsets are into the rebuilt header; past its end they
 * are the entropy-coded data, which is still on the card. */
static int jpeg_read(void *ctx, uint32_t off, void *dst, uint32_t n)
{
    jpeg_src *s = (jpeg_src *)ctx;
    uint8_t  *d = (uint8_t *)dst;

    if (off < (uint32_t)s->hdrlen) {
        uint32_t k = SE_MIN(n, (uint32_t)s->hdrlen - off);
        memcpy(d, s->hdr + off, k);
        d += k; off += k; n -= k;
    }
    if (!n) return 0;
    off = off - (uint32_t)s->hdrlen + (uint32_t)s->scan;
    return read_at(s->h, (int)off, d, (int)n) == (int)n ? 0 : -1;
}

/* ===================================================================== */
/* Writing                                                                */
/* ===================================================================== */

/*
 * Two buffers, used alternately, as on the 5D: Canon's write may return
 * before the card has finished reading the caller's buffer, so the buffer
 * just handed over is never the one refilled next.
 */
#define SD_OUTHALF (24 * 1024)
#define SD_OUTBUF  (SD_OUTHALF * 2)

typedef struct {
    int      h;
    uint8_t *buf[2];
    int      cur, len, err;
} jpeg_out;

static int out_flush(jpeg_out *o)
{
    int n = o->len;
    if (!n) return 0;
    o->len = 0;
    if (sd_write(o->h, o->buf[o->cur], n) != n) { o->err = 1; return -1; }
    o->cur ^= 1;
    return 0;
}

static int out_write(void *ctx, const uint8_t *data, size_t len)
{
    jpeg_out *o = (jpeg_out *)ctx;
    size_t done = 0;

    while (done < len) {
        int take = (int)SE_MIN(len - done, (size_t)(SD_OUTHALF - o->len));
        memcpy(o->buf[o->cur] + o->len, data + done, (size_t)take);
        o->len += take;
        done   += (size_t)take;
        if (o->len == SD_OUTHALF && out_flush(o) != 0) return -1;
    }
    return 0;
}

/* ===================================================================== */
/* The develop                                                            */
/* ===================================================================== */

/*
 * The decoder keeps one MCU row per component plus an RGB row, all at the
 * output width; the encoder keeps itself and an eight-row strip. On a G10
 * frame tools/chdktest measures a peak of 298 KB at 4416 wide and 117 KB
 * at 2208, which this covers by 17% and 60%.
 */
#define SD_SCRATCH(w) ((size_t)32 * 1024 + (size_t)(w) * 72)

typedef struct {
    se_jpeg     *enc;
    se_look_run *run;
    int          ow, oh, rows, pct, err;
} look_ctx;

static int look_emit(void *ctx, const uint8_t *row, int width)
{
    look_ctx *L = (look_ctx *)ctx;
    int pct;

    if (se_jpeg_write_rows(L->enc, row, 1) != SE_OK) { L->err = 1; return -1; }
    pct = ++L->rows * 100 / L->oh;
    if (pct != L->pct) sd_progress(L->pct = pct);
    return 0;
}

static int look_rows(void *ctx, const uint8_t *rgb, int nrows, int width)
{
    look_ctx *L = (look_ctx *)ctx;

    if (width != L->ow) { L->err = 1; return -1; }
    if (se_look_rows(L->run, rgb, nrows, look_emit, L) != SE_OK) {
        L->err = 1; return -1;
    }
    return 0;
}

se_err sd_develop(const char *in_path, const char *tmp_path,
                  const char *out_path, const se_stock *stock,
                  const sd_opts *opts)
{
    jpeg_src     src;
    jpeg_out     out;
    look_ctx     L;
    se_look      lk;
    se_jdec_info ji;
    se_jpeg_exif ex;
    se_arena     arena, lookar;
    uint8_t     *hdr = 0, *block = 0, *strip;
    size_t       lookneed, scratch;
    int          size, ow, oh;
    se_err       rc;

    memset(&src, 0, sizeof src);
    memset(&out, 0, sizeof out);
    memset(&L, 0, sizeof L);
    src.h = out.h = -1;

    if ((size = sd_size(in_path)) <= 0) return SE_E_IO;
    if ((src.h = sd_open(in_path)) == -1) return SE_E_IO;

    /* The header buffer doubles as the EXIF window: the EXIF is read and
     * done with before the first table segment lands at the front. */
    hdr = (uint8_t *)sd_alloc(SD_HDR_MAX + SD_EXIF_READ);
    if (!hdr) { rc = SE_E_NOMEM; goto done; }
    src.hdr = hdr;
    rc = read_header(&src, hdr + SD_HDR_MAX);
    if (rc != SE_OK) { se_log("%s: no usable JPEG header (rc=%d)", in_path, rc); goto done; }

    rc = se_jpeg_dec_info(src.hdr, (size_t)src.hdrlen, &ji);
    if (rc != SE_OK) goto done;
    se_jpeg_scaled_size(ji.width, ji.height, opts->scale, &ow, &oh);

    /* Grain and halation are in output pixels, so they follow the scale. */
    se_look_from_stock(stock, opts->scale, &lk);
    lookneed = se_look_fit(&lk, ow, SE_LOOK_BUDGET);
    scratch  = SD_SCRATCH(ow);

    block = (uint8_t *)sd_alloc(SD_OUTBUF + scratch + lookneed + 64);
    if (!block) {
        se_log("develop: %u KB would not fit",
               (unsigned)((SD_OUTBUF + scratch + lookneed) >> 10));
        rc = SE_E_NOMEM; goto done;
    }
    out.buf[0] = block;
    out.buf[1] = block + SD_OUTHALF;
    se_arena_init(&arena,  block + SD_OUTBUF, scratch);
    se_arena_init(&lookar, block + SD_OUTBUF + scratch, lookneed + 64);

    L.enc = SE_NEW(&arena, se_jpeg, 1);
    strip = SE_NEW(&arena, uint8_t, (size_t)8 * ow * 3);
    if (!L.enc || !strip) { rc = SE_E_NOMEM; goto done; }
    rc = se_look_begin(&L.run, &lk, ow, &lookar);
    if (rc != SE_OK) goto done;
    L.ow = ow;
    L.oh = oh;

    if ((out.h = sd_create(tmp_path)) == -1) { rc = SE_E_IO; goto done; }

    memset(&ex, 0, sizeof ex);
    ex.make     = "Canon";
    ex.model    = src.model;
    ex.software = stock->name;
    ex.datetime = src.datetime;
    rc = se_jpeg_begin(L.enc, ow, oh, opts->quality, strip, &ex, out_write, &out);
    if (rc != SE_OK) goto done;

    rc = se_jpeg_decode_stream_scaled(src.hdr, (size_t)src.hdrlen,
                                      (uint32_t)(src.hdrlen + size - src.scan),
                                      jpeg_read, &src, opts->scale, &arena,
                                      look_rows, &L, &ji);
    /* The look's delay line still holds the last `radius` rows. */
    if (rc == SE_OK && !L.err) rc = se_look_end(L.run, look_emit, &L);
    if (rc == SE_OK && !L.err) rc = se_jpeg_end(L.enc);
    if (rc == SE_OK && out_flush(&out) != 0) rc = SE_E_IO;
    if (rc == SE_OK && (L.err || out.err)) rc = SE_E_IO;
    sd_last_peak = arena.peak;

done:
    if (out.h != -1) {
        sd_close(out.h);
        if (rc == SE_OK) {
            sd_remove(out_path);
            if (sd_rename(tmp_path, out_path) != 0) rc = SE_E_IO;
        }
        if (rc != SE_OK) sd_remove(tmp_path);
    }
    if (src.h != -1) sd_close(src.h);
    if (block) sd_free(block);
    if (hdr) sd_free(hdr);
    return rc;
}
