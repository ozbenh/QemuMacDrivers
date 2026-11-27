/* AGL 1.2 on top of the DG client: one pixel format, one renderer (reported
 * as accelerated), windowed drawables presented by reading the host's
 * drawable back straight into the window's area of the screen.
 *
 * Written from the public AGL API; the wire sequence is the one GLHello
 * proved: DG_HELLO (once), DG_CREATE, DG_RESIZE, GL calls, DG_READ_DRAWABLE,
 * DG_DROP_DRAWABLE, DG_DESTROY. Context make-current is client-side only.
 */
#include <MacTypes.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <string.h>
#include "agl.h"
#include "dg_client.h"
#include "dg_transport.h"
#include "direct-layout.h"		/* DG_LAYOUT, generated */
#include "gl_dispatch.h"		/* gGLIDispatch, generated */

/* exported encoders (gl_generated.c) used for AGL-defined initial state */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);
void glEnable(GLenum cap);

#define UTM_DEPTH_CLAMP 0x864F	/* depth clamp enable (ARB_depth_clamp) */

/* GL's origin is bottom-left: the executor returns drawable rows bottom-up
 * and the device flips them while writing the screen. If frames ever show up
 * upside down, this is the only switch. */
#define PRESENT_FLAGS (PV_FLAG_SWAP32 | PV_FLAG_FLIP)

#define AGL_RENDERER_ID_VALUE 0x00028000	/* fixed, unused by Apple's renderers */

#define AGL_OPTIONS 16			/* entries of kEnableKnown */

/* glc_cc: Apple's GLU imports it as data and uses it like OS X's CGL macros:
 * a pointer to { GLIContext rend; GLIFunctionDispatch disp; } (rend at +0,
 * dispatch slot i at +4+4*i, each a TVector), NULL when no context is current.
 * gGLIObject is that object; rend is the current AGLContext. */
struct GLIObject {
	void *rend;
	GLIFn disp[GLI_SLOTS];
};
typedef char GLIObjectLayout[(sizeof(struct GLIObject) == 4 + 4 * GLI_SLOTS) ? 1 : -1];
static struct GLIObject gGLIObject;
struct GLIObject *glc_cc;		/* exported data symbol, see agl.h */

struct __AGLPixelFormatRec { int depth, stencil; };
struct __AGLRendererInfoRec { int unused; };
struct __AGLContextRec {
	struct DGContext dg;			/* first: dg_current points here */
	AGLDrawable port;
	int width, height;
	uint32_t drawable, nextDrawable;
	struct __AGLContextRec *next;		/* gContexts: this process' contexts */
	GLint enabled[AGL_OPTIONS];		/* aglEnable state, by OptionIndex */
	GLint option[AGL_OPTIONS][4];		/* aglSetInteger values (rects and colormap entries use 4) */
	int classic_rave;			/* created by Apple's Classic RAVE (see FromClassicRAVE) */
};

static struct __AGLPixelFormatRec gFormat = { 24, 0 };
static struct __AGLRendererInfoRec gRenderer;
static int gHello;			/* 1 done, -1 failed, 0 not yet (this process; the result is shared) */
static struct __AGLContextRec *gContexts;	/* contexts this process still owns (released by UTMGLTerm) */
static GLenum gAglError;

static int SetError(GLenum e) { gAglError = e; return 0; }

/* --- connection (DG_HELLO once per stream: the first process sends it, later
 * processes take the negotiated capabilities from the shared block) */
static int Hello(void)
{
	struct DGContext boot;
	struct DGPacket p;
	struct DGReply reply;
	struct DGHelloFeatures f;
	struct PvmacShared *sh;

	if (gHello)
		return gHello > 0 ? 0 : -1;
	gHello = -1;
	if (PvmacOpen())
		return -1;
	sh = PvmacSharedBlock();
	if (sh->hello) {
		if (sh->hello < 0)
			return -1;
		dg_connection.capabilities = sh->capabilities;
		dg_connection.features = sh->features;
		dg_connection.features2 = sh->features2;
		dg_connection.features3 = sh->features3;
		DGLog("DG_HELLO shared: capabilities %lx%08lx", (unsigned long)(dg_connection.capabilities >> 32),
			(unsigned long)dg_connection.capabilities);
		gHello = 1;
		return 0;
	}
	sh->hello = -1;
	memset(&boot, 0, sizeof boot);
	boot.connection = &dg_connection;
	p = DGPacketNew(&boot, DG_HELLO);
	p.message.args[0] = DG_MAGIC;
	p.message.args[1] = DG_VERSION;
	p.message.args[3] = 0x4d4f5339;		/* "MOS9": resource namespace, stable per boot */
	p.message.args[5] = DG_HELLO_FEATURES_MAGIC;
	p.message.args[6] = DG_FEATURE_LARGE_IOSURFACE_WRITES;
	DGPacketPointerW(&p, 2, DG_LAYOUT, sizeof DG_LAYOUT, 0, 1);
	reply = DGSubmit(&boot, &p, 1);
	if (!p.valid_reply) {
		DGLog("DG_HELLO failed: error %lu (layout %s)", (unsigned long)reply.error, DG_LAYOUT);
		DGPacketFree(&p);
		return -1;
	}
	dg_connection.capabilities = reply.result;
	memset(&f, 0, sizeof f);
	DGPacketCopyW(&p, 2, &f, sizeof f, 4);
	if (f.magic == DG_HELLO_FEATURES_MAGIC) {
		dg_connection.features = f.features;
		if (f.magic2 == DG_HELLO_FEATURES2_MAGIC)
			dg_connection.features2 = f.features2;
		if (f.magic3 == DG_HELLO_FEATURES3_MAGIC)
			dg_connection.features3 = f.features3;
	}
	DGPacketFree(&p);
	sh->capabilities = dg_connection.capabilities;
	sh->features = dg_connection.features;
	sh->features2 = dg_connection.features2;
	sh->features3 = dg_connection.features3;
	sh->hello = 1;
	DGLog("DG_HELLO ok: capabilities %lx%08lx features %lx", (unsigned long)(dg_connection.capabilities >> 32),
		(unsigned long)dg_connection.capabilities, (unsigned long)dg_connection.features);
	gHello = 1;
	return 0;
}

/* One message with a reply and no payload (DG_CREATE, DG_RESIZE, ...). */
static int Simple(struct DGContext *c, uint32_t op, uint64_t a0, uint64_t a1, uint64_t a2, struct DGReply *reply)
{
	struct DGPacket p = DGPacketNew(c, op);

	p.message.args[0] = a0;
	p.message.args[1] = a1;
	p.message.args[2] = a2;
	*reply = DGSubmit(c, &p, 1);
	DGPacketFree(&p);
	return reply->error || dg_connection.dead ? -1 : 0;
}

static void Impl_aglGetVersion(GLint *major, GLint *minor)
{
	if (major)
		*major = 1;
	if (minor)
		*minor = 2;
}

