/* PVMAC v2 ring transport; protocol proven by GLHello on Mac OS 9. */
#include <MacTypes.h>
#include <Files.h>
#include <Devices.h>
#include <Quickdraw.h>
#include <Gestalt.h>
#include <Memory.h>
#include <string.h>
#include "dg_transport.h"
#include "dg_client.h"

/* InterfaceLib 7.5 exports (System 7.5+); Retro68's Multiversal headers do not declare them. */
extern OSErr NewGestaltValue(OSType selector, long newValue);
extern OSErr ReplaceGestaltValue(OSType selector, long replacementValue);

PvmacInfo gPv;
static int gOpen;			/* 0 never tried, 1 ok, -1 failed */
static struct PvmacShared *gSh;		/* ring position etc., shared by all processes */
static int gAttached;			/* this process counts in gSh->users */

/* The device queues asynchronous spans; keep them moderately sized so the
 * executor overlaps with the guest. */
#define FLUSH_THRESHOLD (48u*1024u)

UInt32 PvmacRegRd(UInt32 off)
{
	UInt32 v;
	__asm__ volatile("lwbrx %0,%1,%2" : "=r"(v) : "b"(gPv.regBase), "r"(off) : "memory");
	return v;
}

void PvmacRegWr(UInt32 off, UInt32 v)
{
	__asm__ volatile("stwbrx %0,%1,%2" : : "r"(v), "b"(gPv.regBase), "r"(off) : "memory");
	__asm__ volatile("eieio; sync" : : : "memory");
}

struct PvmacShared *PvmacSharedBlock(void) { return gSh; }

/* Find the shared block (Gestalt 'pvGL') or create it. The first process (and
 * the first after every process left) resets the device's stream: the host
 * rejects a second DG_HELLO on one stream, so the stream is started afresh
 * only when nobody else is using it. Before the ring read. */
static int AttachShared(void)
{
	long v = 0;
	OSErr err;

	if (!gSh) {
		if (Gestalt(PV_GESTALT_SELECTOR, &v) == noErr && v)
			gSh = (struct PvmacShared *)v;
		else {
			gSh = (struct PvmacShared *)NewPtrSysClear(sizeof *gSh);
			if (!gSh) {
				DGLog("PVMAC: no system heap for the shared block");
				return -1;
			}
			gSh->magic = PV_SHARED_MAGIC;
			gSh->version = PV_SHARED_VERSION;
			err = NewGestaltValue(PV_GESTALT_SELECTOR, (long)gSh);
			if (err)
				err = ReplaceGestaltValue(PV_GESTALT_SELECTOR, (long)gSh);
			if (err) {
				DGLog("PVMAC: Gestalt 'pvGL' failed: %d", (int)err);
				DisposePtr((Ptr)gSh);
				gSh = NULL;
				return -1;
			}
		}
		if (gSh->magic != PV_SHARED_MAGIC || gSh->version != PV_SHARED_VERSION) {
			DGLog("PVMAC: shared block of another OpenGLLibrary version (%lx v%lu); restart the Mac",
				(unsigned long)gSh->magic, (unsigned long)gSh->version);
			gSh = NULL;
			return -1;
		}
	}
	if (!gSh->ready) {
		PvmacRegWr(PV_REG_STREAM_RESET, 1);
		gSh->head = PvmacRegRd(PV_REG_RING_HEAD);
		gSh->pending = (gSh->head + gPv.ringSize - PvmacRegRd(PV_REG_RING_TAIL)) % gPv.ringSize;
		gSh->hello = 0;
		gSh->users = 0;
		gSh->ownerValid = 0;
		gSh->ready = 1;
		DGLog("PVMAC: stream reset (first process)");
	}
	if (!gAttached) {
		gAttached = 1;
		gSh->users++;
		DGVLog("PVMAC: attached, %lu process(es)", (unsigned long)gSh->users);
	}
	return 0;
}

void PvmacDetach(void)
{
	if (!gAttached || !gSh)
		return;
	if (gOpen > 0)
		PvmacFlush();
	gAttached = 0;
	if (gSh->users)
		gSh->users--;
	if (!gSh->users)
		gSh->ready = 0;		/* the next process starts a fresh stream */
}

