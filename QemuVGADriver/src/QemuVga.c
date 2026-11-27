#include "VideoDriverPrivate.h"
#include "VideoDriverPrototypes.h"
#include "DriverQDCalls.h"
#include "QemuVga.h"
#include <Timer.h>

/*
static struct _vMode defaultVModes[] =  {
	{ 640, 480 },
	{ 800, 600 },
	{ 1024, 768 },
	{ 1280, 1024 },
	{ 1600, 1200 },
	{ 1920, 1080 },
	{ 1920, 1200 },
	{ 0, 0 }
};
*/
static void VgaWriteB(UInt16 port, UInt8 val)
{
	UInt8 *ptr;
	
	ptr = (UInt8 *)((UInt32)GLOBAL.boardRegAddress + port + 0x400 - 0x3c0);
	*ptr = val;
	SynchronizeIO();
}

static UInt8 VgaReadB(UInt16 port)
{
	UInt8 *ptr, val;
	
	ptr = (UInt8 *)((UInt32)GLOBAL.boardRegAddress + port + 0x400 - 0x3c0);
	val = *ptr;
	SynchronizeIO();
	return val;
}

static void DispiWriteW(UInt16 reg, UInt16 val)
{
	UInt16 *ptr;
	
	ptr = (UInt16 *)((UInt32)GLOBAL.boardRegAddress + (reg << 1) + 0x500);
	*ptr = EndianSwap16Bit(val);
	SynchronizeIO();
}

static UInt16 DispiReadW(UInt16 reg)
{
	UInt16 *ptr, val;
	
	ptr = (UInt16 *)((UInt32)GLOBAL.boardRegAddress + (reg << 1) + 0x500);
	val = EndianSwap16Bit(*ptr);
	SynchronizeIO();
	return val;
}

static void ExtWriteL(UInt16 reg, UInt32 val)
{
	UInt32 *ptr;
	
	ptr = (UInt32 *)((UInt32)GLOBAL.boardRegAddress + (reg << 2) + 0x600);
	*ptr = EndianSwap32Bit(val);
	SynchronizeIO();
}

static UInt32 ExtReadL(UInt32 reg)
{
	UInt32 *ptr, val;
	
	ptr = (UInt32 *)((UInt32)GLOBAL.boardRegAddress + (reg << 2) + 0x600);
	val = EndianSwap32Bit(*ptr);
	SynchronizeIO();
	return val;
}

static UInt32 PvmacReadL(UInt32 reg)
{
	volatile UInt32 *ptr;
	UInt32 val;

	ptr = (volatile UInt32 *)((UInt32)GLOBAL.boardRegAddress + PVMAC_BASE + reg);
	val = EndianSwap32Bit(*ptr);
	SynchronizeIO();
	return val;
}

static void PvmacWriteL(UInt32 reg, UInt32 val)
{
	volatile UInt32 *ptr;

	ptr = (volatile UInt32 *)((UInt32)GLOBAL.boardRegAddress + PVMAC_BASE + reg);
	*ptr = EndianSwap32Bit(val);
	SynchronizeIO();
}

static OSStatus VBLTimerProc(void *p1, void *p2);

#ifndef USE_DSL_TIMER
static TMTask gLegacyTimer;

static pascal void legacyTimerCB(TMTaskPtr *inTask)
{
	VBLTimerProc(NULL, NULL);
}

static const RoutineDescriptor	gLegacyTimerDesc	= BUILD_ROUTINE_DESCRIPTOR(uppTimerProcInfo, legacyTimerCB);
static const TimerUPP			gLegacyTimerProc	= (TimerUPP) &gLegacyTimerDesc;
static int gTimerInstalled;

static OSStatus ScheduleVBLTimer(void)
{
	if (!gTimerInstalled) {
		BlockZero(&gLegacyTimer, sizeof(gLegacyTimer));
		gLegacyTimer.tmAddr = gLegacyTimerProc;
		gLegacyTimer.qLink = (QElemPtr)'eada';
		InsXTime((QElemPtr)&gLegacyTimer);
		gTimerInstalled = true;
	}
	PrimeTime((QElemPtr)&gLegacyTimer, TIMER_DURATION);
	return noErr;
}

#else

static OSStatus ScheduleVBLTimer(void)
{
	AbsoluteTime target = AddDurationToAbsolute(TIMER_DURATION, UpTime());
	return SetInterruptTimer(&target, VBLTimerProc, NULL, &GLOBAL.VBLTimerID);
}

