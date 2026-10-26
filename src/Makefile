# Soup Engine

# Raw files for the desktop tool's sample-driven checks, flat or one folder
# deep under samples/.
SAMPLES := $(wildcard samples/*.CR2) $(wildcard samples/*/*.CR2)

.PHONY: all test test-full chdktest asan arm clean contact

all:
	@$(MAKE) -s -C host

# The checks that need no sample files. Run this one by default.
test: all
	@./filmdev selftest

# Everything, including the sample-driven checks and the sanitiser build.
test-full: all
	@./filmdev selftest
	@echo
	@for f in $(SAMPLES); do ./filmdev stress $$f 200 || exit 1; echo; done
	@echo "--- rebuilding under ASan + UBSan ---"
	@$(MAKE) -s -C host asan
	@./filmdev selftest
	@for f in $(SAMPLES); do ./filmdev stress $$f 30 || exit 1; done
	@for f in $(SAMPLES); do ./filmdev develop $$f --stock velvia --half -q -o /dev/null >/dev/null || exit 1; done
	@./filmdev ui out >/dev/null
	@echo "clean under ASan + UBSan"
	@echo
	@echo "--- restoring the release build ---"
	@$(MAKE) -s -C host clean && $(MAKE) -s -C host

SDK := $(shell for s in MacOSX26.5.sdk MacOSX26.sdk MacOSX15.sdk; do \
                 p=/Library/Developer/CommandLineTools/SDKs/$$s; \
                 if [ -d $$p ]; then echo -isysroot $$p; break; fi; done)

# The camera's code on the desktop: the module's vsnprintf against the
# system's, then chdk/soup_dev.c developing real G10/G12 JPEGs with short
# reads and a 1 MB heap, each compared byte for byte against an in-RAM
# develop. Put camera JPEGs in samples/powershot/.
#
# -iquote, not -I: chdk/ also holds the CHDK build's stdint.h, which must
# not stand in for the system's here. The module's vsnprintf is renamed so
# the test can hold it up against the real one.
PS_SAMPLES := $(wildcard samples/powershot/*.JPG)
CHDKTEST_SRC = tools/chdktest.c chdk/soup_dev.c engine/se.c engine/wb.c \
	       engine/lut.c engine/stock.c engine/grade.c engine/jpeg_enc.c \
	       engine/jpeg_dec.c engine/look.c

chdktest:
	@mkdir -p out
	@cc -std=c99 -O2 -DSE_HOSTED $(SDK) -Dvsnprintf=se_vsnprintf \
	   -c -o out/se_vsnprintf.o chdk/vsnprintf.c
	@cc -std=c99 -O2 -g -Wall -Wextra -Wno-unused-parameter \
	   -Wno-missing-field-initializers -DSE_HOSTED $(SDK) -Iengine -iquote chdk \
	   -o out/chdktest $(CHDKTEST_SRC) out/se_vsnprintf.o -lm
	@./out/chdktest
	@test -n "$(PS_SAMPLES)" || { echo "no camera JPEGs in samples/powershot/"; exit 1; }
	@for f in $(PS_SAMPLES); do for s in 1 2; do for st in velvia acros portra; do \
	   echo "--- `basename $$f` $$st 1/$$s"; \
	   ./out/chdktest $$f stocks $$st out/chdk_$$st.jpg $$s 1024 2>/dev/null \
	     | grep -E 'develop:|PASS|FAIL' || exit 1; \
	 done; done; done

asan:
	@$(MAKE) -s -C host asan

# Confirm the engine still builds freestanding for the camera's core:
# ARM946E-S, in DIGIC 4 as in the DIGIC II it was first written for.
arm:
	@mkdir -p out/arm
	@for f in engine/*.c; do \
	  arm-none-eabi-gcc -c -std=c99 -Os -Wall -Wextra -Wno-unused-parameter \
	    -Wno-missing-field-initializers -mcpu=arm946e-s -marm -ffreestanding \
	    -fno-builtin -o out/arm/`basename $$f .c`.o $$f || exit 1; \
	done
	@arm-none-eabi-size -t out/arm/*.o | tail -1

contact: all
	@./filmdev contact $(firstword $(SAMPLES)) out/contact.jpg

clean:
	@$(MAKE) -s -C host clean
	@rm -rf out
