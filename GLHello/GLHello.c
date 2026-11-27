/* GLHello: drives VirtualIntelMac's PassthroughOpenGL host executor through
 * the PVMAC v2 ring from Mac OS 9 (PowerPC, Retro68).
 *
 * Sequence: DG_HELLO, DG_CREATE, DG_RESIZE, glClearColor, glClear,
 * DG_READ_DRAWABLE (BGRA8), DG_DROP_DRAWABLE, DG_DESTROY; the readback is
 * shown in the window through a 32-bit GWorld.
 *
 * Wire format after VirtualIntelMac (MIT, nfzerox), passthrough-opengl-wire.h
 * at commit 2861da9: everything little-endian. glhello_gen.h carries the
 * SDK-derived DG_LAYOUT string and the clear / clear_color opcodes.
 */
#include <MacTypes.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <Events.h>
#include <Fonts.h>
#include <Menus.h>
#include <Dialogs.h>
#include <Devices.h>
#include <Files.h>
#include <CodeFragments.h>
#include <Folders.h>
#include <TextEdit.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "glhello_gen.h"

/* --- copied from QemuVGADriver/src/QemuVga.h */
#define PVMAC_CS_GET_INFO ((short)0x8300)
typedef struct PvmacInfo {
	UInt32 version, caps, regBase, vramBase;
	UInt32 ringOffset, ringSize, replyOffset, replySize;
} PvmacInfo;

#define REG_RING_HEAD    0x30
#define REG_RING_TAIL    0x34
#define REG_REPLY_LEN    0x40
#define REG_DOORBELL     0x44
#define REG_GL_STATUS    0x48
#define REG_REPLY_EXPECT 0x4c
#define CAP_GL_STREAM    2

/* --- wire */
#define DG_MAGIC      0x31474c44UL
#define DG_VERSION    8
#define DG_FEAT_MAGIC 0x584d4956UL
#define DG_FEAT_LARGE_IOSURFACE_WRITES 16
#define DG_CREATE         0x10000UL
#define DG_DESTROY        0x10001UL
#define DG_RESIZE         0x10002UL
#define DG_HELLO          0x10003UL
#define DG_DROP_DRAWABLE  0x10007UL
#define DG_READ_DRAWABLE  0x1000aUL
#define DG_REPLY       1
#define DG_OUTPUT_ONLY 2
#define MSG_BYTES      144
#define W 256
#define H 256
#define GL_COLOR_BUFFER_BIT 0x4000UL

static PvmacInfo gInfo;
static WindowPtr gWin;
static GWorldPtr gGW;
static int gLine;
static char gText[44][72];

/* --- log to the window */
static void DrawLine(int i)
{
	char p[80];
	int n = strlen(gText[i]);
	p[0] = n;
	memcpy(p + 1, gText[i], n);
	TextFont(4);			/* Monaco */
	TextSize(9);
	MoveTo(8, 12 + 12 * i);
	DrawString((ConstStr255Param)p);
}

static void Log(const char *fmt, ...)
{
	char buf[160];
	va_list ap;
	va_start(ap, fmt);
	vsprintf(buf, fmt, ap);
	va_end(ap);
	if (gLine >= 44)
		return;
	buf[70] = 0;
	strcpy(gText[gLine], buf);
	SetPort(gWin);
	DrawLine(gLine++);
}

/* --- PVMAC registers: 32-bit little-endian */
static UInt32 RegRd(UInt32 off)
{
	UInt32 v;
	__asm__ volatile("lwbrx %0,%1,%2" : "=r"(v) : "b"(gInfo.regBase), "r"(off) : "memory");
	return v;
}

static void RegWr(UInt32 off, UInt32 v)
{
	__asm__ volatile("stwbrx %0,%1,%2" : : "r"(v), "b"(gInfo.regBase), "r"(off) : "memory");
	__asm__ volatile("eieio; sync" : : : "memory");
}

static void Put32(UInt8 *p, UInt32 v)
{
	p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24;
}

static UInt32 Get32(const UInt8 *p)
{
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((UInt32)p[3] << 24);
}

static UInt8 *Vram(UInt32 off) { return (UInt8 *)(gInfo.vramBase + off); }

/* Kick the device: it sends ring TAIL..HEAD to the executor, then reads
 * `expect` reply bytes into the reply area. */
static int Doorbell(UInt32 expect)
{
	RegWr(REG_REPLY_EXPECT, expect);
	RegWr(REG_DOORBELL, 1);
	if (expect && RegRd(REG_REPLY_LEN) != expect) {
		Log("reply short: wanted %lu got %lu", expect, RegRd(REG_REPLY_LEN));
		return -1;
	}
	return 0;
}