/* --- pixel format and renderer */
static AGLPixelFormat Impl_aglChoosePixelFormat(const AGLDevice *gdevs, GLint ndev, const GLint *attribs)
{
	const GLint *a = attribs;
	int depth = 0, stencil = 0;

	(void)gdevs;
	(void)ndev;
	if (PvmacOpen()) {
		SetError(AGL_BAD_PIXELFMT);
		return NULL;
	}
	while (a && *a != AGL_NONE) {
		GLint attr = *a++, value = 0;

		switch (attr) {
		case AGL_BUFFER_SIZE: case AGL_LEVEL: case AGL_AUX_BUFFERS:
		case AGL_RED_SIZE: case AGL_GREEN_SIZE: case AGL_BLUE_SIZE: case AGL_ALPHA_SIZE:
		case AGL_DEPTH_SIZE: case AGL_STENCIL_SIZE:
		case AGL_ACCUM_RED_SIZE: case AGL_ACCUM_GREEN_SIZE: case AGL_ACCUM_BLUE_SIZE: case AGL_ACCUM_ALPHA_SIZE:
		case AGL_PIXEL_SIZE: case AGL_SAMPLE_BUFFERS_ARB: case AGL_SAMPLES_ARB:
		case AGL_RENDERER_ID: case AGL_VIRTUAL_SCREEN:
			value = *a++;
			break;
		default:
			break;
		}
		switch (attr) {
		case AGL_STEREO: case AGL_OFFSCREEN: case AGL_PBUFFER:
			SetError(AGL_BAD_ATTRIBUTE);
			return NULL;
		case AGL_AUX_BUFFERS: case AGL_SAMPLE_BUFFERS_ARB: case AGL_SAMPLES_ARB:
		case AGL_ACCUM_RED_SIZE: case AGL_ACCUM_GREEN_SIZE: case AGL_ACCUM_BLUE_SIZE: case AGL_ACCUM_ALPHA_SIZE:
			if (value > 0) {
				SetError(AGL_BAD_ATTRIBUTE);
				return NULL;
			}
			break;
		case AGL_RENDERER_ID:
			if (value != AGL_RENDERER_ID_VALUE) {
				SetError(AGL_BAD_MATCH);
				return NULL;
			}
			break;
		case AGL_DEPTH_SIZE: depth = value; break;
		case AGL_STENCIL_SIZE: stencil = value; break;
		default:
			break;
		}
	}
	gFormat.depth = depth > 16 ? 24 : (depth ? 16 : 24);
	gFormat.stencil = stencil ? 8 : 0;
	return &gFormat;
}

static void Impl_aglDestroyPixelFormat(AGLPixelFormat pix) { (void)pix; }

static GLboolean Impl_aglDescribePixelFormat(AGLPixelFormat pix, GLint attrib, GLint *value)
{
	GLint v = 0;

	if (!pix || !value)
		return SetError(AGL_BAD_PIXELFMT);
	switch (attrib) {
	case AGL_RGBA: case AGL_DOUBLEBUFFER: case AGL_ACCELERATED: case AGL_NO_RECOVERY:
	case AGL_WINDOW: case AGL_COMPLIANT: case AGL_BACKING_STORE:
		v = 1; break;
	case AGL_BUFFER_SIZE: case AGL_PIXEL_SIZE: v = 32; break;
	case AGL_RED_SIZE: case AGL_GREEN_SIZE: case AGL_BLUE_SIZE: case AGL_ALPHA_SIZE: v = 8; break;
	case AGL_DEPTH_SIZE: v = pix->depth; break;
	case AGL_STENCIL_SIZE: v = pix->stencil; break;
	case AGL_RENDERER_ID: v = AGL_RENDERER_ID_VALUE; break;
	case AGL_LEVEL: case AGL_STEREO: case AGL_AUX_BUFFERS: case AGL_ACCUM_RED_SIZE:
	case AGL_ACCUM_GREEN_SIZE: case AGL_ACCUM_BLUE_SIZE: case AGL_ACCUM_ALPHA_SIZE:
	case AGL_OFFSCREEN: case AGL_FULLSCREEN: case AGL_SAMPLE_BUFFERS_ARB: case AGL_SAMPLES_ARB:
	case AGL_ROBUST: case AGL_MP_SAFE: case AGL_MULTISCREEN: case AGL_PBUFFER: case AGL_VIRTUAL_SCREEN:
		v = 0; break;
	default:
		return SetError(AGL_BAD_ATTRIBUTE);
	}
	*value = v;
	return GL_TRUE;
}

static AGLRendererInfo Impl_aglQueryRendererInfo(const AGLDevice *gdevs, GLint ndev)
{
	(void)gdevs;
	(void)ndev;
	if (PvmacOpen()) {
		SetError(AGL_BAD_RENDINFO);
		return NULL;
	}
	return &gRenderer;
}

static void Impl_aglDestroyRendererInfo(AGLRendererInfo rend) { (void)rend; }

/* One renderer only: the list ends after the first. */
static AGLRendererInfo Impl_aglNextRendererInfo(AGLRendererInfo rend)
{
	(void)rend;
	return NULL;
}

static GLboolean Impl_aglDescribeRenderer(AGLRendererInfo rend, GLint prop, GLint *value)
{
	GLint v = 0;

	if (!rend || !value)
		return SetError(AGL_BAD_RENDINFO);
	switch (prop) {
	case AGL_RENDERER_ID: v = AGL_RENDERER_ID_VALUE; break;
	case AGL_ACCELERATED: case AGL_WINDOW: case AGL_COMPLIANT: case AGL_ROBUST: v = 1; break;
	case AGL_BUFFER_MODES: v = AGL_SINGLEBUFFER_BIT | AGL_DOUBLEBUFFER_BIT | AGL_MONOSCOPIC_BIT; break;
	case AGL_COLOR_MODES: v = AGL_RGB888_BIT | AGL_ARGB8888_BIT; break;
	case AGL_DEPTH_MODES: v = AGL_16_BIT | AGL_24_BIT; break;
	case AGL_STENCIL_MODES: v = AGL_0_BIT | AGL_8_BIT; break;
	case AGL_ACCUM_MODES: v = AGL_0_BIT; break;
	case AGL_MAX_AUX_BUFFERS: case AGL_MIN_LEVEL: case AGL_MAX_LEVEL: case AGL_FULLSCREEN:
	case AGL_OFFSCREEN: case AGL_PBUFFER: case AGL_MULTISCREEN: case AGL_STEREO:
		v = 0; break;
	case AGL_VIDEO_MEMORY: case AGL_TEXTURE_MEMORY: v = 64 * 1024 * 1024; break;
	default:
		return SetError(AGL_BAD_PROPERTY);
	}
	*value = v;
	return GL_TRUE;
}

/* --- contexts and drawables */
static AGLContext Impl_aglCreateContext(AGLPixelFormat pix, AGLContext share)
{
	struct __AGLContextRec *c;
	struct DGReply reply;

	if (!pix) {
		SetError(AGL_BAD_PIXELFMT);
		return NULL;
	}
	if (Hello()) {
		SetError(AGL_BAD_CONNECTION);
		return NULL;
	}
	c = (struct __AGLContextRec *)NewPtrClear(sizeof *c);
	if (!c) {
		SetError(AGL_BAD_ALLOC);
		return NULL;
	}
	c->dg.connection = &dg_connection;
	c->dg.alignment[0] = c->dg.alignment[1] = 4;
	c->dg.profile = 0x1000;
	/* args[1]/[2] = depth/stencil bits of the host pixel format (VIM DG_CREATE);
	 * 0 creates a context with no depth buffer. */
	if (Simple(&c->dg, DG_CREATE, share ? share->dg.id : 0, (uint64_t)pix->depth, (uint64_t)pix->stencil, &reply)
	    || !reply.result) {
		DGLog("DG_CREATE failed: error %lu", (unsigned long)reply.error);
		DisposePtr((Ptr)c);
		SetError(AGL_BAD_CONTEXT);
		return NULL;
	}
	c->dg.id = (uint32_t)reply.result;
	c->next = gContexts;
	gContexts = c;
	return c;
}

static void DropDrawable(struct __AGLContextRec *c)
{
	struct DGReply reply;

	if (c->drawable)
		Simple(&c->dg, DG_DROP_DRAWABLE, c->drawable, 0, 0, &reply);
	c->drawable = 0;
	c->width = c->height = 0;
}

static void Unlink(struct __AGLContextRec *c)
{
	struct __AGLContextRec **pp;

	for (pp = &gContexts; *pp; pp = &(*pp)->next)
		if (*pp == c) {
			*pp = c->next;
			break;
		}
}