#endif

static OSStatus VBLTimerProc(void *p1, void *p2)
{
	static UInt32 VBcnt;

	GLOBAL.inInterrupt = 1;

	/* This can be called before the service is ready */
	if (GLOBAL.qdVBLInterrupt && GLOBAL.qdInterruptsEnable)
		VSLDoInterruptService(GLOBAL.qdVBLInterrupt);
	
	/* Reschedule */
	ScheduleVBLTimer();

	GLOBAL.inInterrupt = 0;
	return noErr;
}

#ifdef USE_PCI_IRQ
static InterruptMemberNumber PCIInterruptHandler(InterruptSetMember ISTmember,
												 void *refCon, UInt32 theIntCount)
{
	UInt32 reg;
	
	reg = ExtReadL(2);
	if (!(reg & 1))
		return kIsrIsNotComplete;
	if (GLOBAL.qdVBLInterrupt && GLOBAL.qdInterruptsEnable)
		VSLDoInterruptService(GLOBAL.qdVBLInterrupt);
	ExtWriteL(2, 3);
	return kIsrIsComplete;
}
#endif


OSStatus QemuVga_Init(void)
{
	UInt16 id;
	UInt32 mem, width, height, depth, i;

	lprintf("First MMIO read...\n");
	id = DispiReadW(VBE_DISPI_INDEX_ID);
	mem = DispiReadW(VBE_DISPI_INDEX_VIDEO_MEMORY_64K);
	mem <<= 16;
	lprintf("DISPI_ID=%04x VMEM=%d Mb\n", id, mem >> 20);
	if ((id & 0xfff0) != VBE_DISPI_ID0) {
		lprintf("Unsupported ID !\n");
		return controlErr;
	}
	if (mem > GLOBAL.boardFBMappedSize)
		mem = GLOBAL.boardFBMappedSize;
	GLOBAL.vramSize = mem;

	/* The PVMAC cursor slot lives at the top of VRAM: keep framebuffer
	 * pages (and the mode list, which is sized from boardFBMappedSize)
	 * below it. Must run before the mode list is built.
	 */
	if (QemuVga_ProbePvmac() && !GraphicsCoreAllocHardwareCursor()) {
		lprintf("PVMAC: no memory for the cursor, disabled\n");
		GLOBAL.hasHWCursor = false;
	}
	if (GLOBAL.hasHWCursor) {
		if (GLOBAL.boardFBMappedSize > GLOBAL.cursorOffset)
			GLOBAL.boardFBMappedSize = GLOBAL.cursorOffset;
		if (GLOBAL.vramSize > GLOBAL.cursorOffset)
			GLOBAL.vramSize = GLOBAL.cursorOffset;
	}
	
	// XXX Add endian control regs

	width = DispiReadW(VBE_DISPI_INDEX_XRES);
	height = DispiReadW(VBE_DISPI_INDEX_YRES);
	depth = DispiReadW(VBE_DISPI_INDEX_BPP);
	lprintf("Current setting: %dx%dx%d\n", width, height, depth);

	GLOBAL.depth = GLOBAL.bootDepth = depth;
	GLOBAL.numModes = QemuVga_InitModes();
	lprintf("Number of modes: %d\n", GLOBAL.numModes);

	/* Boot in the resolution QEMU was started with (-g), if we have it */
	i = QemuVga_FindMode(width, height);
	if (i == kQvNoMode) {
		i = QemuVga_FindMode(1024, 768);
		if (i == kQvNoMode)
			i = QemuVga_NextMode(kQvNoMode);
		lprintf("Not found in list ! using mode ID %d.\n", i + 1);
	} else {
		lprintf("Using mode ID %d\n", i + 1);
	}
	GLOBAL.bootMode = i;

	QemuVga_SetMode(GLOBAL.bootMode, depth, 0);

#ifdef USE_PCI_IRQ
	if (SetupPCIInterrupt(&GLOBAL.deviceEntry, &GLOBAL.irqInfo,
					   	  PCIInterruptHandler, NULL) == noErr)
		GLOBAL.hasPCIInterrupt = true;
	else
#else
	GLOBAL.hasPCIInterrupt = false;
#endif
	return noErr;
}