static int RingPut(const UInt8 *p, UInt32 n)
{
	UInt32 size = gInfo.ringSize, head, tail, used, first;
	UInt8 *ring = Vram(gInfo.ringOffset);

	if (n >= size) {
		Log("message %lu B does not fit the %lu B ring", n, size);
		return -1;
	}
	head = RegRd(REG_RING_HEAD);
	tail = RegRd(REG_RING_TAIL);
	used = (head + size - tail) % size;
	if (n > size - 1 - used) {		/* full: let the executor drain it */
		if (Doorbell(0))
			return -1;
		head = RegRd(REG_RING_HEAD);
		tail = RegRd(REG_RING_TAIL);
		used = (head + size - tail) % size;
		if (n > size - 1 - used) {
			Log("ring stuck: head %lu tail %lu", head, tail);
			return -1;
		}
	}
	first = size - head < n ? size - head : n;
	memcpy(ring + head, p, first);
	memcpy(ring, p + first, n - first);
	__asm__ volatile("sync" : : : "memory");
	RegWr(REG_RING_HEAD, (head + n) % size);
	return 0;
}

/* Queue one DGMessage (+ body). With a reply, ring the doorbell and read
 * DGReply {bytes, error, result} (16 B) then its payload (second doorbell).
 * *payload points into the VRAM reply area. */
typedef struct { UInt32 bytes, error, resultLo, resultHi; } Reply;

static int Send(UInt32 op, UInt32 ctx, UInt32 flags, UInt32 bytes, const UInt32 *args,
				const void *body, UInt32 bodyLen, Reply *r, const UInt8 **payload)
{
	UInt8 msg[MSG_BYTES + 128];
	const UInt8 *rep;
	int i;

	if (bodyLen > 128)
		return -1;
	memset(msg, 0, sizeof msg);
	Put32(msg, op);
	Put32(msg + 4, ctx);
	Put32(msg + 8, bytes);
	Put32(msg + 12, flags);
	for (i = 0; i < 16 && args; i++)
		Put32(msg + 16 + 8 * i, args[i]);	/* args[] are 64-bit; high words 0 */
	memcpy(msg + MSG_BYTES, body, bodyLen);
	if (RingPut(msg, MSG_BYTES + bodyLen))
		return -1;
	if (!(flags & DG_REPLY))
		return 0;			/* batched until the next reply */

	if (Doorbell(16))
		return -1;
	rep = Vram(gInfo.replyOffset);
	r->bytes = Get32(rep);
	r->error = Get32(rep + 4);
	r->resultLo = Get32(rep + 8);
	r->resultHi = Get32(rep + 12);
	if (r->bytes) {				/* payload: a second doorbell, ring empty */
		if (r->bytes > gInfo.replySize) {
			Log("reply payload %lu > reply area %lu", r->bytes, gInfo.replySize);
			return -1;
		}
		if (Doorbell(r->bytes))
			return -1;
	}
	*payload = rep;
	return 0;
}

static int Step(const char *name, UInt32 op, UInt32 ctx, UInt32 flags, UInt32 bytes,
				const UInt32 *args, const void *body, UInt32 bodyLen,
				Reply *r, const UInt8 **payload)
{
	int e = Send(op, ctx, flags | DG_REPLY, bytes, args, body, bodyLen, r, payload);
	if (e)
		Log("%s: transport failed", name);
	else
		Log("%s: bytes=%lu error=%lu result=%lu", name, r->bytes, r->error, r->resultLo);
	return e || r->error;
}

static void Blit(void)
{
	Rect r;
	SetPort(gWin);
	SetRect(&r, 10, 250, 10 + W, 250 + H);
	CopyBits((BitMap *)*GetGWorldPixMap(gGW), &gWin->portBits, &gGW->portRect, &r, srcCopy, NULL);
}

static void Show(const UInt8 *px)
{
	/* The executor reads back BGRA8 (B,G,R,A in memory); a 32-bit GWorld
	 * is A,R,G,B in memory on the big-endian PPC. */
	PixMapHandle pm = GetGWorldPixMap(gGW);
	UInt8 *dst, *row;
	long rb;
	int x, y;

	LockPixels(pm);
	dst = (UInt8 *)GetPixBaseAddr(pm);
	rb = (*pm)->rowBytes & 0x3FFF;
	for (y = 0; y < H; y++) {
		row = dst + y * rb;
		for (x = 0; x < W; x++, px += 4, row += 4) {
			row[0] = 0;
			row[1] = px[2];
			row[2] = px[1];
			row[3] = px[0];
		}
	}
	UnlockPixels(pm);
	Blit();
}