static GLboolean Impl_aglDestroyContext(AGLContext ctx)
{
	struct DGReply reply;

	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	Unlink(ctx);
	DropDrawable(ctx);
	Simple(&ctx->dg, DG_DESTROY, 0, 0, 0, &reply);
	if (dg_current == &ctx->dg)
		dg_current = NULL;
	if (glc_cc && glc_cc->rend == ctx)
		glc_cc = NULL;
	DGClientAttribFree(&ctx->dg);
	DisposePtr((Ptr)ctx);
	return GL_TRUE;
}

/* CFM termination routine (termSection / termOffset of the PEF loader header,
 * set by set-pef-init-term.py; exported so the script can find its TVector).
 * Runs when the process that loaded the library quits: release the host
 * contexts it still owns (a GL application that does not call aglDestroyContext
 * would leak them for the whole boot now that the stream outlives a process),
 * hand over what is queued and leave the shared stream. Memory is not freed:
 * the process heap goes away with the process. */
void UTMGLTerm(void)
{
	struct __AGLContextRec *c;
	struct DGReply reply;

	while ((c = gContexts) != NULL) {
		gContexts = c->next;
		if (gHello > 0 && !dg_connection.dead) {
			DropDrawable(c);
			Simple(&c->dg, DG_DESTROY, 0, 0, 0, &reply);	/* also flushes the compact run first */
		}
	}
	dg_current = NULL;
	glc_cc = NULL;
	DGFlushTransport(NULL);
	PvmacDetach();
}

static GLboolean Impl_aglSetCurrentContext(AGLContext ctx)
{
	dg_current = ctx ? &ctx->dg : NULL;
	if (ctx) {
		if (!gGLIObject.disp[0])
			memcpy(gGLIObject.disp, gGLIDispatch, sizeof gGLIObject.disp);
		gGLIObject.rend = ctx;
		glc_cc = &gGLIObject;
	} else
		glc_cc = NULL;
	return GL_TRUE;
}

/* dg is the first member of the context record, so the pointer maps back. */
static AGLContext Impl_aglGetCurrentContext(void)
{
	return (AGLContext)dg_current;
}

static GLenum Impl_aglGetError(void)
{
	GLenum e = gAglError;

	gAglError = 0;			/* AGL_NO_ERROR */
	return e;
}

static const GLubyte *Impl_aglErrorString(GLenum code)
{
	switch (code) {
	case 0: return (const GLubyte *)"no error";
	case AGL_BAD_ATTRIBUTE: return (const GLubyte *)"invalid pixel format attribute";
	case AGL_BAD_PROPERTY: return (const GLubyte *)"invalid renderer property";
	case AGL_BAD_PIXELFMT: return (const GLubyte *)"invalid pixel format";
	case AGL_BAD_RENDINFO: return (const GLubyte *)"invalid renderer info";
	case AGL_BAD_CONTEXT: return (const GLubyte *)"invalid context";
	case AGL_BAD_DRAWABLE: return (const GLubyte *)"invalid drawable";
	case AGL_BAD_GDEV: return (const GLubyte *)"invalid graphics device";
	case AGL_BAD_STATE: return (const GLubyte *)"invalid context state";
	case AGL_BAD_VALUE: return (const GLubyte *)"invalid numerical value";
	case AGL_BAD_MATCH: return (const GLubyte *)"invalid share context";
	case AGL_BAD_ENUM: return (const GLubyte *)"invalid enumerant";
	case AGL_BAD_OFFSCREEN: return (const GLubyte *)"invalid offscreen drawable";
	case AGL_BAD_FULLSCREEN: return (const GLubyte *)"invalid fullscreen drawable";
	case AGL_BAD_WINDOW: return (const GLubyte *)"invalid window";
	case AGL_BAD_POINTER: return (const GLubyte *)"invalid pointer";
	case AGL_BAD_MODULE: return (const GLubyte *)"invalid code module";
	case AGL_BAD_ALLOC: return (const GLubyte *)"memory allocation failure";
	case AGL_BAD_CONNECTION: return (const GLubyte *)"invalid CoreGraphics connection";
	default: return (const GLubyte *)"unknown error";
	}
}

/* Port content size in pixels. */
static int AglPortSize(AGLDrawable port, int *w, int *h)
{
	Rect r;

	if (!port)
		return -1;
	r = port->portRect;
	*w = r.right - r.left;
	*h = r.bottom - r.top;
	return *w >= 1 && *h >= 1 && *w <= 8192 && *h <= 8192 ? 0 : -1;
}

/* Last drawable size sent to the host (diagnostics, verbose mode). */
static int sHostW, sHostH;

/* Tell the executor the drawable size: a new drawable id per size, the old
 * one dropped afterwards (VIM's DGMakeCurrentMode does the same). */
static GLboolean ApplyDrawable(struct __AGLContextRec *c, int force)
{
	struct DGReply reply;
	uint32_t old = c->drawable, id;
	int w, h;

	if (AglPortSize(c->port, &w, &h))
		return SetError(AGL_BAD_DRAWABLE);
	if (!force && old && w == c->width && h == c->height)
		return GL_TRUE;
	id = ++c->nextDrawable;
	if (Simple(&c->dg, DG_RESIZE, w, h, id, &reply)) {
		DGLog("DG_RESIZE %dx%d failed: error %lu", w, h, (unsigned long)reply.error);
		return SetError(AGL_BAD_DRAWABLE);
	}
	if (old)
		Simple(&c->dg, DG_DROP_DRAWABLE, old, 0, 0, &reply);
	c->drawable = id;
	c->width = w;
	c->height = h;
	if (!old) {
		/* AGL semantics: the first time a context gets a drawable, its
		 * viewport and scissor box are set to the drawable's size. The
		 * host CGL context starts without a drawable (viewport 0x0), so
		 * apps that never call glViewport (Apple's Classic RAVE) drew
		 * nothing. Later resizes leave them alone, as in AGL. */
		struct DGContext *save = dg_current;

		dg_current = &c->dg;
		glViewport(0, 0, w, h);
		glScissor(0, 0, w, h);
		if (c->classic_rave)
			glEnable(UTM_DEPTH_CLAMP);
		dg_current = save;
		DGVLog("  initial viewport/scissor %dx%d", w, h);
	}
	sHostW = w;
	sHostH = h;
	if (DGVerbose())
		DGVLog("  DG_RESIZE sent %dx%d drawable %lu", w, h, (unsigned long)id);
	return GL_TRUE;
}

static GLboolean Impl_aglSetDrawable(AGLContext ctx, AGLDrawable draw)
{
	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (!draw) {
		DropDrawable(ctx);
		ctx->port = NULL;
		return GL_TRUE;
	}
	ctx->port = draw;
	if (DGVerbose()) {
		GrafPtr save;
		Point tl;
		Rect r = draw->portRect;

		tl.v = r.top;
		tl.h = r.left;
		GetPort(&save);
		SetPort((GrafPtr)draw);
		LocalToGlobal(&tl);
		SetPort(save);
		DGVLog("  aglSetDrawable: portRect (%d,%d,%d,%d) local->global origin (%d,%d)", (int)r.left, (int)r.top,
			(int)r.right, (int)r.bottom, (int)tl.h, (int)tl.v);
	}
	return ApplyDrawable(ctx, 1);
}

static GLboolean Impl_aglUpdateContext(AGLContext ctx)
{
	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (!ctx->port)
		return GL_TRUE;
	return ApplyDrawable(ctx, 0);
}

/* Full screen is not supported: Apple's AGL takes over a display here. A
 * program should fall back to a drawable (windowed, or a DrawSprocket
 * front-buffer port through aglSetDrawable, which this library presents). */
static GLboolean Impl_aglSetFullScreen(AGLContext ctx, GLsizei width, GLsizei height, GLsizei freq, GLint device)
{
	(void)ctx;
	(void)freq;
	(void)device;
	DGLog("aglSetFullScreen(%d x %d) refused: use aglSetDrawable", (int)width, (int)height);
	return SetError(AGL_BAD_FULLSCREEN);
}