OSStatus QemuVga_Open(void)
{
	lprintf("QemuVga v1.00\n");

	GLOBAL.isOpen = true;

	if (GLOBAL.hasPCIInterrupt) {
		QemuVga_EnableInterrupts();
		lprintf("VBL registered using PCI interrupts\n");	
	} else {
		/* Schedule the timer now if timers are supported. They aren't on OS X
		 * in which case we must not create the VSL service, otherwise OS X will expect
		 * a VBL and fail to update the cursor when not getting one.
	 	*/
		lprintf("Testing using timer to simulate VBL..\n");	
		GLOBAL.hasTimer = (ScheduleVBLTimer() == noErr);
		GLOBAL.qdInterruptsEnable = GLOBAL.hasTimer;

		if (GLOBAL.hasTimer)
			lprintf("Using timer to simulate VBL.\n");	
		else
			lprintf("No timer service (OS X ?), VBL not registered.\n");	

	}

	/* Create VBL if we have a PCI interrupt or timer works */
	if (GLOBAL.hasPCIInterrupt || GLOBAL.hasTimer)
		VSLNewInterruptService(&GLOBAL.deviceEntry, kVBLInterruptServiceType, &GLOBAL.qdVBLInterrupt);
	
	return noErr;
}

OSStatus QemuVga_Close(void)
{
	lprintf("Closing Driver...\n");

	GLOBAL.isOpen = false;
	
	QemuVga_DisableInterrupts();
	if (GLOBAL.qdVBLInterrupt)
		VSLDisposeInterruptService( GLOBAL.qdVBLInterrupt );
	GLOBAL.qdVBLInterrupt = NULL;

	return noErr;
}

OSStatus QemuVga_Exit(void)
{
	QemuVga_Close();

	return noErr;
}

void QemuVga_EnableInterrupts(void)
{
	GLOBAL.qdInterruptsEnable = true;
	if (GLOBAL.hasTimer)
		ScheduleVBLTimer();
	else if (GLOBAL.hasPCIInterrupt) {
		GLOBAL.irqInfo.enableFunction(GLOBAL.irqInfo.interruptSetMember, GLOBAL.irqInfo.refCon);
		ExtWriteL(2, 3);
	}
}

void QemuVga_DisableInterrupts(void)
{
	AbsoluteTime remaining;

	GLOBAL.qdInterruptsEnable = false;
	if (GLOBAL.hasTimer)
		CancelTimer(GLOBAL.VBLTimerID, &remaining);
	else if (GLOBAL.hasPCIInterrupt) {
		ExtWriteL(2, 1);
		GLOBAL.irqInfo.disableFunction(GLOBAL.irqInfo.interruptSetMember, GLOBAL.irqInfo.refCon);
	}
}

OSStatus QemuVga_SetColorEntry(UInt32 index, RGBColor *color)
{
	//lprintf("SetColorEntry %d, %x %x %x\n", index, color->red, color->green, color->blue);
	VgaWriteB(0x3c8, index);
	VgaWriteB(0x3c9, color->red >> 8);
	VgaWriteB(0x3c9, color->green >> 8);
	VgaWriteB(0x3c9, color->blue >> 8);
	return noErr;
}

OSStatus QemuVga_GetColorEntry(UInt32 index, RGBColor *color)
{
	UInt32 r,g,b;
	
	VgaWriteB(0x3c7, index);
	r = VgaReadB(0x3c9);
	g = VgaReadB(0x3c9);
	b = VgaReadB(0x3c9);
	color->red = (r << 8) | r;
	color->green = (g << 8) | g;
	color->blue = (b << 8) | b;

	return noErr;
}

OSStatus QemuVga_GetModeInfo(UInt32 index, UInt32 *width, UInt32 *height)
{
	if (!QemuVga_ModeAvailable(index))
		return paramErr;
	if (width)
		*width = qvModes[index].width;
	if (height)
		*height = qvModes[index].height;
	return noErr;
}

OSStatus QemuVga_GetModePages(UInt32 index, UInt32 depth,
							  UInt32 *pageSize, UInt32 *pageCount)
{
	UInt32 width, height, pBytes;

	if (!QemuVga_ModeAvailable(index))
		return paramErr;
	width = qvModes[index].width;
	height = qvModes[index].height;
	pBytes = width * ((depth + 7) / 8) * height;
	if (pageSize)
		*pageSize = pBytes;
	if (pageCount) {
		if (pBytes <= (GLOBAL.boardFBMappedSize / 2))
			*pageCount = 2;
		else
			*pageCount = 1;
	}
	return noErr;
}