/* --- CFM probe: does "OpenGLLibrary" resolve by name, and from the file? */
static void PStr(char *dst, ConstStr255Param p)
{
	memcpy(dst, p + 1, p[0]);
	dst[p[0]] = 0;
}

static void CfmProbe(void)
{
	static const char *const how[2] = { "kReferenceCFrag", "kFindCFrag" };
	Str255 en;
	char nm[256];
	CFragConnectionID conn = 0;
	Ptr mainAddr;
	Ptr addr;
	CFragSymbolClass cls;
	OSErr e;
	long i, cnt;
	FSSpec fs;
	short vref;
	long dirid;
	int k;

	for (k = 0; k < 2; k++) {
		en[0] = 0;
		conn = 0;
		mainAddr = NULL;
		e = GetSharedLibrary("\pOpenGLLibrary", kPowerPCCFragArch,
			k ? kFindCFrag : kReferenceCFrag, &conn, &mainAddr, en);
		PStr(nm, en);
		Log("GetSharedLibrary %s: err %d conn %lx main %lx errName '%s'", how[k], (int)e,
			(long)conn, (long)mainAddr, nm);
		if (!e)
			break;
	}
	if (!e) {
		addr = NULL;
		cls = 0;
		e = FindSymbol(conn, "\paglGetVersion", &addr, &cls);
		Log("FindSymbol aglGetVersion: err %d class %d addr %lx", (int)e, (int)cls, (long)addr);
		cnt = 0;
		e = CountSymbols(conn, &cnt);
		Log("CountSymbols: err %d count %ld", (int)e, cnt);
		for (i = 0; !e && i < 5 && i < cnt; i++) {
			Str255 sn;
			sn[0] = 0;
			cls = 0;
			addr = NULL;
			e = GetIndSymbol(conn, i, sn, &addr, &cls);
			PStr(nm, sn);
			Log("  sym %ld: err %d '%s' class %d addr %lx", i, (int)e, nm, (int)cls, (long)addr);
		}
	}

	e = FindFolder(kOnSystemDisk, kExtensionFolderType, kDontCreateFolder, &vref, &dirid);
	if (e) {
		Log("FindFolder Extensions: err %d", (int)e);
		return;
	}
	e = FSMakeFSSpec(vref, dirid, "\pOpenGLLibrary", &fs);
	Log("FSMakeFSSpec Extensions:OpenGLLibrary: err %d", (int)e);
	if (e)
		return;
	conn = 0;
	mainAddr = NULL;
	en[0] = 0;
	e = GetDiskFragment(&fs, 0, kCFragGoesToEOF, "\pOpenGLLibrary", kPrivateCFragCopy,
		&conn, &mainAddr, en);
	PStr(nm, en);
	Log("GetDiskFragment: err %d conn %lx main %lx errName '%s'", (int)e, (long)conn,
		(long)mainAddr, nm);
	if (!e) {
		addr = NULL;
		cls = 0;
		e = FindSymbol(conn, "\paglGetVersion", &addr, &cls);
		Log("  FindSymbol aglGetVersion: err %d class %d addr %lx", (int)e, (int)cls, (long)addr);
	}
}