static const GLint kEnableKnown[AGL_OPTIONS + 1] = {
	AGL_SWAP_RECT, AGL_BUFFER_RECT, AGL_SWAP_LIMIT, AGL_COLORMAP_TRACKING, AGL_COLORMAP_ENTRY,
	AGL_RASTERIZATION, AGL_SWAP_INTERVAL, AGL_STATE_VALIDATION, AGL_BUFFER_NAME,
	AGL_ORDER_CONTEXT_TO_FRONT, AGL_CONTEXT_SURFACE_ID, AGL_CONTEXT_DISPLAY_ID, AGL_SURFACE_ORDER,
	AGL_SURFACE_OPACITY, AGL_CLIP_REGION, AGL_FS_CAPTURE_SINGLE, 0
};

/* Index of a context option in kEnableKnown, -1 when it is not one. */
static int OptionIndex(GLenum pname)
{
	int i;

	for (i = 0; kEnableKnown[i]; i++)
		if ((GLenum)kEnableKnown[i] == pname)
			return i;
	return -1;
}

static int OptionWords(GLenum pname)
{
	return pname == AGL_SWAP_RECT || pname == AGL_BUFFER_RECT || pname == AGL_COLORMAP_ENTRY ? 4 : 1;
}

static GLboolean Impl_aglEnable(AGLContext ctx, GLenum pname)
{
	int i = OptionIndex(pname);

	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (i < 0)
		return SetError(AGL_BAD_ENUM);
	ctx->enabled[i] = 1;
	return GL_TRUE;
}

static GLboolean Impl_aglDisable(AGLContext ctx, GLenum pname)
{
	int i = OptionIndex(pname);

	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (i < 0)
		return SetError(AGL_BAD_ENUM);
	ctx->enabled[i] = 0;
	return GL_TRUE;
}

static GLboolean Impl_aglIsEnabled(AGLContext ctx, GLenum pname)
{
	int i = OptionIndex(pname);

	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (i < 0)
		return SetError(AGL_BAD_ENUM);
	return ctx->enabled[i] ? GL_TRUE : GL_FALSE;
}

/* The values are stored or ignored: frames are always presented at swap, the
 * whole window at a time (AGL_SWAP_RECT / AGL_BUFFER_RECT / AGL_CLIP_REGION
 * are kept for aglGetInteger only). */
static GLboolean Impl_aglSetInteger(AGLContext ctx, GLenum pname, const GLint *params)
{
	int i = OptionIndex(pname), n, k;

	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (i < 0)
		return SetError(AGL_BAD_ENUM);
	if (!params)
		return SetError(AGL_BAD_POINTER);
	n = OptionWords(pname);
	for (k = 0; k < n; k++)
		ctx->option[i][k] = params[k];
	return GL_TRUE;
}

static GLboolean Impl_aglGetInteger(AGLContext ctx, GLenum pname, GLint *params)
{
	int i, n, k;

	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (!params)
		return SetError(AGL_BAD_POINTER);
	if (pname == AGL_VIRTUAL_SCREEN) {
		params[0] = 0;
		return GL_TRUE;
	}
	i = OptionIndex(pname);
	if (i < 0)
		return SetError(AGL_BAD_ENUM);
	n = OptionWords(pname);
	for (k = 0; k < n; k++)
		params[k] = ctx->option[i][k];
	return GL_TRUE;
}

/* --- pixel format lists, devices, virtual screens, library state
 * One pixel format, one device, one virtual screen. */
static AGLPixelFormat Impl_aglNextPixelFormat(AGLPixelFormat pix)
{
	(void)pix;
	return NULL;
}

static AGLDevice *Impl_aglDevicesOfPixelFormat(AGLPixelFormat pix, GLint *ndevs)
{
	static AGLDevice sDevice[1];

	if (ndevs)
		*ndevs = 0;
	if (!pix || !ndevs) {
		SetError(pix ? AGL_BAD_POINTER : AGL_BAD_PIXELFMT);
		return NULL;
	}
	sDevice[0] = GetMainDevice();
	*ndevs = 1;
	return sDevice;
}

/* The executor has no context copy: only an empty mask succeeds. */
static GLboolean Impl_aglCopyContext(AGLContext src, AGLContext dst, GLuint mask)
{
	if (!src || !dst)
		return SetError(AGL_BAD_CONTEXT);
	if (!mask)
		return GL_TRUE;
	DGLog("aglCopyContext(mask %lx) not supported", (unsigned long)mask);
	return SetError(AGL_BAD_STATE);
}

/* Offscreen rendering is not supported; a NULL base address detaches. */
static GLboolean Impl_aglSetOffScreen(AGLContext ctx, GLsizei width, GLsizei height, GLsizei rowbytes, void *baseaddr)
{
	(void)rowbytes;
	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	if (!baseaddr && !width && !height) {
		DropDrawable(ctx);
		ctx->port = NULL;
		return GL_TRUE;
	}
	DGLog("aglSetOffScreen(%d x %d) not supported: use aglSetDrawable", (int)width, (int)height);
	return SetError(AGL_BAD_OFFSCREEN);
}

static AGLDrawable Impl_aglGetDrawable(AGLContext ctx)
{
	if (!ctx) {
		SetError(AGL_BAD_CONTEXT);
		return NULL;
	}
	return ctx->port;
}

static GLboolean Impl_aglSetVirtualScreen(AGLContext ctx, GLint screen)
{
	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	return screen == 0 ? GL_TRUE : SetError(AGL_BAD_VALUE);
}

static GLint Impl_aglGetVirtualScreen(AGLContext ctx)
{
	if (!ctx)
		SetError(AGL_BAD_CONTEXT);
	return 0;
}

/* Global options (format cache, renderer retention) have nothing to cache here. */
static GLboolean Impl_aglConfigure(GLenum pname, GLuint param)
{
	(void)param;
	switch (pname) {
	case AGL_FORMAT_CACHE_SIZE: case AGL_CLEAR_FORMAT_CACHE: case AGL_RETAIN_RENDERERS:
		return GL_TRUE;
	default:
		return SetError(AGL_BAD_ENUM);
	}
}

/* Bitmap fonts (display lists from a QuickDraw font) are not built. */
static GLboolean Impl_aglUseFont(AGLContext ctx, GLint fontID, Style face, GLint size, GLint first, GLint count, GLint base)
{
	(void)face; (void)first; (void)count; (void)base;
	if (!ctx)
		return SetError(AGL_BAD_CONTEXT);
	DGLog("aglUseFont(font %d size %d) not supported", (int)fontID, (int)size);
	return SetError(AGL_BAD_VALUE);
}

/* No cached renderer or format state; clears the error. */
static void Impl_aglResetLibrary(void)
{
	gAglError = 0;
}

/* --- presentation */
struct Target {
	UInt8 *base;			/* screen base address */
	long rowBytes;
	int depth;			/* pixel size in bits */
	int screenW, screenH;
	int gx, gy;			/* window content origin in global coordinates */
};

static void GetTarget(AGLDrawable port, struct Target *t)
{
	GDHandle gd = GetMainDevice();
	PixMapHandle pm = (**gd).gdPMap;
	GrafPtr save;
	Point tl;
	Rect r;

	t->base = (UInt8 *)(**pm).baseAddr;
	t->rowBytes = (**pm).rowBytes & 0x3FFF;
	t->depth = (**pm).pixelSize;
	t->screenW = (**gd).gdRect.right - (**gd).gdRect.left;
	t->screenH = (**gd).gdRect.bottom - (**gd).gdRect.top;
	GetPort(&save);
	SetPort((GrafPtr)port);
	r = port->portRect;
	tl.v = r.top;
	tl.h = r.left;
	LocalToGlobal(&tl);
	SetPort(save);
	t->gx = tl.h - (**gd).gdRect.left;
	t->gy = tl.v - (**gd).gdRect.top;
}