OSStatus QemuVga_SetMode(UInt32 mode, UInt32 depth, UInt32 page)
{
	UInt32 width, height;
	UInt32 pageSize, numPages;

	if (!QemuVga_ModeAvailable(mode))
		return paramErr;
	
	width = qvModes[mode].width;
	height = qvModes[mode].height;
	QemuVga_GetModePages(mode, depth, &pageSize, &numPages);
	lprintf("Set Mode: %dx%dx%d has %d pages\n", width, height, depth, numPages);
	if (page >= numPages)
		return paramErr;

	DispiWriteW(VBE_DISPI_INDEX_ENABLE,      0);
	DispiWriteW(VBE_DISPI_INDEX_BPP,         depth);
	DispiWriteW(VBE_DISPI_INDEX_XRES,        width);
	DispiWriteW(VBE_DISPI_INDEX_YRES,        height);
	DispiWriteW(VBE_DISPI_INDEX_BANK,        0);
	DispiWriteW(VBE_DISPI_INDEX_VIRT_WIDTH,  width);
	DispiWriteW(VBE_DISPI_INDEX_VIRT_HEIGHT, height * numPages);
	DispiWriteW(VBE_DISPI_INDEX_X_OFFSET,    0);
	DispiWriteW(VBE_DISPI_INDEX_Y_OFFSET,    height * page);
	DispiWriteW(VBE_DISPI_INDEX_ENABLE,      VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED | VBE_DISPI_8BIT_DAC);	
	GLOBAL.curMode = mode;
	GLOBAL.depth = depth;
	GLOBAL.curPage = page;
	GLOBAL.curBaseAddress = FB_START + page * pageSize;
	
	return noErr;
}

OSStatus QemuVga_Blank(Boolean blank)
{
	/* We use the AR Index VGA register which is a flip flop
	 * so we need to ensure we write twice. We use a non-existing
	 * index so that the second write is dropped.
	 */
	if (blank) {
		VgaWriteB(0x3c0, 0x1f);
		VgaWriteB(0x3c0, 0x1f);
	} else {
		VgaWriteB(0x3c0, 0x3f);
		VgaWriteB(0x3c0, 0x3f);
	}
	GLOBAL.blanked = blank;
	return noErr;
}

/************************ PVMAC hardware cursor ****************************/

Boolean QemuVga_ProbePvmac(void)
{
	UInt32 offset;

	GLOBAL.hasHWCursor = false;
	if (GLOBAL.boardRegMappedSize < PVMAC_BASE + PVMAC_REG_CUR_OFFSET + 4)
		return false;
	if (PvmacReadL(PVMAC_REG_ID) != PVMAC_ID_VALUE)
		return false;
	if (!(PvmacReadL(PVMAC_REG_CAPS) & PVMAC_CAP_HWCURSOR))
		return false;
	offset = PvmacReadL(PVMAC_REG_CUR_OFFSET);
	if (offset < 0x10000 || offset + 0x4000 > GLOBAL.boardFBMappedSize)
		return false;
	GLOBAL.cursorOffset = offset;
	GLOBAL.hasHWCursor = true;
	PvmacWriteL(PVMAC_REG_CUR_CTRL, 0);
	lprintf("PVMAC v%d: hardware cursor, slot at %08lX\n",
			PvmacReadL(PVMAC_REG_VERSION), offset);
	return true;
}

UInt32 *QemuVga_CursorSlot(void)
{
	return (UInt32 *)(FB_START + GLOBAL.cursorOffset);
}

/* The 32x32 ARGB image must already be in the cursor slot. */
Boolean QemuVga_CursorDefine(UInt32 hotX, UInt32 hotY)
{
	SynchronizeIO();	/* image stores before the doorbell */
	PvmacWriteL(PVMAC_REG_CUR_SIZE, PVMAC_CURSOR_EDGE);
	PvmacWriteL(PVMAC_REG_CUR_HOT, (hotY << 16) | (hotX & 0xffff));
	PvmacWriteL(PVMAC_REG_CUR_DEFINE, 1);
	return (PvmacReadL(PVMAC_REG_STATUS) & PVMAC_STATUS_ERROR) == 0;
}

/* x, y: hotspot position on screen */
void QemuVga_CursorMove(SInt32 x, SInt32 y, Boolean visible)
{
	PvmacWriteL(PVMAC_REG_CUR_POS, ((UInt32)(y & 0xffff) << 16) | (x & 0xffff));
	PvmacWriteL(PVMAC_REG_CUR_CTRL, visible ? 1 : 0);
}
