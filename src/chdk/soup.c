/*
 * soup.c - Soup Engine as a CHDK module, for the PowerShot G10 and G12.
 *
 * Soup Engine began on the EOS 5D Classic, inside Magic Lantern.
 * PowerShots have no Magic Lantern; they have CHDK, which loads modules
 * (.flt files) from the card and lists the ones marked MTYPE_TOOL under its
 * Tools menu. This is one.
 *
 * What it does is the 5D's JPEG path: pick a stock, and each photo gets a
 * second JPEG with the look on it, written to A/FILM/<folder>/ and never
 * touching the original. The develop itself is soup_dev.c, which is tested
 * on the desktop by tools/chdktest.c; this file is only the camera side --
 * the picker, the card, and a task to run the develop on.
 *
 * The keys, in the picker:
 *
 *   UP / DOWN      choose a film
 *   LEFT / RIGHT   half size or full size
 *   SET            develop the newest photo in the current folder
 *   DISP           develop every photo in it that has no film copy yet
 *   TRASH          AUTO on or off: develop each new photo as it is taken
 *   MENU           close the picker (AUTO keeps running)
 *
 * The develop runs on a task of its own, not on CHDK's. Its deepest call
 * needs about 10 KB of stack -- se_jpeg_dec_info keeps its whole decoder
 * state there -- and CHDK's spytask has 8 KB. A task of our own also means
 * the picker keeps drawing while a frame develops, and that AUTO can go on
 * working after the picker is closed and the camera is back to shooting:
 * CHDK keeps a module loaded for as long as it says it cannot be unloaded.
 */
#include "camera_info.h"
#include "keyboard.h"
#include "shooting.h"
#include "file_counter.h"
#include "clock.h"
#include "meminfo.h"
#include "task.h"
#include "gui.h"
#include "gui_draw.h"
#include "stdlib.h"
#include "string.h"
#include "stdio.h"
#include "dirent.h"
#include "module_def.h"
#include "simple_module.h"

#include "soup_dev.h"

#define SOUP_STOCKS  "A/CHDK/FILM"
#define SOUP_CFG     "A/CHDK/FILM/SOUP.CFG"
#define SOUP_LOG     "A/CHDK/FILM/SOUP.LOG"
#define SOUP_OUT     "A/FILM"
#define SOUP_TMP     "A/FILM/SOUP.TMP"

#define SOUP_STACK   0x6000      /* 24 KB: the develop needs about 10      */
#define SOUP_PRIO    0x1A        /* below spytask (0x19), like DNG's task  */
#define SOUP_QUALITY 90

/* ===================================================================== */
/* The card, the heap, and the log, as soup_dev.c asks for them           */
/* ===================================================================== */

int   sd_open(const char *p)                 { return open(p, O_RDONLY, 0777); }
int   sd_create(const char *p)               { return open(p, O_WRONLY | O_CREAT | O_TRUNC, 0777); }
int   sd_read(int h, void *buf, int n)       { return read(h, buf, n); }
int   sd_write(int h, const void *buf, int n){ return write(h, buf, n); }
int   sd_seek(int h, int pos)                { return lseek(h, pos, SEEK_SET); }
void  sd_close(int h)                        { close(h); }
void  sd_remove(const char *p)               { remove(p); }
void *sd_alloc(size_t n)                     { return malloc((long)n); }
void  sd_free(void *p)                       { free(p); }

int sd_size(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 ? (int)st.st_size : -1;
}

/* What Canon's RenameFile returns is not documented, and no caller in
 * CHDK looks at it. Whether the new name exists afterwards is the answer
 * that matters. */
int sd_rename(const char *from, const char *to)
{
    rename(from, to);
    return sd_size(to) > 0 ? 0 : -1;
}

static volatile int soup_pct;
void sd_progress(int pct) { soup_pct = pct; }

/* Only lines worth reading after the fact go to the card: what was
 * developed, and why anything was not. */
static void soup_log(void *ctx, const char *msg)
{
    int h = open(SOUP_LOG, O_WRONLY | O_CREAT | O_APPEND, 0777);
    if (h < 0) return;
    write(h, msg, strlen(msg));
    write(h, "\n", 1);
    close(h);
}

/* ===================================================================== */
/* State shared by the picker and the task                                */
/* ===================================================================== */

enum { JOB_NONE, JOB_LAST, JOB_ALL };

static se_stock     stocks[SD_MAX_STOCKS];
static int          nstocks;            /* 0 until the task has loaded them */
static int          sel;
static int          scale = 2;
static char         want_stock[SE_STOCK_NAME];