/* Source rows are bottom-up BGRA bytes (a band of n rows of rw pixels, `src`):
 * the band covers columns rx..rx+rw of the window and bottom-up drawable rows
 * y0..y0+n; screen row of source row s is (h - 1 - s) of the window. CPU path
 * for windows that are partly off screen; the device does the common case. */
static void BlitBand(const struct Target *t, const UInt8 *src, int rx, int rw, int h, int y0, int n)
{
	int r, x, x0 = 0, x1 = rw;

	if (t->gx + rx < 0)
		x0 = -(t->gx + rx);
	if (t->gx + rx + rw > t->screenW)
		x1 = t->screenW - (t->gx + rx);
	for (r = 0; r < n; r++) {
		int sy = t->gy + (h - 1 - (y0 + r));
		const UInt8 *s = src + (long)r * rw * 4;
		UInt8 *d;

		if (sy < 0 || sy >= t->screenH)
			continue;
		d = t->base + (long)sy * t->rowBytes;
		if (t->depth == 32) {
			d += (long)(t->gx + rx + x0) * 4;
			for (x = x0; x < x1; x++, d += 4) {
				d[0] = 0;
				d[1] = s[x * 4 + 2];
				d[2] = s[x * 4 + 1];
				d[3] = s[x * 4];
			}
		} else {
			d += (long)(t->gx + rx + x0) * 2;
			for (x = x0; x < x1; x++, d += 2) {
				UInt16 v = (UInt16)(((s[x * 4 + 2] >> 3) << 10) | ((s[x * 4 + 1] >> 3) << 5) | (s[x * 4] >> 3));
				d[0] = (UInt8)(v >> 8);
				d[1] = (UInt8)v;
			}
		}
	}
}

/* Diagnostics (verbose mode): the first 5 presents, then every 200th. */
static unsigned long sSwapCount;

static unsigned long DiagPixel(const UInt8 *p, int bpp)
{
	return bpp == 4 ? ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]
		: ((unsigned long)p[0] << 8) | p[1];
}

/* Five samples (center, quarter points) and the non-zero pixel count of a
 * band of n rows of w pixels, `pitch` bytes apart, bpp bytes per pixel. */
static void DiagBand(const char *what, const UInt8 *p, long pitch, int bpp, int w, int n)
{
	unsigned long nz = 0;
	int x, r;

	for (r = 0; r < n; r++) {
		const UInt8 *q = p + (long)r * pitch;

		for (x = 0; x < w; x++, q += bpp)
			if (bpp == 4 ? (q[0] | q[1] | q[2] | q[3]) : (q[0] | q[1]))
				nz++;
	}
	#define S(fx, fy) DiagPixel(p + (long)((n * (fy)) / 4) * pitch + (long)((w * (fx)) / 4) * bpp, bpp)
	DGVLog("  %s band0 %dx%d bpp %d: center %lx q(1,1) %lx q(3,1) %lx q(1,3) %lx q(3,3) %lx nonzero %lu of %lu", what, w, n,
		bpp * 8, S(2, 2), S(1, 1), S(3, 1), S(1, 3), S(3, 3), nz, (unsigned long)w * (unsigned long)n);
	#undef S
}

static void DiagPresent(const struct __AGLContextRec *c, const struct Target *t, int w, int h, int direct,
	int bands, UInt32 flags)
{
	CGrafPtr port = c->port;
	GDHandle gd = GetMainDevice();
	UInt8 *scrBase = (UInt8 *)(**(**gd).gdPMap).baseAddr;
	int color = (port->portVersion & 0xC000) == 0xC000;
	UInt8 *pbase = NULL;
	long prb = 0;

	if (color) {
		PixMapHandle pm = port->portPixMap;

		pbase = (UInt8 *)(**pm).baseAddr;
		prb = (**pm).rowBytes & 0x3FFF;
	}
	DGVLog("present #%lu: port %lx %s portBase %lx rowBytes %ld screenBase %lx -> %s", sSwapCount, (unsigned long)(port),
		color ? "CGrafPort" : "GrafPort(b/w)", (unsigned long)(pbase), prb, (unsigned long)(scrBase),
		color ? (pbase == scrBase ? "window/screen port" : "GWorld/offscreen port") : "old-style port");
	DGVLog("  portRect (%d,%d,%d,%d) target base %lx rowBytes %ld depth %d screen %dx%d g (%d,%d)", (int)port->portRect.left,
		(int)port->portRect.top, (int)port->portRect.right, (int)port->portRect.bottom, (unsigned long)(t->base), t->rowBytes,
		t->depth, t->screenW, t->screenH, t->gx, t->gy);
	DGVLog("  ctx %dx%d host drawable %dx%d direct %d bands %d flags %lx vramBase %lx ringOffset %lx", c->width,
		c->height, sHostW, sHostH, direct, bands, (unsigned long)(flags), (unsigned long)(gPv.vramBase), (unsigned long)(gPv.ringOffset));
	(void)w; (void)h;
}

/* Read the rectangle (rx, rtop, rw, rh) of the drawable (window coordinates,
 * top-left origin; the whole window for the fast path) in bands that fit the
 * reply area and show it at the window's place on the main screen. */
static int PresentRect(struct __AGLContextRec *c, const struct Target *t, int rx, int rtop, int rw, int rh, int diag)
{
	struct DGReply reply;
	int w = c->width, h = c->height, y0, yEnd, n, rowsMax, direct;
	UInt32 flags;
	UInt32 bytes, rowBytes = (UInt32)rw * 4;
	int sx = t->gx + rx, sy = t->gy + rtop;

	rowsMax = (int)(gPv.replySize / rowBytes);
	if (rowsMax < 1 || rowBytes > 65536) {
		DGLog("present: window %d wide does not fit the reply area", rw);
		return -1;
	}
	/* The device writes rows straight into VRAM when the rectangle is on the
	 * main 32- or 16-bit screen (aligned, below the reserved ring); 16-bit
	 * uses the device's RGB555 conversion. */
	direct = sx >= 0 && sy >= 0 && sx + rw <= t->screenW && sy + rh <= t->screenH &&
		(UInt32)t->base >= gPv.vramBase && !((UInt32)t->base & 1) && !(t->rowBytes & 1) &&
		(UInt32)t->base - gPv.vramBase + (UInt32)(sy + rh) * (UInt32)t->rowBytes <= gPv.ringOffset;
	if (t->depth == 32 && (((UInt32)t->base & 3) || (t->rowBytes & 3)))
		direct = 0;
	flags = t->depth == 32 ? PRESENT_FLAGS : (PV_FLAG_RGB555 | PV_FLAG_FLIP);
	if (diag)
		DiagPresent(c, t, w, h, direct, (rh + rowsMax - 1) / rowsMax, flags);
	/* bottom-up rows of the drawable covered by the rectangle */
	y0 = h - rtop - rh;
	yEnd = y0 + rh;
	for (; y0 < yEnd; y0 += n) {
		struct DGPacket p;
		int whole;

		n = yEnd - y0 < rowsMax ? yEnd - y0 : rowsMax;
		whole = rx == 0 && rw == w && y0 == 0 && n == h;
		bytes = (UInt32)n * rowBytes;
		if (!whole && !(dg_connection.capabilities & DG_CAP_DRAWABLE_READ_RECTS)) {
			DGLog("present: %dx%d needs banded reads, host lacks DG_CAP_DRAWABLE_READ_RECTS", w, h);
			return -1;
		}
		p = DGPacketNew(&c->dg, whole ? DG_READ_DRAWABLE : DG_READ_DRAWABLE_RECT);
		p.message.flags = DG_OUTPUT_ONLY;
		p.message.bytes = bytes;
		p.message.args[0] = rw;
		p.message.args[1] = n;
		if (!whole) {
			p.message.args[2] = rx;		/* x */
			p.message.args[3] = y0;		/* y, from the bottom */
		}				/* else args[2] = 0: BGRA8 */
		if (DGSubmitHeader(&c->dg, &p, &reply) || reply.error || reply.bytes != bytes) {
			DGLog("present: read failed (error %lu bytes %lu of %lu)", (unsigned long)reply.error,
				(unsigned long)reply.bytes, (unsigned long)bytes);
			DGPacketFree(&p);
			if (!dg_connection.dead && reply.bytes && reply.bytes <= gPv.replySize)
				PvmacDoorbell(reply.bytes);	/* keep the stream in step */
			return -1;
		}
		DGPacketFree(&p);
		if (direct) {
			UInt32 dest = (UInt32)t->base - gPv.vramBase + (UInt32)(t->gy + h - y0 - n) * (UInt32)t->rowBytes +
				(UInt32)(t->gx + rx) * (t->depth == 32 ? 4 : 2);
			int bad;

			PvmacRegWr(PV_REG_REPLY_DEST, dest);
			PvmacRegWr(PV_REG_REPLY_PITCH, (UInt32)t->rowBytes);
			PvmacRegWr(PV_REG_REPLY_ROW, rowBytes);
			PvmacRegWr(PV_REG_REPLY_FLAGS, flags | PV_FLAG_PRESENT);
			bad = PvmacDoorbell(bytes);
			if (diag && y0 == h - rtop - rh && !bad)
				DiagBand("VRAM after doorbell", t->base + (long)(t->gy + h - y0 - n) * t->rowBytes +
					(long)(t->gx + rx) * (t->depth == 32 ? 4 : 2), t->rowBytes, t->depth == 32 ? 4 : 2, rw, n);
			PvmacRegWr(PV_REG_REPLY_FLAGS, 0);
			PvmacRegWr(PV_REG_REPLY_DEST, 0);
			if (bad)
				return -1;
		} else {
			if (PvmacDoorbell(bytes))
				return -1;
			if (diag && y0 == h - rtop - rh)
				DiagBand("reply area", PvmacReplyArea(), (long)rowBytes, 4, rw, n);
			BlitBand(t, PvmacReplyArea(), rx, rw, h, y0, n);
		}
	}
	return 0;
}