int main(void)
{
	Rect r, gr;
	Reply rp;
	ParamBlockRec pb;
	GDHandle gd;
	PvmacInfo *ip = &gInfo;
	const UInt8 *pl;
	UInt32 a[16], ctx, bits;
	float f;
	EventRecord ev;
	QDErr qe;
	int ok = 0;

	InitGraf(&qd.thePort);
	InitFonts();
	InitWindows();
	InitMenus();
	TEInit();
	InitDialogs(NULL);
	InitCursor();
	SetRect(&r, 20, 50, 20 + 540, 50 + 520);
	gWin = NewWindow(NULL, &r, "\pGLHello", true, documentProc, (WindowPtr)-1, true, 0);
	SetPort(gWin);
	Log("GLHello - PVMAC v2 GL stream test");
	CfmProbe();

	gd = GetMainDevice();
	memset(&pb, 0, sizeof pb);
	pb.cntrlParam.ioCRefNum = (**gd).gdRefNum;
	pb.cntrlParam.csCode = PVMAC_CS_GET_INFO;
	memcpy(pb.cntrlParam.csParam, &ip, sizeof ip);
	qe = PBStatusSync(&pb);
	if (qe) {
		Log("csCode 0x8300 failed: %d (not the PVMAC qemu_vga.ndrv?)", (int)qe);
		goto wait;
	}
	Log("PVMAC version %lu caps %08lx", gInfo.version, gInfo.caps);
	Log("regs %08lx vram %08lx", gInfo.regBase, gInfo.vramBase);
	Log("ring +%lx size %lu, reply +%lx size %lu", gInfo.ringOffset, gInfo.ringSize,
		gInfo.replyOffset, gInfo.replySize);
	if (!(gInfo.caps & CAP_GL_STREAM) || !gInfo.regBase || !gInfo.ringSize) {
		Log("no GL stream (caps bit 1 clear): use mac-accel=on, PVMAC v2");
		goto wait;
	}

	/* DG_HELLO: args[0] magic, [1] version, [2] body offset+1, [3..4]
	 * resource namespace (stable per boot), [5..6] client features. The
	 * body is the DG_LAYOUT string with its NUL, as the client sends it. */
	memset(a, 0, sizeof a);
	a[0] = DG_MAGIC;
	a[1] = DG_VERSION;
	a[2] = 1;
	a[3] = 0x4d4f5339;		/* "MOS9" */
	a[5] = DG_FEAT_MAGIC;
	a[6] = DG_FEAT_LARGE_IOSURFACE_WRITES;
	if (Step("DG_HELLO", DG_HELLO, 0, 0, sizeof GLH_LAYOUT, a, GLH_LAYOUT, sizeof GLH_LAYOUT, &rp, &pl))
		goto done;
	Log("  capabilities %08lx%08lx", rp.resultHi, rp.resultLo);

	memset(a, 0, sizeof a);
	if (Step("DG_CREATE", DG_CREATE, 0, 0, 0, a, 0, 0, &rp, &pl) || !rp.resultLo)
		goto done;
	ctx = rp.resultLo;

	memset(a, 0, sizeof a);
	a[0] = W;
	a[1] = H;
	a[2] = 1;			/* drawable id */
	if (Step("DG_RESIZE", DG_RESIZE, ctx, 0, 0, a, 0, 0, &rp, &pl))
		goto done;

	/* Generated GL opcodes: scalar arguments in their native bit patterns
	 * (floats as IEEE bits); no reply, so they queue behind the next one. */
	memset(a, 0, sizeof a);
	f = 1.0f;
	memcpy(&bits, &f, 4);
	a[0] = bits;			/* red */
	a[3] = bits;			/* alpha */
	if (Send(GLH_OP_CLEAR_COLOR, ctx, 0, 0, a, 0, 0, &rp, &pl))
		goto done;
	memset(a, 0, sizeof a);
	a[0] = GL_COLOR_BUFFER_BIT;
	if (Send(GLH_OP_CLEAR, ctx, 0, 0, a, 0, 0, &rp, &pl))
		goto done;
	Log("glClearColor(1,0,0,1) + glClear queued");

	memset(a, 0, sizeof a);
	a[0] = W;
	a[1] = H;			/* args[2] = 0: BGRA8 */
	if (Step("DG_READ_DRAWABLE", DG_READ_DRAWABLE, ctx, DG_OUTPUT_ONLY, W * H * 4, a, 0, 0, &rp, &pl))
		goto done;
	if (rp.bytes == W * H * 4) {
		SetRect(&gr, 0, 0, W, H);
		qe = NewGWorld(&gGW, 32, &gr, NULL, NULL, 0);
		if (qe) {
			Log("NewGWorld failed: %d", (int)qe);
		} else {
			Log("pixel(0,0) B,G,R,A = %02x %02x %02x %02x", pl[0], pl[1], pl[2], pl[3]);
			Show(pl);
			ok = 1;
		}
	}

	memset(a, 0, sizeof a);
	a[0] = 1;
	Step("DG_DROP_DRAWABLE", DG_DROP_DRAWABLE, ctx, 0, 0, a, 0, 0, &rp, &pl);
	memset(a, 0, sizeof a);
	Step("DG_DESTROY", DG_DESTROY, ctx, 0, 0, a, 0, 0, &rp, &pl);
done:
	Log("GL_STATUS %08lx (write-1-to-clear)", RegRd(REG_GL_STATUS));
	Log(ok ? "Done: red square = success. Click to quit." : "FAILED. Click to quit.");
wait:
	for (;;) {
		if (!WaitNextEvent(everyEvent, &ev, 30, NULL))
			continue;
		if (ev.what == mouseDown || ev.what == keyDown)
			break;
		if (ev.what == updateEvt) {
			BeginUpdate(gWin);
			for (qe = 0; qe < gLine; qe++)
				DrawLine(qe);
			if (gGW)
				Blit();
			EndUpdate(gWin);
		}
	}
	return 0;
}