static int          running;            /* the picker is on screen         */
static volatile int task_running;
static volatile int task_quit;
static volatile int job;                /* asked for by the picker          */
static volatile int auto_on;
static volatile int busy;               /* a frame is developing            */
static char         status[48];
static char         current[16];        /* the frame being developed        */
static volatile int need_redraw;

/* ===================================================================== */
/* Settings                                                               */
/* ===================================================================== */

static void cfg_save(void)
{
    char buf[64];
    int  h = open(SOUP_CFG, O_WRONLY | O_CREAT | O_TRUNC, 0777);
    if (h < 0) return;
    sprintf(buf, "STOCK=%s\nSIZE=%d\nAUTO=%d\n",
            nstocks ? stocks[sel].name : "", scale, auto_on);
    write(h, buf, strlen(buf));
    close(h);
}

static void cfg_load(void)
{
    char  buf[96], *p;
    int   h = open(SOUP_CFG, O_RDONLY, 0777), n, i;

    if (h < 0) return;
    n = read(h, buf, sizeof buf - 1);
    close(h);
    if (n <= 0) return;
    buf[n] = '\0';

    for (p = buf; *p; ) {
        char *line = p;
        while (*p && *p != '\n') p++;
        if (*p) *p++ = '\0';
        if (strncmp(line, "STOCK=", 6) == 0) {
            for (i = 0; line[6 + i] && i < SE_STOCK_NAME - 1; i++) want_stock[i] = line[6 + i];
            want_stock[i] = '\0';
        }
        else if (strncmp(line, "SIZE=", 5) == 0) scale = line[5] == '1' ? 1 : 2;
        else if (strncmp(line, "AUTO=", 5) == 0) auto_on = line[5] == '1';
    }
}

/* ===================================================================== */
/* Finding photos                                                         */
/* ===================================================================== */

/* "IMG_1234.JPG" -> 1234; -1 for anything that is not a camera JPEG. */
static int jpeg_number(const char *name)
{
    int i, n = 0;

    if (strlen(name) != 12 || name[8] != '.') return -1;
    if (!(name[9] == 'J' || name[9] == 'j') || !(name[10] == 'P' || name[10] == 'p') ||
        !(name[11] == 'G' || name[11] == 'g')) return -1;
    for (i = 4; i < 8; i++) {
        if (name[i] < '0' || name[i] > '9') return -1;
        n = n * 10 + name[i] - '0';
    }
    return n;
}

/* A/DCIM/100CANON/IMG_1234.JPG -> A/FILM/100CANON/IMG_1234.JPG, making the
 * folders on the way. */
static void out_path(const char *dir, const char *name, char *out)
{
    const char *folder = strrchr(dir, '/') + 1;

    if (sd_size(SOUP_OUT) < 0) mkdir(SOUP_OUT);
    sprintf(out, SOUP_OUT "/%s", folder);
    if (sd_size(out) < 0) mkdir(out);
    sprintf(out + strlen(out), "/%s", name);
}

/*
 * Walk the current folder once. `after` >= 0: return the lowest-numbered
 * JPEG above it, for AUTO and DISP to work through in order. `after` < 0:
 * return the highest-numbered, for SET. Returns the number, or -1.
 */
static int find_jpeg(const char *dir, int after, char *name)
{
    DIR           *d = opendir(dir);
    struct dirent *e;
    int            best = -1, n;

    if (!d) return -1;
    while ((e = readdir(d)) != NULL) {
        if ((n = jpeg_number(e->d_name)) < 0) continue;
        if (after >= 0 ? (n > after && (best < 0 || n < best)) : n > best) {
            best = n;
            strcpy(name, e->d_name);
        }
    }
    closedir(d);
    return best;
}

/* ===================================================================== */
/* The develop task                                                       */
/* ===================================================================== */

/*
 * A JPEG still being written must not be developed half-read. Canon writes
 * it after the shot is over, so wait until the size has stopped moving.
 */
static int settled(const char *path)
{
    int a = sd_size(path), b, i;

    for (i = 0; i < 20; i++) {
        msleep(250);
        b = sd_size(path);
        if (b > 0 && b == a) return 1;
        a = b;
    }
    return 0;
}