/* --- visible region (S5): present only what is not covered by other windows,
 * menus or dialogs. The visible area is visRgn (QuickDraw already removed the
 * windows in front) intersected with clipRgn and portRect, all in the port's
 * local coordinates, which only differ from window coordinates by portRect's
 * top-left. A rectangular result is used as is; a complex region is split
 * into rectangles from the classic QuickDraw region record (rgnSize, rgnBBox,
 * then per scanline: y, x toggle list, 0x7FFF; final 0x7FFF): the toggles of
 * a scanline flip the "inside" state of the x intervals until the next one,
 * so each scanline band yields one rectangle per inside interval. */
#define VIS_MAX_RECTS 128
#define VIS_MAX_X 64
#define RGN_END 0x7FFF

static RgnHandle gVisA, gVisB;		/* scratch regions, made on first use */

/* Add local rectangle l,t,r,b as a window-coordinate rectangle, clipped to
 * `lim` (window coordinates; the on-screen part of the window). */
static void VisAdd(Rect *out, int *n, const Rect *portRect, const Rect *lim, int l, int t, int r, int b)
{
	Rect q;

	q.left = (short)(l - portRect->left);
	q.right = (short)(r - portRect->left);
	q.top = (short)(t - portRect->top);
	q.bottom = (short)(b - portRect->top);
	if (q.left < lim->left) q.left = lim->left;
	if (q.top < lim->top) q.top = lim->top;
	if (q.right > lim->right) q.right = lim->right;
	if (q.bottom > lim->bottom) q.bottom = lim->bottom;
	if (q.left >= q.right || q.top >= q.bottom)
		return;
	out[(*n)++] = q;
}

/* Split a region into rectangles; -1 when it has more than the fixed limits. */
static int VisSplit(RgnHandle rgn, const Rect *portRect, const Rect *lim, Rect *out, int *n)
{
	const short *p, *end;
	short cur[VIS_MAX_X], nxt[VIS_MAX_X];
	int ncur = 0, yPrev = 0, rc = 0;

	HLock((Handle)rgn);
	p = (const short *)((const char *)*rgn + 10);
	end = (const short *)((const char *)*rgn + (**rgn).rgnSize);
	while (p < end && *p != RGN_END) {
		int y = *p++, i = 0, j, m = 0;
		const short *xs = p;
		int nx = 0;

		while (p < end && *p != RGN_END) {
			p++;
			nx++;
		}
		if (p >= end) {
			rc = -1;
			break;
		}
		p++;				/* the scanline's terminator */
		if (ncur && y > yPrev) {
			for (j = 0; j + 1 < ncur; j += 2) {
				if (*n + 1 > VIS_MAX_RECTS) {
					rc = -1;
					break;
				}
				VisAdd(out, n, portRect, lim, cur[j], yPrev, cur[j + 1], y);
			}
			if (rc)
				break;
		}
		/* new state = current intervals' edges xor the toggles of this scanline (both sorted) */
		j = 0;
		while ((i < ncur || j < nx) && !rc) {
			short v;

			if (j >= nx || (i < ncur && cur[i] < xs[j]))
				v = cur[i++];
			else if (i >= ncur || xs[j] < cur[i])
				v = xs[j++];
			else {
				i++;
				j++;
				continue;
			}
			if (m >= VIS_MAX_X) {
				rc = -1;
				break;
			}
			nxt[m++] = v;
		}
		if (rc)
			break;
		memcpy(cur, nxt, m * sizeof(short));
		ncur = m;
		yPrev = y;
	}
	HUnlock((Handle)rgn);
	return rc;
}

/* 1: the whole window is visible (fast path, `out` unused); 0: out[0..*n) are
 * the visible rectangles in window coordinates, already limited to the part of
 * the window that is on the main screen (*n == 0: nothing to show). */
static int VisibleRects(const struct __AGLContextRec *c, const struct Target *t, Rect *out, int *n)
{
	GrafPtr gp = (GrafPtr)c->port;
	RgnHandle vis = gp->visRgn, clip = gp->clipRgn;
	Rect pr = c->port->portRect, lim, r;
	int w = c->width, h = c->height;

	*n = 0;
	if (!vis || !clip || !*vis || !*clip)
		return 1;
	lim.left = t->gx < 0 ? (short)-t->gx : 0;
	lim.top = t->gy < 0 ? (short)-t->gy : 0;
	lim.right = t->gx + w > t->screenW ? (short)(t->screenW - t->gx) : (short)w;
	lim.bottom = t->gy + h > t->screenH ? (short)(t->screenH - t->gy) : (short)h;
	if ((**vis).rgnSize == 10 && (**clip).rgnSize == 10) {
		r = pr;
		if (!SectRect(&r, &(**vis).rgnBBox, &r) || !SectRect(&r, &(**clip).rgnBBox, &r))
			return 0;
		if (EqualRect(&r, &pr))
			return 1;
		VisAdd(out, n, &pr, &lim, r.left, r.top, r.right, r.bottom);
		return 0;
	}
	if (!gVisA)
		gVisA = NewRgn();
	if (!gVisB)
		gVisB = NewRgn();
	if (!gVisA || !gVisB) {
		DGLog("present: no memory for the visible region, showing the whole window");
		return 1;
	}
	SectRgn(vis, clip, gVisA);
	RectRgn(gVisB, &pr);
	SectRgn(gVisA, gVisB, gVisA);
	if (EmptyRgn(gVisA))
		return 0;
	if ((**gVisA).rgnSize == 10 && EqualRect(&(**gVisA).rgnBBox, &pr))
		return 1;
	if ((**gVisA).rgnSize == 10) {
		r = (**gVisA).rgnBBox;
		VisAdd(out, n, &pr, &lim, r.left, r.top, r.right, r.bottom);
		return 0;
	}
	if (VisSplit(gVisA, &pr, &lim, out, n)) {
		/* too complex: fall back to the region's bounding box */
		DGLog("present: visible region too complex, using its bounding box");
		*n = 0;
		r = (**gVisA).rgnBBox;
		VisAdd(out, n, &pr, &lim, r.left, r.top, r.right, r.bottom);
	}
	return 0;
}

