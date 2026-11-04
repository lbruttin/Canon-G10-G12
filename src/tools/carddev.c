/*
 * carddev - develop a PowerShot card's photos on the computer.
 *
 * The camera takes about two minutes over a full-size G10 frame; this takes
 * about a second. It runs chdk/soup_dev.c, the same file the camera module
 * runs, so the film version it writes is byte for byte the one the camera
 * would have written (tools/chdktest checks that equivalence).
 *
 * It does what the module's DISP does, for every DCIM folder at once: each
 * camera JPEG that has no film version yet gets one. By default it takes
 * the film, the size and KEEP/REPLACE from the card's own CHDK/FILM/SOUP.CFG,
 * and the stocks from the card's CHDK/FILM, so the result matches what the
 * camera is set to. Photos already developed -- by the camera or by an
 * earlier run -- are left alone, so it can be run again at any time.
 *
 * REPLACE overwrites the camera's originals, so it asks first.
 *
 * usage: carddev [card] [--stock NAME] [--full|--half] [--keep|--replace] [--yes]
 *        card defaults to /Volumes/CANON
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "soup_dev.h"

/* ---- the card, as soup_dev.c asks for it -------------------------------- */
int   sd_open(const char *p)                  { return open(p, O_RDONLY); }
int   sd_create(const char *p)                { return open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644); }
int   sd_read(int h, void *buf, int n)        { return (int)read(h, buf, (size_t)n); }
int   sd_write(int h, const void *buf, int n) { return (int)write(h, buf, (size_t)n); }
int   sd_seek(int h, int pos)                 { return lseek(h, pos, SEEK_SET) < 0 ? -1 : pos; }
void  sd_close(int h)                         { close(h); }
void  sd_remove(const char *p)                { remove(p); }
int   sd_rename(const char *a, const char *b) { return rename(a, b); }
void *sd_alloc(size_t n)                      { return malloc(n); }
void  sd_free(void *p)                        { free(p); }
int   sd_ms(void)                             { return (int)(clock() * 1000.0 / CLOCKS_PER_SEC); }

int sd_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (int)st.st_size : -1;
}

static int g_pct;
void sd_progress(int pct)
{
    if (pct / 10 != g_pct / 10) { printf("."); fflush(stdout); }
    g_pct = pct;
}

static void log_line(void *ctx, const char *msg) { fprintf(stderr, "  %s\n", msg); }

/* ---- settings ----------------------------------------------------------- */
typedef struct {
    char stock[SE_STOCK_NAME];
    int  scale;        /* 1 full, 2 half */
    int  keep;         /* 0: the film version replaces the original */
} settings;

/* The camera's own settings, as the module saved them. */
static void read_cfg(const char *card, settings *s)
{
    char  path[1024], line[128];
    FILE *f;

    snprintf(path, sizeof path, "%s/CHDK/FILM/SOUP.CFG", card);
    if (!(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strncmp(line, "STOCK=", 6) == 0) snprintf(s->stock, sizeof s->stock, "%s", line + 6);
        else if (strncmp(line, "SIZE=", 5) == 0) s->scale = line[5] == '1' ? 1 : 2;
        else if (strncmp(line, "KEEP=", 5) == 0) s->keep = line[5] != '0';
    }
    fclose(f);
}

/* ---- photos ------------------------------------------------------------- */
typedef struct { char dir[16]; char name[16]; } photo;

/* "IMG_1234.JPG" and friends: four characters, four digits, .JPG */
static int is_camera_jpeg(const char *n)
{
    int i;
    if (strlen(n) != 12 || strcasecmp(n + 8, ".JPG") != 0) return 0;
    for (i = 4; i < 8; i++) if (n[i] < '0' || n[i] > '9') return 0;
    return 1;
}

static int by_name(const void *a, const void *b)
{
    const photo *p = a, *q = b;
    int c = strcmp(p->dir, q->dir);
    return c ? c : strcmp(p->name, q->name);
}

/* Every camera JPEG under DCIM, folders in order, files in order. */
static photo *list_photos(const char *card, int *count)
{
    char           path[1024];
    DIR           *top, *d;
    struct dirent *e, *f;
    photo         *list = NULL;
    int            n = 0, cap = 0;

    snprintf(path, sizeof path, "%s/DCIM", card);
    if (!(top = opendir(path))) return NULL;
    while ((e = readdir(top)) != NULL) {
        if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof list->dir) continue;
        snprintf(path, sizeof path, "%s/DCIM/%s", card, e->d_name);
        if (!(d = opendir(path))) continue;
        while ((f = readdir(d)) != NULL) {
            if (!is_camera_jpeg(f->d_name)) continue;
            if (n == cap) {
                cap = cap ? cap * 2 : 256;
                list = realloc(list, (size_t)cap * sizeof *list);
            }
            snprintf(list[n].dir, sizeof list->dir, "%s", e->d_name);
            snprintf(list[n].name, sizeof list->name, "%s", f->d_name);
            n++;
        }
        closedir(d);
    }
    closedir(top);
    if (n) qsort(list, (size_t)n, sizeof *list, by_name);
    *count = n;
    return list;
}

/* Where the film version goes, exactly as the module puts it. */
static void dest_of(const char *card, const photo *p, int keep, char *out, size_t n)
{
    if (keep) snprintf(out, n, "%s/FILM/%s/%s", card, p->dir, p->name);
    else      snprintf(out, n, "%s/DCIM/%s/%s", card, p->dir, p->name);
}