static int develop(const char *dir, const char *name)
{
    const se_stock *st = &stocks[sel];  /* the picker may move on meanwhile */
    char    in[64], out[64];
    sd_opts o;
    se_err  rc;
    int     t0 = get_tick_count();

    sprintf(in, "%s/%s", dir, name);
    out_path(dir, name, out);
    if (!settled(in)) { se_log("%s: still being written, skipped", in); return -1; }

    strcpy(current, name);
    soup_pct = 0;
    busy = 1;
    o.scale   = scale;
    o.quality = SOUP_QUALITY;
    rc = sd_develop(in, SOUP_TMP, out, st, &o);
    busy = 0;

    if (rc == SE_OK) {
        se_log("%s -> %s, %s, %d s", in, out, st->name,
               (get_tick_count() - t0) / 1000);
        sprintf(status, "%s done", name);
    } else {
        cam_meminfo m;
        GetMemInfo(&m);
        se_log("%s: %s (largest free block %d KB)", in, se_strerror(rc),
               m.free_block_max_size >> 10);
        sprintf(status, "%s: %s", name, se_strerror(rc));
    }
    return rc == SE_OK ? 0 : -1;
}

/* How long AUTO keeps looking for the file behind a new exposure. */
#define SOUP_AUTO_WAIT_MS 30000

static void soup_task(void)
{
    char dir[32], name[16], out[64];
    long shots = 0, waiting = 0;
    int  armed = 0, newest = 0, n;

    if (!nstocks) {
        int i;
        nstocks = sd_load_stocks(SOUP_STOCKS, stocks, SD_MAX_STOCKS);
        for (i = 0; i < nstocks; i++)
            if (strcmp(stocks[i].name, want_stock) == 0) sel = i;
        need_redraw = 1;
    }

    while (!task_quit) {
        get_target_dir_name(dir);

        if (job == JOB_LAST) {
            if (find_jpeg(dir, -1, name) < 0) strcpy(status, "no photos in this folder");
            else develop(dir, name);
            job = JOB_NONE;
        }
        else if (job == JOB_ALL) {
            int done = 0;
            for (n = find_jpeg(dir, 0, name); n >= 0 && !task_quit && job == JOB_ALL;
                 n = find_jpeg(dir, n, name)) {
                out_path(dir, name, out);
                if (sd_size(out) > 0) continue;
                if (develop(dir, name) == 0) done++;
            }
            if (job == JOB_ALL) sprintf(status, "%d developed", done);
            job = JOB_NONE;
        }
        else if (auto_on) {
            /*
             * Only what is shot from here on. The exposure counter moves
             * with every frame, so the folder is walked when it does, and
             * never while the camera is busy taking the next one.
             *
             * The counter moves before Canon has written the file, so a
             * walk that finds nothing is not the end of it: the change
             * stays pending until a new JPEG turns up or SOUP_AUTO_WAIT_MS
             * has gone by, which only a RAW-only frame should ever reach.
             */
            long now = get_exposure_counter();

            if (!armed) {
                shots  = now;
                newest = find_jpeg(dir, -1, name);
                if (newest < 0) newest = 0;
                armed  = 1;
            }
            else if (now != shots && !shooting_in_progress()) {
                int found = 0;
                if (!waiting) waiting = get_tick_count();
                while ((n = find_jpeg(dir, newest, name)) >= 0 && auto_on && !task_quit) {
                    develop(dir, name);
                    newest = n;
                    found  = 1;
                }
                if (found || get_tick_count() - waiting > SOUP_AUTO_WAIT_MS) {
                    shots   = now;
                    waiting = 0;
                }
            }
        }
        else {
            armed = 0;
        }
        msleep(200);
    }

    task_running = 0;
    ExitTask();
}

/* ===================================================================== */
/* The picker                                                             */
/* ===================================================================== */

#define ROWS 7

static void put_row(int row, const char *s, twoColors cl)
{
    char buf[64];
    int  cols = camera_screen.width / FONT_WIDTH - 2, i;

    if (cols > (int)sizeof buf - 1) cols = (int)sizeof buf - 1;
    for (i = 0; i < cols && s[i]; i++) buf[i] = s[i];
    for (; i < cols; i++) buf[i] = ' ';
    buf[i] = '\0';
    draw_string(camera_screen.disp_left + FONT_WIDTH, FONT_HEIGHT / 2 + row * FONT_HEIGHT, buf, cl);
}

