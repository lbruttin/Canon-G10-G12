/*
 * soup_dev.h - the PowerShot develop: a camera JPEG in, the same frame with
 * a film look out.
 *
 * This is ml/payload/film.c's develop_preview() with the CR2 taken out. On
 * a G10 or G12 the camera's own JPEG is a file of its own, so there is no
 * preview to dig out of a raw: the develop is decode -> look -> encode,
 * streamed through a few hundred KB, exactly as on the 5D.
 *
 * Nothing in here knows about CHDK. Everything it asks of the camera goes
 * through the sd_* calls below, which chdk/soup.c answers with CHDK's own
 * functions and tools/chdktest.c answers on the desktop -- with short reads
 * and a capped heap, so the file that runs on the camera is the file that
 * was tested.
 */
#ifndef SOUP_DEV_H
#define SOUP_DEV_H

#include "se.h"
#include "stock.h"

/* ---- what the develop needs from the camera ---------------------------- */
int   sd_open(const char *path);                 /* read-only; -1 on failure */
int   sd_create(const char *path);               /* truncating; -1 on failure */
int   sd_read(int h, void *buf, int n);          /* may return less than n   */
int   sd_write(int h, const void *buf, int n);
int   sd_seek(int h, int pos);                   /* absolute; <0 on failure  */
int   sd_size(const char *path);                 /* bytes, or -1 if absent   */
void  sd_close(int h);
void  sd_remove(const char *path);
int   sd_rename(const char *from, const char *to);
void *sd_alloc(size_t n);
void  sd_free(void *p);

/* Called as rows leave the encoder, 0..100. Runs on the develop's task. */
void  sd_progress(int percent);

/* ---- stocks ------------------------------------------------------------ */
#define SD_MAX_STOCKS 24

/*
 * Load the stocks named in <dir>/INDEX.TXT, one file name per line, which
 * is the same card layout as ML/FILM on the 5D. Returns how many loaded.
 * With no index, or nothing in it, a single straight "Standard" stock is
 * returned so the picker never shows an empty list.
 */
int sd_load_stocks(const char *dir, se_stock *out, int max);

/* ---- the develop ------------------------------------------------------- */
typedef struct {
    int         scale;      /* 1 = full size, 2 = half                    */
    int         quality;    /* JPEG quality of the output, 1..100         */
} sd_opts;

/*
 * Develop `in_path` into `out_path`.
 *
 * The output is written under `tmp_path` and renamed into place only once
 * it is complete, so a frame interrupted by the battery or the power
 * switch never leaves behind a truncated JPEG that looks finished.
 */
se_err sd_develop(const char *in_path, const char *tmp_path,
                  const char *out_path, const se_stock *stock,
                  const sd_opts *opts);

/* The arena high-water mark of the last develop, for the desktop test. */
extern size_t sd_last_peak;

#endif /* SOUP_DEV_H */
