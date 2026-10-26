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

# Only what the JPEG develop reaches. cr2.c, debayer.c and the rest of the
# raw path are not ported: see src/chdk/README.md.
SOUP_ENGINE = se stock grade lut look jpeg_dec jpeg_enc wb
SOUP_OBJ    = $Osoup.o $Osoup_dev.o $Osoup_libc.o $(SOUP_ENGINE:%=$Osoup_%.o)

# ARM, not CHDK's usual Thumb. The per-pixel paths lean on 32x32->64
# multiplies, which Thumb-1 has no instruction for, and this is the same
# ARM946E-S the 5D build was tuned on.
#
# -nostdinc as CHDK's own rules do; src/chdk comes first for the two
# headers CHDK lacks (stdint.h, and cam.h for the payload's vsnprintf).
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

$Osoup.o: $(SOUP)/chdk/soup.c $(SOUP)/chdk/soup_dev.h
	@echo $< '->' $@
	$(CC) $(CFLAGS) $(SOUP_CFLAGS) -c -o $@ $<

$Osoup_dev.o: $(SOUP)/chdk/soup_dev.c $(SOUP)/chdk/soup_dev.h
	@echo $< '->' $@
	$(CC) $(CFLAGS) $(SOUP_CFLAGS) -c -o $@ $<

# Only vsnprintf is wanted from the payload's libc; CHDK has the rest, so
# those are renamed out of the way exactly as the host's camtest does.
$Osoup_libc.o: $(SOUP)/ml/payload/libc.c
	@echo $< '->' $@
	$(CC) $(CFLAGS) $(SOUP_CFLAGS) -Dmemcpy=se_x_memcpy -Dmemset=se_x_memset \
	    -Dstrlen=se_x_strlen -Dstrcmp=se_x_strcmp -Dstrncmp=se_x_strncmp -c -o $@ $<

$Osoup_%.o: $(SOUP)/engine/%.c
	@echo $< '->' $@
	$(CC) $(CFLAGS) $(SOUP_CFLAGS) -c -o $@ $<
