# Retro68 build of QemuVGADriver (qemu_vga.ndrv).
#
# The original build is the CodeWarrior project QemuVGADriver.mcp.xml; this
# Makefile reproduces its release target ("qemu_vga.ndrv": SharedLibrary,
# type 'ndrv', creator 'PSSD', main symbol DoDriverIO, exports from
# VideoDriver.exp) with the Retro68 PowerPC toolchain and Apple's Universal
# Interfaces 3.x (the open-source Multiversal Interfaces lack Video.h and
# VideoServices.h).
#
# Build:   PATH=/path/to/Retro68-build/toolchain/bin:$PATH make
# CI:      .github/workflows/build-ndrv.yml (ghcr.io/autc04/retro68 container)
#
# The link recipe (ndrv.lds, -bE:, --gc-keep-exported, MakePEF) follows
# elliotnunn/classicvirtio's Makefile (MIT).
#
# GCC 14+ turns int-conversion and return-mismatch into errors; the
# CodeWarrior-era sources rely on both (InterruptServiceIDType set from a
# pointer, a bare 'return;'), so keep them as warnings.

CC      = powerpc-apple-macos-gcc
MAKEPEF = MakePEF

OUT     = build
SRCS    = src/DriverDoDriverIO.c \
          src/DriverGestaltHandler.c \
          src/DriverQDCalls.c \
          src/QemuEdid.c \
          src/QemuVga.c \
          ../shared/MacDriverUtils.c

CFLAGS  = -Os -ffunction-sections -fdata-sections \
          -Isrc -I../shared \
          -Wall -Wno-multichar -Wno-pointer-sign \
          -Wno-error=int-conversion -Wno-error=return-mismatch \
          $(EXTRA_CFLAGS)

LIBS    = -lDriverServicesLib -lNameRegistryLib -lPCILib -lVideoServicesLib \
          -lDriverLoaderLib -lInterfaceLibForDriverServices

all: layoutcheck $(OUT)/qemu_vga.ndrv

# Fail early if the Video.h records do not get the mac68k layout.
layoutcheck: layoutcheck.c
	$(CC) -fsyntax-only $(CFLAGS) layoutcheck.c

$(OUT)/qemu_vga.so: $(SRCS) src/*.h ../shared/*.h ndrv.lds src/VideoDriver.exp
	mkdir -p $(OUT)
	$(CC) -o $@ $(CFLAGS) \
		-T ndrv.lds -Wl,-bE:src/VideoDriver.exp -Wl,--gc-sections -Wl,--gc-keep-exported \
		$(SRCS) $(LIBS)

$(OUT)/qemu_vga.ndrv: $(OUT)/qemu_vga.so
	$(MAKEPEF) -o $@ $<

clean:
	rm -rf $(OUT)

.PHONY: all clean layoutcheck
