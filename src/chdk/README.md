# The CHDK module

Soup Engine for PowerShots, as a CHDK module (`SOUP.FLT`). For installing
and using it, see the [main README](../../README.md). This page covers how
it works, what's tested, and how to build it.

> ⚠️ **Not yet run on a camera.** It builds into a valid CHDK module, and
> the develop is tested on the desktop against real G10 frames. Everything
> below about the camera itself is untested.

## How it works

CHDK loads modules from `A/CHDK/MODULES` and lists the ones marked as
tools under its Tools menu. This one does the engine's JPEG path. It
takes the camera's own JPEG, decodes it, runs it through the film look,
and encodes a new JPEG. All of that is streamed, so it needs a few
hundred KB rather than a whole frame in memory.

The develop runs on a task of its own, not on CHDK's. Its deepest call
needs about 10 KB of stack, and CHDK's own task has 8 KB. A separate task
also keeps the picker drawing during a develop, and lets AUTO keep working
after the picker is closed. CHDK keeps a module loaded for as long as the
module says it can't be unloaded.

Each output is written as `A/FILM/SOUP.TMP` and renamed into place once
it's complete. If the battery dies mid-develop, you never get a truncated
JPEG that looks finished.

## Cameras

Written for the **G10** and **G12**. Both have DIGIC 4, which uses the same
ARM946E-S core the engine was first written for, on the 5D Classic's
DIGIC II. Nothing in the module depends on the model, so other DIGIC 4
PowerShots with a CHDK port may work too.

## Not done (yet)

- **Raw.** CHDK can save raw files, but the engine's raw path is built
  around the 5D's CR2 and its colour matrix. The G10 and G12 would need
  their own matrices, and CHDK's DNG files would need a reader.
- **JPEGs with restart markers.** The streaming decoder refuses them and
  says so in `SOUP.LOG`. The G10's JPEGs don't use them. No
  straight-from-camera G12 file has been checked yet. If yours do, every
  develop fails with "unsupported variant", and that's worth an issue.
- **Progress while shooting.** With AUTO on and the picker closed, nothing
  on screen shows that a frame is developing.

## Memory

A develop takes about 350 KB of Canon's heap at half size and 600 KB at
full size, all in one allocation, and gives it back afterwards. The
module's own static memory is about 24 KB, mostly the loaded stocks. If
an allocation fails, `SOUP.LOG` records the largest free block the camera
had.

## Building

You need [CHDK's source](https://chdk.fandom.com/wiki/CHDK) and
an `arm-none-eabi-gcc` that CHDK supports: GCC 4, 5, or 8 to 11. Then:

```sh
cp src/chdk/makefile_usermodules.inc <chdk>/trunk/modules/
make -C <chdk>/trunk PLATFORM=g12 PLATFORMSUB=100c \
     SOUP=/path/to/this/repo/src fir
# -> <chdk>/trunk/modules/.o/soup.flt, which goes on the card as CHDK/MODULES/SOUP.FLT
```

CHDK's `modules/Makefile` includes `makefile_usermodules.inc` if it
exists; that's CHDK's hook for out-of-tree modules. Use your camera's
`PLATFORM` and `PLATFORMSUB` (its firmware version); the module doesn't
depend on either.

The module is built in ARM mode rather than CHDK's usual Thumb, because
the per-pixel code relies on 32×32→64 multiplies. It also carries the
libgcc soft-float helpers that CHDK doesn't export.

## Testing on the desktop

```sh
mkdir -p samples/powershot && cp /path/to/IMG_*.JPG samples/powershot/
make chdktest        # from src/
```

`soup_dev.c` contains no CHDK code. Everything it needs from the camera
goes through the `sd_*` calls in `soup_dev.h`. `tools/chdktest.c` answers
those calls with a card that returns half its reads short and a heap
capped at 1 MB, and checks that each output is byte-for-byte identical to
a develop done with the whole file in memory. It also checks the module's
`vsnprintf` against the system's.

## Files

| | |
|:--|:--|
| `soup.c` | the module: the picker, the card, the develop task, AUTO |
| `soup_dev.c`, `soup_dev.h` | the develop: camera JPEG → look → JPEG, streamed |
| `vsnprintf.c` | the one libc function the engine needs that CHDK doesn't export |
| `makefile_usermodules.inc` | the build rules, dropped into CHDK's `modules/` |
| `libgcc.ld` | folds libgcc's helpers into one section, so `elf2flt` accepts them |
| `stdint.h` | the header CHDK's `-nostdinc` build is missing |