static int developed(const char *card, const photo *p, int keep)
{
    char src[1024], dst[1024];
    snprintf(src, sizeof src, "%s/DCIM/%s/%s", card, p->dir, p->name);
    if (sd_is_developed(src)) return 1;
    if (!keep) return 0;
    dest_of(card, p, keep, dst, sizeof dst);
    return sd_size(dst) > 0;
}

static void usage(void)
{
    fprintf(stderr,
        "usage: carddev [card] [--stock NAME] [--full|--half] [--keep|--replace] [--yes]\n"
        "\n"
        "Develops every photo on a PowerShot card that has no film version yet,\n"
        "exactly as the camera module would. Settings default to the camera's\n"
        "own (CHDK/FILM/SOUP.CFG on the card). card defaults to /Volumes/CANON.\n");
}

int main(int argc, char **argv)
{
    static se_stock stocks[SD_MAX_STOCKS];
    const char *card = "/Volumes/CANON";
    settings    s = { "", 2, 1 };
    char        stockdir[1024], src[1024], dst[1024], tmp[1024], line[16];
    photo      *list;
    int         i, n = 0, nstocks, pick = -1, todo = 0, done = 0, failed = 0, yes = 0;
    int         stock_arg = 0, scale_arg = 0, keep_arg = -1;
    clock_t     t0;

    for (i = 1; i < argc; i++) {
        if      (strcmp(argv[i], "--stock") == 0 && i + 1 < argc) stock_arg = ++i;
        else if (strcmp(argv[i], "--full") == 0)    scale_arg = 1;
        else if (strcmp(argv[i], "--half") == 0)    scale_arg = 2;
        else if (strcmp(argv[i], "--keep") == 0)    keep_arg = 1;
        else if (strcmp(argv[i], "--replace") == 0) keep_arg = 0;
        else if (strcmp(argv[i], "--yes") == 0)     yes = 1;
        else if (argv[i][0] == '-') { usage(); return 2; }
        else card = argv[i];
    }

    read_cfg(card, &s);
    if (stock_arg) snprintf(s.stock, sizeof s.stock, "%s", argv[stock_arg]);
    if (scale_arg) s.scale = scale_arg;
    if (keep_arg >= 0) s.keep = keep_arg;

    se_set_log(log_line, NULL);
    snprintf(stockdir, sizeof stockdir, "%s/CHDK/FILM", card);
    nstocks = sd_load_stocks(stockdir, stocks, SD_MAX_STOCKS);
    for (i = 0; i < nstocks; i++) if (strcasecmp(stocks[i].name, s.stock) == 0) pick = i;
    if (pick < 0) {
        fprintf(stderr, "carddev: no film '%s' in %s. Films there:", s.stock, stockdir);
        for (i = 0; i < nstocks; i++) fprintf(stderr, " %s%s", stocks[i].name, i + 1 < nstocks ? "," : "\n");
        return 1;
    }

    if (!(list = list_photos(card, &n))) {
        fprintf(stderr, "carddev: no DCIM folder on %s. Is the card in?\n", card);
        return 1;
    }
    for (i = 0; i < n; i++) if (!developed(card, &list[i], s.keep)) todo++;

    printf("%s: %d photo%s, %d to develop with %s, %s size, %s\n",
           card, n, n == 1 ? "" : "s", todo, stocks[pick].name,
           s.scale == 1 ? "full" : "half",
           s.keep ? "originals kept (copies in FILM/)" : "REPLACING the originals");
    if (!todo) return 0;

    /* A locked card mounts read-only: say so now, not once per photo. */
    snprintf(tmp, sizeof tmp, "%s/.carddev", card);
    {
        int h = open(tmp, O_WRONLY | O_CREAT, 0644);
        if (h < 0) {
            fprintf(stderr, "carddev: cannot write to %s (%s). If the card's tab is on LOCK, "
                            "slide it up and put the card back in.\n", card, strerror(errno));
            return 1;
        }
        close(h);
        remove(tmp);
    }

    if (!s.keep && !yes) {
        printf("The film version will replace each original, which cannot be undone. Continue? [y/N] ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin) || (line[0] != 'y' && line[0] != 'Y')) {
            printf("Nothing changed.\n");
            return 1;
        }
    }

    t0 = clock();
    for (i = 0; i < n; i++) {
        sd_opts o = { s.scale, 90 };
        se_err  rc;

        if (developed(card, &list[i], s.keep)) continue;
        snprintf(src, sizeof src, "%s/DCIM/%s/%s", card, list[i].dir, list[i].name);
        dest_of(card, &list[i], s.keep, dst, sizeof dst);
        if (s.keep) {
            snprintf(tmp, sizeof tmp, "%s/FILM", card);
            mkdir(tmp, 0755);
            snprintf(tmp, sizeof tmp, "%s/FILM/%s", card, list[i].dir);
            mkdir(tmp, 0755);
            snprintf(tmp, sizeof tmp, "%s/FILM/SOUP.TMP", card);
        } else {
            snprintf(tmp, sizeof tmp, "%s/DCIM/%s/SOUP.TMP", card, list[i].dir);
        }

        printf("  %s/%s ", list[i].dir, list[i].name);
        fflush(stdout);
        g_pct = 0;
        rc = sd_develop(src, tmp, dst, &stocks[pick], &o);
        if (rc == SE_OK) { done++; printf(" done\n"); }
        else             { failed++; printf(" FAILED: %s\n", se_strerror(rc)); }
    }
    printf("%d developed, %d failed, %.0f s\n", done, failed,
           (double)(clock() - t0) / CLOCKS_PER_SEC);
    free(list);
    return failed ? 1 : 0;
}