static int Present(struct __AGLContextRec *c)
{
	struct Target t;
	Rect rects[VIS_MAX_RECTS];
	int w = c->width, h = c->height, diag, n, i, vis;

	GetTarget(c->port, &t);
	sSwapCount++;
	diag = DGVerbose() && (sSwapCount <= 5 || sSwapCount % 200 == 0);
	if (t.depth != 32 && t.depth != 16) {
		DGLog("present: %d-bit screen not supported", t.depth);
		return -1;
	}
	vis = VisibleRects(c, &t, rects, &n);
	if (!vis && !(dg_connection.capabilities & DG_CAP_DRAWABLE_READ_RECTS)) {
		DGLog("present: partly covered window needs DG_CAP_DRAWABLE_READ_RECTS, showing the whole window");
		vis = 1;
	}
	if (vis)
		return PresentRect(c, &t, 0, 0, w, h, diag);
	if (diag)
		DGVLog("present #%lu: %d visible rectangle(s)", sSwapCount, n);
	for (i = 0; i < n; i++)
		if (PresentRect(c, &t, rects[i].left, rects[i].top, rects[i].right - rects[i].left,
		    rects[i].bottom - rects[i].top, diag && i == 0))
			return -1;
	return 0;
}

static void Impl_aglSwapBuffers(AGLContext ctx)
{
	if (!ctx || !ctx->port || !ctx->drawable) {
		SetError(ctx ? AGL_BAD_DRAWABLE : AGL_BAD_CONTEXT);
		return;
	}
	if (!ApplyDrawable(ctx, 0))		/* the window was resized */
		return;
	if (dg_connection.dead)
		return;
	if (Present(ctx))
		SetError(AGL_BAD_DRAWABLE);
}

/* --- verbose logging (OpenGLLibrary.debug in the System Folder, see dg_log.c)
 * The entry points are thin wrappers over Impl_agl*: when verbose, each call
 * logs its arguments and result on one line, ending with the pending AGL
 * error (gAglError, left unread). */
#define ERR ((unsigned long)gAglError)
#define UL(x) ((unsigned long)(x))

/* Attributes that carry a value in aglChoosePixelFormat's list (as above). */
static int AttribTakesValue(GLint attr)
{
	switch (attr) {
	case AGL_BUFFER_SIZE: case AGL_LEVEL: case AGL_AUX_BUFFERS:
	case AGL_RED_SIZE: case AGL_GREEN_SIZE: case AGL_BLUE_SIZE: case AGL_ALPHA_SIZE:
	case AGL_DEPTH_SIZE: case AGL_STENCIL_SIZE:
	case AGL_ACCUM_RED_SIZE: case AGL_ACCUM_GREEN_SIZE: case AGL_ACCUM_BLUE_SIZE: case AGL_ACCUM_ALPHA_SIZE:
	case AGL_PIXEL_SIZE: case AGL_SAMPLE_BUFFERS_ARB: case AGL_SAMPLES_ARB:
	case AGL_RENDERER_ID: case AGL_VIRTUAL_SCREEN:
		return 1;
	}
	return 0;
}

void aglGetVersion(GLint *major, GLint *minor)
{
	Impl_aglGetVersion(major, minor);
	DGVLog("aglGetVersion -> %d.%d", major ? (int)*major : -1, minor ? (int)*minor : -1);
}

AGLPixelFormat aglChoosePixelFormat(const AGLDevice *gdevs, GLint ndev, const GLint *attribs)
{
	AGLPixelFormat r;

	if (DGVerbose()) {
		const GLint *a = attribs;
		int n = 0;

		DGVLog("aglChoosePixelFormat(ndev %d, gdevs %lx, attribs %lx)", (int)ndev, UL(gdevs), UL(attribs));
		while (a && *a != AGL_NONE && n++ < 64) {
			GLint attr = *a++;

			if (AttribTakesValue(attr))
				DGVLog("  attrib %d = %d", (int)attr, (int)*a++);
			else
				DGVLog("  attrib %d", (int)attr);
		}
		DGVLog("  (%s)", a ? "AGL_NONE" : "no list");
	}
	r = Impl_aglChoosePixelFormat(gdevs, ndev, attribs);
	DGVLog("aglChoosePixelFormat -> %lx err %lx", UL(r), ERR);
	return r;
}

void aglDestroyPixelFormat(AGLPixelFormat pix)
{
	DGVLog("aglDestroyPixelFormat(%lx)", UL(pix));
	Impl_aglDestroyPixelFormat(pix);
}

GLboolean aglDescribePixelFormat(AGLPixelFormat pix, GLint attrib, GLint *value)
{
	GLboolean r = Impl_aglDescribePixelFormat(pix, attrib, value);

	DGVLog("aglDescribePixelFormat(%lx, attrib %d) -> %d value %d err %lx", UL(pix), (int)attrib, (int)r,
		r && value ? (int)*value : -1, ERR);
	return r;
}

AGLRendererInfo aglQueryRendererInfo(const AGLDevice *gdevs, GLint ndev)
{
	AGLRendererInfo r = Impl_aglQueryRendererInfo(gdevs, ndev);

	DGVLog("aglQueryRendererInfo(gdevs %lx, ndev %d) -> %lx err %lx", UL(gdevs), (int)ndev, UL(r), ERR);
	return r;
}

void aglDestroyRendererInfo(AGLRendererInfo rend)
{
	DGVLog("aglDestroyRendererInfo(%lx)", UL(rend));
	Impl_aglDestroyRendererInfo(rend);
}

AGLRendererInfo aglNextRendererInfo(AGLRendererInfo rend)
{
	AGLRendererInfo r = Impl_aglNextRendererInfo(rend);

	DGVLog("aglNextRendererInfo(%lx) -> %lx", UL(rend), UL(r));
	return r;
}

GLboolean aglDescribeRenderer(AGLRendererInfo rend, GLint prop, GLint *value)
{
	GLboolean r = Impl_aglDescribeRenderer(rend, prop, value);

	DGVLog("aglDescribeRenderer(%lx, prop %d) -> %d value %d err %lx", UL(rend), (int)prop, (int)r,
		r && value ? (int)*value : -1, ERR);
	return r;
}

/* Classic RAVE passes RAVE z (0..1 by the spec) straight through as GL depth,
 * and games put background layers slightly outside it (Blokz: menu panel and
 * buttons at z 1.09-1.10). ATI's RAVE engines clamp such z, host GL clips the
 * geometry at the far plane, so those layers vanished. Contexts created by
 * Classic RAVE therefore get depth clamping (z clamped, nothing clipped).
 * Detected by the caller's return address lying near Classic RAVE's code
 * (its rgl_initialize entry; the whole fragment is ~70 KB). */
static int FromClassicRAVE(const void *ra)
{
	ConnectionID conn;
	Ptr mainAddr, sym;
	Str255 err;
	SymClass cls;
	unsigned long code;

	if (GetSharedLibrary("\pClassic RAVE", kPowerPCArch, kFindLib, &conn, &mainAddr, err) != noErr)
		return 0;
	if (FindSymbol(conn, "\prgl_initialize", &sym, &cls) != noErr || !sym)
		return 0;
	code = *(unsigned long *)sym;		/* TVector: code address first */
	return (unsigned long)ra - code + 0x20000UL < 0x40000UL;
}

