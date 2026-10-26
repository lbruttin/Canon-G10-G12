# Soup Engine for PowerShot (CHDK)

> ⚠️ **Experimental, and not yet run on a camera.** It builds into a valid
> CHDK module, and the develop is tested on the desktop against real G10
> frames. Nobody has run it on a G10 or G12 yet. Try it on a card you
> don't mind reformatting.

The 5D build lives inside Magic Lantern. PowerShots don't have Magic
Lantern; they have [CHDK](https://chdk.fandom.com/wiki/CHDK), which loads
modules from the card. This directory is Soup Engine as one of those
modules.

It does what the 5D does on its default `SOURCE: JPEG` path. You pick a
film, and each photo gets a second JPEG with the look on it. The original
is never touched.

```
A/DCIM/100CANON/IMG_1234.JPG     the camera's photo, untouched
A/FILM/100CANON/IMG_1234.JPG     the same photo with the look on it
```

The copies go in their own folder, not next to the original. That keeps
Canon's playback and file numbering out of it, but it also means the
copies don't show up when you review photos on the camera.

## Cameras

Written for the **G10** and the **G12**. Both have DIGIC 4, which uses the
same ARM946E-S core as the 5D Classic's DIGIC II. Nothing in the module
depends on the model, so other DIGIC 4 PowerShots with a CHDK port may
work as well.

## Installing

1. Install CHDK for your camera and firmware. Their wiki explains how to
   find your firmware version.
2. Build `soup.flt` (see below) and copy it to `A/CHDK/MODULES/SOUP.FLT`.
3. Copy the film stocks, the files in [`../../card/ML/FILM/`](../../card/ML/FILM),
   to `A/CHDK/FILM/`. They're the same `.fs` files the 5D uses, and the
   [Soup Stocks](https://jiijii0.github.io/soup-stocks/) editor works for
   both.

## Using it

In CHDK's menu: **Tools → Soup Engine**.

| key | what it does |
|:--|:--|
| **Up / Down** | choose a film |
| **Left / Right** | half size or full size |
| **SET** | develop the newest photo in the current folder |
| **DISP** | develop every photo in the folder that has no film copy yet |
| **Trash** | AUTO on or off: develop each new photo after it's taken |
| **MENU** | close the picker. AUTO keeps running. |

The film, the size and AUTO are remembered in `A/CHDK/FILM/SOUP.CFG`.
What was developed, and why anything wasn't, is logged to
`A/CHDK/FILM/SOUP.LOG`.

Developing runs on its own task, so the picker keeps updating and you
can keep shooting with AUTO on. Expect roughly a minute per photo at half
size. That's an estimate scaled from the 5D's timings, not a measurement.

## What it doesn't do (yet)

- **`SOURCE: RAW`.** CHDK can save raw files, but the engine's raw path
  is built around the 5D's CR2 and its colour matrix. The G10 and G12 need
  their own matrices, and CHDK's DNG files would need a reader.
- **JPEGs with restart markers.** The streaming decoder refuses them and
  says so in the log. The G10's own JPEGs don't use them. No
  straight-from-camera G12 file has been checked yet. If yours do, every
  develop will fail with "unsupported variant", and that's worth an issue.
- **Progress while shooting.** With AUTO on and the picker closed, nothing
  on the screen shows that a frame is developing.

## Memory

The develop takes about 350 KB from Canon's heap at half size and 600 KB
at full size, then gives it back. `tools/chdktest` measures this on every
run. If the camera can't spare it, the log records the largest free block
it found.

## Building

You need CHDK's source and an `arm-none-eabi-gcc` that CHDK supports
(GCC 4, 5 or 8 to 11; check their build docs). Then:

```sh
cp src/chdk/makefile_usermodules.inc <chdk>/trunk/modules/
make -C <chdk>/trunk PLATFORM=g12 PLATFORMSUB=100c \
     SOUP=/path/to/soup-engine/src fir
# -> <chdk>/trunk/modules/.o/soup.flt
```

CHDK's `modules/Makefile` includes `makefile_usermodules.inc` if it
exists; that file is CHDK's hook for out-of-tree modules. Use your own
`PLATFORM` and `PLATFORMSUB` (camera and firmware). The module isn't tied
to either.

## Testing on the desktop

```sh
mkdir -p samples/powershot && cp /path/to/IMG_*.JPG samples/powershot/
make chdktest
```

This runs [`soup_dev.c`](soup_dev.c), the same file that runs on the
camera, with half of all reads coming back short and a 1 MB heap cap. It
checks that each output is byte-for-byte identical to a develop done
with the whole file in RAM.

## Files

| | |
|:--|:--|
| `soup.c` | the module: the picker, the card, the develop task, AUTO |
| `soup_dev.c` | the develop: camera JPEG → look → JPEG, streamed. No CHDK in it. |
| `makefile_usermodules.inc` | the build rules, dropped into CHDK's `modules/` |
| `libgcc.ld` | folds the soft-float helpers into one section so `elf2flt` accepts them |
| `stdint.h`, `cam.h` | the two headers CHDK's `-nostdinc` build is missing |