int PvmacOpen(void)
{
	ParamBlockRec pb;
	GDHandle gd;
	PvmacInfo *ip = &gPv;
	OSErr err;

	if (gOpen)
		return gOpen > 0 ? 0 : -1;
	gOpen = -1;
	gd = GetMainDevice();
	memset(&pb, 0, sizeof pb);
	pb.cntrlParam.ioCRefNum = (**gd).gdRefNum;
	pb.cntrlParam.csCode = PVMAC_CS_GET_INFO;
	memcpy(pb.cntrlParam.csParam, &ip, sizeof ip);
	err = PBStatusSync(&pb);
	if (err) {
		DGLog("PVMAC: csCode 0x8300 failed: %d (not the PVMAC qemu_vga.ndrv?)", (int)err);
		return -1;
	}
	if (!(gPv.caps & PVMAC_CAP_GL_STREAM) || !gPv.regBase || !gPv.ringSize) {
		DGLog("PVMAC: no GL stream (caps %lx): needs mac-accel=on and a v2 device", gPv.caps);
		return -1;
	}
	if (AttachShared())
		return -1;
	DGLog("PVMAC v%lu caps %lx ring %lu reply %lu", gPv.version, gPv.caps, gPv.ringSize, gPv.replySize);
	gOpen = 1;
	return 0;
}

UInt32 PvmacPending(void) { return gSh->pending; }

const UInt8 *PvmacReplyArea(void) { return (const UInt8 *)(gPv.vramBase + gPv.replyOffset); }

/* Several processes: bytes another process left queued are complete messages
 * (each call appends whole messages); hand them over before appending our own
 * so every process' messages reach the host in the order they were issued. */
static void Claim(void)
{
	ProcessSerialNumber me;
	Boolean same = false;

	if (gSh->users < 2)
		return;
	GetCurrentProcess(&me);
	if (gSh->pending && gSh->ownerValid) {
		SameProcess(&gSh->owner, &me, &same);
		if (!same)
			PvmacFlush();
	}
	gSh->owner = me;
	gSh->ownerValid = 1;
}

/* The device copies every flushed span into a byte stream to the executor,
 * so a message larger than the ring is sent in ring-sized pieces
 * (PvmacAppend flushes between them): a 512x512 RGBA texture is 1 MB plus
 * its header, more than the 1 MB ring (Quake III menu background). */
int PvmacReserve(UInt32 n)
{
	Claim();
	if (n > gPv.ringSize - 1 - gSh->pending)
		PvmacFlush();			/* the device queues the span: ring is free again */
	return 0;
}

void PvmacAppend(const void *p, UInt32 n)
{
	UInt8 *ring = (UInt8 *)(gPv.vramBase + gPv.ringOffset);
	const UInt8 *src = (const UInt8 *)p;

	while (n) {
		UInt32 room = gPv.ringSize - 1 - gSh->pending, k, first;

		if (!room) {
			PvmacFlush();
			room = gPv.ringSize - 1;
		}
		k = n < room ? n : room;
		first = gPv.ringSize - gSh->head < k ? gPv.ringSize - gSh->head : k;
		memcpy(ring + gSh->head, src, first);
		memcpy(ring, src + first, k - first);
		gSh->head = (gSh->head + k) % gPv.ringSize;
		gSh->pending += k;
		src += k;
		n -= k;
	}
}

void PvmacFlush(void)
{
	if (!gSh->pending)
		return;
	__asm__ volatile("sync" : : : "memory");
	PvmacRegWr(PV_REG_RING_HEAD, gSh->head);
	PvmacRegWr(PV_REG_REPLY_EXPECT, 0);
	PvmacRegWr(PV_REG_DOORBELL, 1);
	gSh->pending = 0;
}

int PvmacDoorbell(UInt32 expect)
{
	__asm__ volatile("sync" : : : "memory");
	PvmacRegWr(PV_REG_RING_HEAD, gSh->head);
	PvmacRegWr(PV_REG_REPLY_EXPECT, expect);
	PvmacRegWr(PV_REG_DOORBELL, 1);
	gSh->pending = 0;
	if (expect && PvmacRegRd(PV_REG_REPLY_LEN) != expect) {
		DGLog("PVMAC: reply short: wanted %lu got %lu status %lx", expect,
			PvmacRegRd(PV_REG_REPLY_LEN), PvmacRegRd(PV_REG_GL_STATUS));
		return -1;
	}
	return 0;
}

/* Called by the generated code after glFlush: queue the work now. */
void DGFlushTransport(struct DGContext *context)
{
	(void)context;
	if (gOpen > 0) {
		DGCompactFlush();
		PvmacFlush();
	}
}

/* After a no-reply message: keep spans bounded. */
void PvmacMaybeFlush(void)
{
	if (gSh->pending >= FLUSH_THRESHOLD)
		PvmacFlush();
}
