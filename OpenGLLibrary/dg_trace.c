/* Diagnostic frame trace (off unless requested).
 *
 * Enabled only when a file named "OpenGLLibrary.trace" already exists in the
 * default folder (the application's) or the System Folder at the first
 * aglSwapBuffers. The library
 * then keeps the messages of the last complete frame (aglSwapBuffers to
 * aglSwapBuffers) and writes them into that file when the context is destroyed
 * (the application quits: GLQuake `quit`), or at frame TRACE_DUMP_FRAME,
 * whichever comes first (apps that quit without destroying their context).
 *
 * File: "DGT2" magic, u32 count, u32 dropped, then count records of
 * TRACE_RECORD bytes, all big-endian as the guest stores them:
 *   u32 op, u32 payload bytes, u64 args[0..7] (host values before the LE
 *   header encoding), first 12 payload bytes, u32 host context id.
 *   (Older description follows:)
 *   header encoding), first 16 payload bytes (wire = little-endian order).
 */
#include <MacTypes.h>
#include <Files.h>
#include <MacMemory.h>
#include <string.h>
#include "dg_client.h"

#define TRACE_MAX 60000u
#define TRACE_RECORD 88u
#define TRACE_DUMP_FRAME 4u

enum { UNKNOWN, OFF, ON, DONE };
static int gState = UNKNOWN;
static Handle gBuf[2];			/* [gCur] being filled, [!gCur] last frame */
static int gCur;
static UInt32 gCount[2], gDropped[2];
static FSSpec gSpec;
static UInt32 gFrames;

void DGTraceDump(void);
static void TraceInit(void);

void DGTraceMessage(const struct DGPacket *p)
{
	unsigned char *r;
	UInt32 n;

	if (gState == UNKNOWN)
		TraceInit();		/* first mode must see setup before any swap */
	if (gState != ON)
		return;
	if (gCount[gCur] >= TRACE_MAX) {
		gDropped[gCur]++;
		return;
	}
	r = (unsigned char *)*gBuf[gCur] + gCount[gCur] * TRACE_RECORD;
	memcpy(r, &p->message.op, 4);
	memcpy(r + 4, &p->message.bytes, 4);
	memcpy(r + 8, p->message.args, 64);
	memset(r + 72, 0, 16);
	n = p->message.bytes < 12 ? p->message.bytes : 12;
	if (p->data && n)
		memcpy(r + 72, p->data, n);
	memcpy(r + 84, &p->message.context, 4);	/* host context id */
	gCount[gCur]++;
}

static Handle Alloc(void)
{
	OSErr err;
	Handle h = TempNewHandle(TRACE_MAX * TRACE_RECORD, &err);

	if (!h || err)
		h = NewHandle(TRACE_MAX * TRACE_RECORD);
	if (h)
		HLock(h);
	return h;
}

/* Looks for the control file once. A non-empty file selects "first" mode:
 * record everything from the library's first message up to the first
 * aglSwapBuffers (context setup included) and dump that. */
static int gFirstMode;

static void TraceInit(void)
{
	short ref;
	long len = 0;

	gState = OFF;
	/* the application's folder, else the System Folder (games often
	 * change the default folder) */
	if (FSMakeFSSpec(0, 0, "\pOpenGLLibrary.trace", &gSpec)) {
		short vRef;
		long dirID;

		if (FindFolder((short)0x8000 /* kOnSystemDisk */, kSystemFolderType, 0, &vRef, &dirID) ||
		    FSMakeFSSpec(vRef, dirID, "\pOpenGLLibrary.trace", &gSpec))
			return;
	}
	if (!FSpOpenDF(&gSpec, fsRdPerm, &ref)) {
		GetEOF(ref, &len);
		FSClose(ref);
	}
	gFirstMode = len > 0;
	gBuf[0] = Alloc();
	gBuf[1] = Alloc();
	if (!gBuf[0] || !gBuf[1]) {
		DGLog("trace: no memory");
		return;
	}
	gState = ON;
	DGLog(gFirstMode ? "trace: on (first frame, from library start)" : "trace: on");
}

/* Called at the start of aglSwapBuffers: a frame boundary. */
void DGTraceFrame(void)
{
	if (gState == UNKNOWN) {
		TraceInit();
		return;
	}
	if (gState == ON && gFirstMode) {
		gCur = !gCur;		/* DGTraceDump writes !gCur: the buffer filled so far */
		DGTraceDump();
		return;
	}
	if (gState != ON)
		return;
	if (++gFrames == TRACE_DUMP_FRAME) {
		DGTraceDump();		/* writes the frame that just ended */
		return;
	}
	gCur = !gCur;
	gCount[gCur] = gDropped[gCur] = 0;
}

/* Called from aglDestroyContext: write the last complete frame once. */
void DGTraceDump(void)
{
	short ref;
	long len;
	UInt32 head[3];
	int last = !gCur;

	if (gState != ON)
		return;
	gState = DONE;
	if (FSpOpenDF(&gSpec, fsRdWrPerm, &ref)) {
		DGLog("trace: cannot open OpenGLLibrary.trace");
		return;
	}
	SetEOF(ref, 0);
	head[0] = 'DGT2';
	head[1] = gCount[last];
	head[2] = gDropped[last];
	len = sizeof head;
	FSWrite(ref, &len, head);
	len = gCount[last] * TRACE_RECORD;
	FSWrite(ref, &len, *gBuf[last]);
	FSClose(ref);
	DGLog("trace: wrote %lu records (%lu dropped)", (unsigned long)gCount[last], (unsigned long)gDropped[last]);
}
