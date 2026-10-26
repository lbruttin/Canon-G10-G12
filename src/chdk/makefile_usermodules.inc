# Soup Engine as a CHDK module: builds SOUP.FLT.
#
# CHDK's modules/Makefile includes modules/makefile_usermodules.inc if it
# exists, for exactly this. Copy this file there, and tell it where this
# repo's src/ is:
#
#   cp src/chdk/makefile_usermodules.inc <chdk>/trunk/modules/
#   make -C <chdk>/trunk PLATFORM=g10 PLATFORMSUB=104a SOUP=/path/to/soup-engine/src fir
#
# The module comes out as <chdk>/trunk/modules/.o/soup.flt (for a CHDK
# build that also packs a zip, it is in CHDK/MODULES there too).

ifndef SOUP
    $(error SOUP must point at soup-engine/src)
endif

FLTS += $Osoup.flt

# From the engine, only what the JPEG develop reaches. cr2.c, debayer.c and
# the rest of the raw path are not ported: see src/chdk/README.md.
SOUP_CHDK   = soup soup_dev vsnprintf
SOUP_ENGINE = se stock grade lut look jpeg_dec jpeg_enc wb
SOUP_OBJ    = $(SOUP_CHDK:%=$Osoupc_%.o) $(SOUP_ENGINE:%=$Osoupe_%.o)

# ARM, not CHDK's usual Thumb. The per-pixel paths lean on 32x32->64
# multiplies, which Thumb-1 has no instruction for, and this is the same
# ARM946E-S the 5D build was tuned on.
#
# -nostdinc as CHDK's own rules do; src/chdk supplies the one header
# CHDK lacks, stdint.h.
SOUP_CFLAGS = -marm -nostdinc -std=gnu99 -I$(SOUP)/chdk -I$(SOUP)/engine

# The engine bakes its curves in float. CHDK exports only some of the
# soft-float helpers that needs, so the module carries its own: exactly the
# ones its objects call, pulled out of libgcc and folded into one .text by
# libgcc.ld, because elf2flt cannot take libgcc's sections as they come.
SOUP_LIBGCC := $(shell $(CC) $(CFLAGS) -marm -print-libgcc-file-name)

$Osoup.flt: $(SOUP_OBJ) $Osoup_gcc.o

$Osoup_gcc.o: $(SOUP_OBJ) $(SOUP)/chdk/libgcc.ld
	@echo libgcc '->' $@
	$(CC) -nostdlib -r -Wl,-T,$(SOUP)/chdk/libgcc.ld -o $@ \
	    $$($(NM) -u $(SOUP_OBJ) | awk '/ __aeabi_|^ *U __/ { print "-Wl,-u," $$NF }' | sort -u) \
	    $(SOUP_LIBGCC)

$Osoupc_%.o: $(SOUP)/chdk/%.c $(SOUP)/chdk/soup_dev.h
	@echo $< '->' $@
	$(CC) $(CFLAGS) $(SOUP_CFLAGS) -c -o $@ $<

$Osoupe_%.o: $(SOUP)/engine/%.c
	@echo $< '->' $@
	$(CC) $(CFLAGS) $(SOUP_CFLAGS) -c -o $@ $<