void gui_soup_draw(int force)
{
    static int last_pct = -1, last_busy = -1;
    twoColors  norm = MAKE_COLOR(COLOR_BLACK, COLOR_WHITE);
    twoColors  hi   = MAKE_COLOR(COLOR_WHITE, COLOR_BLACK);
    twoColors  dim  = MAKE_COLOR(COLOR_BLACK, COLOR_GREY);
    char       buf[64];
    int        i, top;

    if (busy != last_busy || soup_pct != last_pct) need_redraw = 1;
    if (!need_redraw && !force) return;
    if (need_redraw == 2 || force)
        draw_rectangle(camera_screen.disp_left, 0, camera_screen.disp_right,
                       camera_screen.height - 1, MAKE_COLOR(COLOR_BLACK, COLOR_BLACK),
                       RECT_BORDER0 | DRAW_FILLED);
    need_redraw = 0;
    last_pct  = soup_pct;
    last_busy = busy;

    sprintf(buf, "SOUP ENGINE  %s  AUTO %s", scale == 1 ? "FULL" : "HALF", auto_on ? "ON" : "OFF");
    put_row(0, buf, MAKE_COLOR(COLOR_BLACK, COLOR_YELLOW));

    if (!nstocks) {
        put_row(2, "loading films...", norm);
        return;
    }

    top = sel - ROWS / 2;
    if (top > nstocks - ROWS) top = nstocks - ROWS;
    if (top < 0) top = 0;
    for (i = 0; i < ROWS; i++) {
        int k = top + i;
        if (k < nstocks) sprintf(buf, " %s", stocks[k].name);
        else buf[0] = '\0';
        put_row(2 + i, buf, k == sel ? hi : norm);
    }
    put_row(2 + ROWS, stocks[sel].desc, dim);

    if (busy) sprintf(buf, "%s  %d%%", current, soup_pct);
    else      strcpy(buf, status);
    put_row(4 + ROWS, buf, norm);

    put_row(6 + ROWS, "SET last  DISP all  TRASH auto", dim);
}

int gui_soup_kbd_process(void)
{
    if (!nstocks) return 0;

    switch (kbd_get_autoclicked_key()) {
    case KEY_UP:    if (sel > 0) sel--;           break;
    case KEY_DOWN:  if (sel < nstocks - 1) sel++; break;
    case KEY_LEFT:
    case KEY_RIGHT: scale = scale == 1 ? 2 : 1;   break;
    case KEY_SET:     if (job == JOB_NONE) job = JOB_LAST; break;
    case KEY_DISPLAY: if (job == JOB_NONE) job = JOB_ALL;  break;
    case KEY_ERASE:   auto_on = !auto_on; strcpy(status, auto_on ? "AUTO: new photos" : ""); break;
    default: return 0;
    }
    cfg_save();
    need_redraw = 1;
    return 0;
}

void gui_soup_menu_kbd_process(void)
{
    /* Leaving the picker stops a DISP run between frames; AUTO goes on. */
    if (job == JOB_ALL) job = JOB_NONE;
    running = 0;
    if (!auto_on) task_quit = 1;
    gui_default_kbd_process_menu_btn();
}

gui_handler GUI_MODE_SOUP =
    { GUI_MODE_MODULE, gui_soup_draw, gui_soup_kbd_process, gui_soup_menu_kbd_process, 0, GUI_MODE_FLAG_NODRAWRESTORE };

/***************** BEGIN OF AUXILARY PART *********************
ATTENTION: DO NOT REMOVE OR CHANGE SIGNATURES IN THIS SECTION
**************************************************************/

int _run()
{
    running = 1;
    /* Closed and reopened before the task saw task_quit: let it finish,
     * or the picker would be talking to a task that is on its way out. */
    while (task_running && task_quit) msleep(10);
    if (!task_running) {
        se_set_log(soup_log, 0);
        cfg_load();
        task_quit = 0;
        task_running = 1;
        CreateTask("SoupDev", SOUP_PRIO, SOUP_STACK, soup_task);
    }
    gui_set_mode(&GUI_MODE_SOUP);
    need_redraw = 2;
    return 0;
}

/* CHDK asks this every keyboard tick, and unloads the module the moment it
 * says yes -- so it must not while the task is alive, AUTO or not. */
int _module_can_unload()
{
    return !running && !task_running;
}

int _module_exit_alt()
{
    if (!auto_on) task_quit = 1;
    running = 0;
    return 0;
}

libsimple_sym _librun =
{
    {
        0,
        0,
        _module_can_unload,
        _module_exit_alt,
        _run
    }
};

/*************** END OF AUXILARY PART *******************/

ModuleInfo _module_info =
{
    MODULEINFO_V1_MAGICNUM,
    sizeof(ModuleInfo),
    SIMPLE_MODULE_VERSION,      // Module version

    ANY_CHDK_BRANCH, 0, OPT_ARCHITECTURE,   // Requirements of CHDK version
    ANY_PLATFORM_ALLOWED,       // Specify platform dependency

    (int)"Soup Engine",
    MTYPE_TOOL,

    &_librun.base,

    ANY_VERSION,                // CONF version
    CAM_SCREEN_VERSION,         // CAM SCREEN version
    ANY_VERSION,                // CAM SENSOR version
    CAM_INFO_VERSION,           // CAM INFO version

    0,
};
