#include <Video.h>
#include "VideoDriverPrivate.h"
#include "logger.h"
#include "QemuVga.h"

/*
 * Display modes.
 *
 * Mac OS saves the selected resolution as its Display Manager mode ID, so a
 * mode must keep its ID forever. The ID of a mode is its index in this table
 * plus one. Never reorder or remove entries; add new modes at the end.
 *
 * IDs 2 to 21 are the list that earlier versions of this driver built from
 * QEMU's EDID with 16 MB of VRAM (ID 1 was a bogus 0x0 entry produced by
 * reserved EDID bits), so settings saved with those versions keep their
 * resolution. ID 1 is never offered.
 *
 * The list no longer depends on the EDID: QEMU's virtual display has no
 * timing limits, and the EDID mode list changed between QEMU versions, which
 * shifted the saved IDs. Every mode that fits in VRAM at 32 bpp is offered.
 */
const struct _vMode qvModes[] = {
	{    0,    0 },	/*  1: reserved, never offered */
	{  640,  480 },	/*  2 */
	{  800,  600 },	/*  3 */
	{  832,  624 },	/*  4 */
	{ 1024,  768 },	/*  5 */
	{ 1152,  864 },	/*  6 */
	{ 1152,  870 },	/*  7 */
	{ 1280,  768 },	/*  8 */
	{ 1280,  960 },	/*  9 */
	{ 1280, 1024 },	/* 10 */
	{ 1360,  768 },	/* 11 */
	{ 1440,  900 },	/* 12 */
	{ 1440, 1050 },	/* 13 */
	{ 1600, 1200 },	/* 14 */
	{ 1680, 1050 },	/* 15 */
	{ 1792, 1344 },	/* 16 */
	{ 1856, 1392 },	/* 17 */
	{ 1920, 1080 },	/* 18 */
	{ 1920, 1200 },	/* 19 */
	{ 1920, 1440 },	/* 20 */
	{ 2560, 1080 },	/* 21 */
	{ 1152,  768 },	/* 22: PowerBook G4 15" (Titanium) */
	{ 1280,  800 },	/* 23 */
	{ 1280,  854 },	/* 24: PowerBook G4 15" (Aluminum) */
	{ 1440,  960 },	/* 25: PowerBook G4 15" (high resolution) */
	{ 1600,  900 },	/* 26 */
	{ 1600, 1024 },	/* 27: Apple Studio Display 17" (CRT) */
	{ 2560, 1440 },	/* 28 */
	{ 2560, 1600 },	/* 29: Apple Cinema HD Display 30" */
	{ 3840, 2160 },	/* 30 */
};

#define QV_NUM_MODES (sizeof(qvModes) / sizeof(qvModes[0]))

UInt32 QemuVga_NumModeIDs(void)
{
	return QV_NUM_MODES;
}

/* index = mode ID - 1 */
Boolean QemuVga_ModeAvailable(UInt32 index)
{
	const struct _vMode *m;

	if (index >= QV_NUM_MODES)
		return false;
	m = &qvModes[index];
	if (m->width == 0 || m->height == 0)
		return false;
	/* Must fit in VRAM at the deepest depth (below the cursor slot) */
	return (UInt32)m->width * m->height * 4 <= GLOBAL.boardFBMappedSize;
}

static Boolean ModeLess(const struct _vMode *a, const struct _vMode *b)
{
	return a->width < b->width ||
		(a->width == b->width && a->height < b->height);
}

/*
 * Available modes are presented in size order (width, then height), as the
 * old driver did, independently of their IDs. Pass kQvNoMode to get the
 * first one. Returns kQvNoMode after the last one.
 */
UInt32 QemuVga_NextMode(UInt32 prevIndex)
{
	const struct _vMode *prev = NULL;
	UInt32 i, best = kQvNoMode;

	if (prevIndex != kQvNoMode) {
		if (prevIndex >= QV_NUM_MODES)
			return kQvNoMode;
		prev = &qvModes[prevIndex];
	}
	for (i = 0; i < QV_NUM_MODES; i++) {
		if (!QemuVga_ModeAvailable(i))
			continue;
		if (prev && !ModeLess(prev, &qvModes[i]))
			continue;
		if (best == kQvNoMode || ModeLess(&qvModes[i], &qvModes[best]))
			best = i;
	}
	return best;
}

UInt32 QemuVga_FindMode(UInt32 width, UInt32 height)
{
	UInt32 i;

	for (i = 0; i < QV_NUM_MODES; i++) {
		if (qvModes[i].width == width && qvModes[i].height == height &&
		    QemuVga_ModeAvailable(i))
			return i;
	}
	return kQvNoMode;
}

UInt16 QemuVga_InitModes(void)
{
	UInt32 i;
	UInt16 n = 0;

	lprintf("Available modes:\n");
	for (i = QemuVga_NextMode(kQvNoMode); i != kQvNoMode; i = QemuVga_NextMode(i)) {
		lprintf("   ID %d: %d, %d\n", i + 1, qvModes[i].width, qvModes[i].height);
		n++;
	}
	return n;
}