AGLContext aglCreateContext(AGLPixelFormat pix, AGLContext share)
{
	const void *ra = __builtin_return_address(0);
	AGLContext r = Impl_aglCreateContext(pix, share);

	if (r && FromClassicRAVE(ra)) {
		r->classic_rave = 1;
		DGLog("aglCreateContext from Classic RAVE: depth clamp on");
	}

	DGVLog("aglCreateContext(pix %lx, share %lx) -> %lx err %lx", UL(pix), UL(share), UL(r), ERR);
	return r;
}

GLboolean aglDestroyContext(AGLContext ctx)
{
	GLboolean r;

	DGVLog("aglDestroyContext(%lx)", UL(ctx));
	r = Impl_aglDestroyContext(ctx);
	DGVLog("  -> %d err %lx", (int)r, ERR);
	return r;
}

GLboolean aglSetCurrentContext(AGLContext ctx)
{
	GLboolean r = Impl_aglSetCurrentContext(ctx);

	DGVLog("aglSetCurrentContext(%lx) -> %d", UL(ctx), (int)r);
	return r;
}

AGLContext aglGetCurrentContext(void)
{
	AGLContext r = Impl_aglGetCurrentContext();

	DGVLog("aglGetCurrentContext -> %lx", UL(r));
	return r;
}

GLenum aglGetError(void)
{
	GLenum r = Impl_aglGetError();

	DGVLog("aglGetError -> %lx", UL(r));
	return r;
}

const GLubyte *aglErrorString(GLenum code)
{
	const GLubyte *r = Impl_aglErrorString(code);

	DGVLog("aglErrorString(%lx) -> \"%s\"", UL(code), (const char *)r);
	return r;
}

GLboolean aglSetDrawable(AGLContext ctx, AGLDrawable draw)
{
	GLboolean r = Impl_aglSetDrawable(ctx, draw);

	DGVLog("aglSetDrawable(%lx, port %lx) -> %d err %lx", UL(ctx), UL(draw), (int)r, ERR);
	return r;
}

GLboolean aglUpdateContext(AGLContext ctx)
{
	GLboolean r = Impl_aglUpdateContext(ctx);

	DGVLog("aglUpdateContext(%lx) -> %d err %lx", UL(ctx), (int)r, ERR);
	return r;
}

GLboolean aglSetFullScreen(AGLContext ctx, GLsizei width, GLsizei height, GLsizei freq, GLint device)
{
	GLboolean r = Impl_aglSetFullScreen(ctx, width, height, freq, device);

	DGVLog("aglSetFullScreen(%lx, %d x %d, freq %d, device %d) -> %d err %lx", UL(ctx), (int)width, (int)height,
		(int)freq, (int)device, (int)r, ERR);
	return r;
}

GLboolean aglEnable(AGLContext ctx, GLenum pname)
{
	GLboolean r = Impl_aglEnable(ctx, pname);

	DGVLog("aglEnable(%lx, %lx) -> %d err %lx", UL(ctx), UL(pname), (int)r, ERR);
	return r;
}

GLboolean aglDisable(AGLContext ctx, GLenum pname)
{
	GLboolean r = Impl_aglDisable(ctx, pname);

	DGVLog("aglDisable(%lx, %lx) -> %d err %lx", UL(ctx), UL(pname), (int)r, ERR);
	return r;
}

GLboolean aglIsEnabled(AGLContext ctx, GLenum pname)
{
	GLboolean r = Impl_aglIsEnabled(ctx, pname);

	DGVLog("aglIsEnabled(%lx, %lx) -> %d err %lx", UL(ctx), UL(pname), (int)r, ERR);
	return r;
}

GLboolean aglSetInteger(AGLContext ctx, GLenum pname, const GLint *params)
{
	GLboolean r = Impl_aglSetInteger(ctx, pname, params);

	DGVLog("aglSetInteger(%lx, %lx, [%d]) -> %d err %lx", UL(ctx), UL(pname), params ? (int)params[0] : -1,
		(int)r, ERR);
	return r;
}

GLboolean aglGetInteger(AGLContext ctx, GLenum pname, GLint *params)
{
	GLboolean r = Impl_aglGetInteger(ctx, pname, params);

	DGVLog("aglGetInteger(%lx, %lx) -> %d value %d err %lx", UL(ctx), UL(pname), (int)r,
		r && params ? (int)params[0] : -1, ERR);
	return r;
}

AGLPixelFormat aglNextPixelFormat(AGLPixelFormat pix)
{
	AGLPixelFormat r = Impl_aglNextPixelFormat(pix);

	DGVLog("aglNextPixelFormat(%lx) -> %lx", UL(pix), UL(r));
	return r;
}

AGLDevice *aglDevicesOfPixelFormat(AGLPixelFormat pix, GLint *ndevs)
{
	AGLDevice *r = Impl_aglDevicesOfPixelFormat(pix, ndevs);

	DGVLog("aglDevicesOfPixelFormat(%lx) -> %lx n %d err %lx", UL(pix), UL(r), ndevs ? (int)*ndevs : -1, ERR);
	return r;
}

GLboolean aglCopyContext(AGLContext src, AGLContext dst, GLuint mask)
{
	GLboolean r = Impl_aglCopyContext(src, dst, mask);

	DGVLog("aglCopyContext(%lx, %lx, mask %lx) -> %d err %lx", UL(src), UL(dst), UL(mask), (int)r, ERR);
	return r;
}

GLboolean aglSetOffScreen(AGLContext ctx, GLsizei width, GLsizei height, GLsizei rowbytes, void *baseaddr)
{
	GLboolean r = Impl_aglSetOffScreen(ctx, width, height, rowbytes, baseaddr);

	DGVLog("aglSetOffScreen(%lx, %d x %d, rowbytes %d, base %lx) -> %d err %lx", UL(ctx), (int)width, (int)height,
		(int)rowbytes, UL(baseaddr), (int)r, ERR);
	return r;
}

AGLDrawable aglGetDrawable(AGLContext ctx)
{
	AGLDrawable r = Impl_aglGetDrawable(ctx);

	DGVLog("aglGetDrawable(%lx) -> %lx", UL(ctx), UL(r));
	return r;
}

GLboolean aglSetVirtualScreen(AGLContext ctx, GLint screen)
{
	GLboolean r = Impl_aglSetVirtualScreen(ctx, screen);

	DGVLog("aglSetVirtualScreen(%lx, %d) -> %d err %lx", UL(ctx), (int)screen, (int)r, ERR);
	return r;
}

GLint aglGetVirtualScreen(AGLContext ctx)
{
	GLint r = Impl_aglGetVirtualScreen(ctx);

	DGVLog("aglGetVirtualScreen(%lx) -> %d", UL(ctx), (int)r);
	return r;
}

GLboolean aglConfigure(GLenum pname, GLuint param)
{
	GLboolean r = Impl_aglConfigure(pname, param);

	DGVLog("aglConfigure(%lx, %lx) -> %d err %lx", UL(pname), UL(param), (int)r, ERR);
	return r;
}

GLboolean aglUseFont(AGLContext ctx, GLint fontID, Style face, GLint size, GLint first, GLint count, GLint base)
{
	GLboolean r = Impl_aglUseFont(ctx, fontID, face, size, first, count, base);

	DGVLog("aglUseFont(%lx, font %d, face %d, size %d, first %d, count %d, base %d) -> %d err %lx", UL(ctx),
		(int)fontID, (int)face, (int)size, (int)first, (int)count, (int)base, (int)r, ERR);
	return r;
}

void aglResetLibrary(void)
{
	DGVLog("aglResetLibrary");
	Impl_aglResetLibrary();
}

void aglSwapBuffers(AGLContext ctx)
{
	/* per frame: logged only the first time, to keep the log small */
	static int sFirst = 1;

	if (sFirst) {
		DGVLog("aglSwapBuffers(%lx) (first call; later ones not logged)", UL(ctx));
		sFirst = 0;
	}
	Impl_aglSwapBuffers(ctx);
}
