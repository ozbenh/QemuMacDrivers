/* UTM RAVE: draw contexts. Creation and deletion, the AGL drawable (a
 * CGrafPort that gives the size, the place on screen and the visible region),
 * state variables, frame methods (RenderStart/End, Flush, Sync, clears,
 * notice methods) and the memory-device read-back.
 *
 * Presentation. OpenGLLibrary's agl.c presents a context at aglSwapBuffers by
 * reading the host drawable back into the main screen at the position of the
 * drawable's CGrafPort (LocalToGlobal of its portRect) and clipped to the
 * port's visRgn. So for a GDevice context the engine opens a CGrafPort whose
 * portRect is the RAVE rect and whose portBits are moved to the rect's global
 * position, and sets its visRgn to the rect intersected with the TQAClip
 * region (global coordinates; copied once at creation: RAVE applications
 * recreate the draw context when the clip changes). A memory-device context
 * uses the same port only for its size: it is never swapped, RenderEnd reads
 * the frame back (glReadPixels) and converts it into the application's
 * buffer.
 *
 * Coordinates. RAVE x, y are pixels with y down; the projection is
 * glOrtho(0, w, h, 0, 0, -1), so vertices map 1:1 and a vertex z in [0, 1] is
 * the GL depth (ndc = 2z - 1). Depth clamping (GL_DEPTH_CLAMP) keeps z outside
 * [0, 1] from being clipped, as OpenGLLibrary does for Classic RAVE contexts. */
#include <MacTypes.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <string.h>
#include "UTMRAVEEngine.h"
#include "UTMRAVELog.h"

#define MAX_CONTEXT_SIZE 8192		/* agl.c refuses larger drawables */

/* ---- port */

/* Opens the drawable port for a context at global position (gx, gy). */
static CGrafPtr OpenDrawablePort(int gx, int gy, long w, long h, const TQAClip *clip)
{
	CGrafPtr port = (CGrafPtr)NewPtrClear(sizeof(CGrafPort));
	GrafPtr save;
	Rect r;

	if (!port)
		return NULL;
	GetPort(&save);
	OpenCPort(port);		/* makes the new port current */
	MovePortTo((short)gx, (short)gy);
	PortSize((short)w, (short)h);
	r = port->portRect;
	RectRgn(port->visRgn, &r);
	if (clip && clip->clipType == kQAClipRgn && clip->clip.clipRgn && *clip->clip.clipRgn) {
		RgnHandle local = NewRgn();

		if (local) {
			Rect bbox = (**clip->clip.clipRgn).rgnBBox;

			CopyRgn(clip->clip.clipRgn, local);
			OffsetRgn(local, (short)-gx, (short)-gy);	/* global to the port's local coordinates */
			SectRgn(port->visRgn, local, port->visRgn);
			DisposeRgn(local);
			UTMRAVELog("  clip region bbox global (%d,%d,%d,%d)", (int)bbox.left, (int)bbox.top, (int)bbox.right,
				(int)bbox.bottom);
		} else
			UTMRAVELog("  no memory for the clip region: drawing unclipped");
	}
	SetPort(save);
	return port;
}

static void ClosePortAndFree(CGrafPtr port)
{
	if (!port)
		return;
	CloseCPort(port);
	DisposePtr((Ptr)port);
}

/* ---- creation and deletion */

TQAError UTMRAVEDrawPrivateNew(TQADrawContext *dc, const TQADevice *device, const TQARect *rect,
	const TQAClip *clip, unsigned long flags)
{
	TQADrawPrivate *c;
	AGLContext shared;
	AGLPixelFormat fmt;
	long w, h;
	int gx = 0, gy = 0;
	TQAError err;

	err = UTMRAVEEngineCheckDevice(device);
	if (err != kQANoErr) {
		UTMRAVELog("DrawPrivateNew: device rejected (%d)", (int)err);
		return err;
	}
	if (!rect)
		return kQAParamErr;
	w = rect->right - rect->left;
	h = rect->bottom - rect->top;
	UTMRAVELog("DrawPrivateNew: device %d rect (%ld,%ld,%ld,%ld) = %ld x %ld flags %lx clip %s", (int)device->deviceType,
		rect->left, rect->top, rect->right, rect->bottom, w, h, flags, clip ? "yes" : "none");
	if (w < 1 || h < 1 || w > MAX_CONTEXT_SIZE || h > MAX_CONTEXT_SIZE) {
		UTMRAVELog("DrawPrivateNew: size %ld x %ld out of range", w, h);
		return kQAParamErr;
	}
	if (flags & kQAContext_Scale)
		UTMRAVELog("DrawPrivateNew: kQAContext_Scale not supported, drawing unscaled");
	shared = UTMRAVEShared();
	fmt = UTMRAVEPixelFormat();
	if (!shared || !fmt)
		return kQAError;
	c = (TQADrawPrivate *)NewPtrClear(sizeof *c);
	if (!c)
		return kQAOutOfMemory;
	c->device = *device;
	c->isMemory = device->deviceType == kQADeviceMemory;
	c->width = w;
	c->height = h;
	c->flags = flags;
	c->drawContext = dc;
	if (c->isMemory) {
		/* the rect's origin only selects where in the buffer RenderEnd writes (memLeft/memTop) */
		c->memLeft = rect->left;
		c->memTop = rect->top;
	} else {
		GDHandle gd = device->device.gDevice;

		gx = (**gd).gdRect.left + (int)rect->left;
		gy = (**gd).gdRect.top + (int)rect->top;
	}
	c->port = OpenDrawablePort(gx, gy, w, h, c->isMemory ? NULL : clip);
	if (!c->port) {
		DisposePtr((Ptr)c);
		return kQAOutOfMemory;
	}
	c->agl = gGL.aglCreateContext(fmt, shared);
	if (!c->agl) {
		UTMRAVELog("DrawPrivateNew: aglCreateContext failed: AGL error %d", (int)gGL.aglGetError());
		ClosePortAndFree(c->port);
		DisposePtr((Ptr)c);
		return kQAError;
	}
	if (!gGL.aglSetDrawable(c->agl, c->port)) {
		UTMRAVELog("DrawPrivateNew: aglSetDrawable failed: AGL error %d", (int)gGL.aglGetError());
		gGL.aglDestroyContext(c->agl);
		ClosePortAndFree(c->port);
		DisposePtr((Ptr)c);
		return kQAError;
	}
	UTMRAVEForgetCurrent();
	UTMRAVEMakeCurrent(c->agl);
	gGL.glViewport(0, 0, (GLsizei)w, (GLsizei)h);
	gGL.glMatrixMode(GL_PROJECTION);
	gGL.glLoadIdentity();
	gGL.glOrtho(0.0, (GLdouble)w, (GLdouble)h, 0.0, 0.0, -1.0);
	gGL.glMatrixMode(GL_MODELVIEW);
	gGL.glLoadIdentity();
	gGL.glEnable(GL_DEPTH_CLAMP);
	gGL.glDisable(GL_DITHER);		/* the engine never dithers (gestalt: NoDither) */
	UTMRAVEStateDefaults(c);
	c->dirty = UTM_DIRTY_ALL;
	dc->drawPrivate = c;
	UTMRAVEInstallStateMethods(dc);
	UTMRAVEInstallDrawMethods(dc);
	UTMRAVEInstallFrameMethods(dc);
	UTMRAVELinkContext(c);
	return kQANoErr;
}

void UTMRAVEDrawPrivateDelete(TQADrawPrivate *c)
{
	if (!c)
		return;
	UTMRAVELog("DrawPrivateDelete %lx", (unsigned long)c);
	UTMRAVEUnlinkContext(c);
	if (c->agl) {
		gGL.aglSetDrawable(c->agl, NULL);
		if (UTMRAVECurrentContext() == c->agl)
			UTMRAVEForgetCurrent();
		gGL.aglDestroyContext(c->agl);
	}
	ClosePortAndFree(c->port);
	if (c->rgba)
		DisposePtr((Ptr)c->rgba);
	if (c->frame)
		DisposePtr((Ptr)c->frame);
	if (c->meshG)
		DisposePtr((Ptr)c->meshG);
	if (c->meshT)
		DisposePtr((Ptr)c->meshT);
	DisposePtr((Ptr)c);
}

/* ---- state variables */

void UTMRAVEStateDefaults(TQADrawPrivate *c)
{
	int i;

	for (i = 0; i < UTM_MAX_TAG; i++)
		c->state[i].i = 0;
	c->state[kQATag_ZFunction].i = (c->flags & kQAContext_NoZBuffer) ? kQAZFunction_None : kQAZFunction_LT;
	c->state[kQATag_Width].f = 1.0f;
	c->state[kQATag_ZMinOffset].f = 0.0f;
	c->state[kQATag_ZMinScale].f = 1.0f / 65535.0f;		/* conservative for the 24-bit buffer */
	c->state[kQATag_Antialias].i = kQAAntiAlias_Fast;
	c->state[kQATag_Blend].i = kQABlend_PreMultiply;
	c->state[kQATag_PerspectiveZ].i = kQAPerspectiveZ_Off;
	c->state[kQATag_TextureFilter].i = kQATextureFilter_Fast;
	c->state[kQATag_TextureOp].i = kQATextureOp_None;
	c->state[kQATag_Texture].p = NULL;
	c->state[kQATag_ChannelMask].i = kQAChannelMask_r | kQAChannelMask_g | kQAChannelMask_b | kQAChannelMask_a;
	c->state[kQATag_ZBufferMask].i = kQAZBufferMask_Enable;
	c->state[kQATag_AlphaTestFunc].i = kQAAlphaTest_None;
	c->state[kQATag_BitmapScale_x].f = 1.0f;
	c->state[kQATag_BitmapScale_y].f = 1.0f;
	c->state[kQATag_BitmapFilter].i = kQAFilter_Fast;
	c->state[kQATagGL_TextureWrapU].i = kQAGL_Repeat;
	c->state[kQATagGL_TextureWrapV].i = kQAGL_Repeat;
	c->state[kQATagGL_ScissorXMin].i = 0;
	c->state[kQATagGL_ScissorYMin].i = 0;
	c->state[kQATagGL_ScissorXMax].i = (unsigned long)c->width;
	c->state[kQATagGL_ScissorYMax].i = (unsigned long)c->height;
	c->state[kQATagGL_BlendSrc].i = GL_SRC_ALPHA;
	c->state[kQATagGL_BlendDst].i = GL_ONE_MINUS_SRC_ALPHA;
	c->state[kQATagGL_LinePattern].i = 0xFFFF;
	c->state[kQATagGL_LinePatternFactor].i = 1;
	for (i = kQATagGL_AreaPattern0; i <= kQATagGL_AreaPattern31; i++)
		c->state[i].i = 0xFFFFFFFFUL;
	c->state[kQATagGL_DepthBG].f = 1.0f;
	c->state[kQATag_FogEnd].f = 1.0f;
	c->state[kQATag_FogDensity].f = 1.0f;
}

static unsigned DirtyFor(long tag)
{
	switch (tag) {
	case kQATag_ZFunction: case kQATag_ZBufferMask:
		return UTM_DIRTY_DEPTH;
	case kQATag_Blend: case kQATagGL_BlendSrc: case kQATagGL_BlendDst:
		return UTM_DIRTY_BLEND;
	case kQATag_AlphaTestFunc: case kQATag_AlphaTestRef:
		return UTM_DIRTY_ALPHA;
	case kQATag_ChannelMask:
		return UTM_DIRTY_MASK;
	case kQATagGL_ScissorXMin: case kQATagGL_ScissorYMin: case kQATagGL_ScissorXMax: case kQATagGL_ScissorYMax:
		return UTM_DIRTY_SCISSOR;
	case kQATag_Width: case kQATagGL_LinePattern: case kQATagGL_LinePatternFactor:
		return UTM_DIRTY_LINE;
	default:
		if (tag >= kQATagGL_AreaPattern0 && tag <= kQATagGL_AreaPattern31)
			return UTM_DIRTY_LINE;
		return 0;
	}
}

/* Integer tags whose non-zero values select a feature this engine does not
 * implement: logged once per tag, the value is still stored and readable. */
static const char *UnimplementedIntTag(long tag, unsigned long value)
{
	if (!value)
		return NULL;
	switch (tag) {
	case kQATag_Antialias:
		return value == kQAAntiAlias_Fast ? NULL : "kQATag_Antialias (antialiasing)";	/* Fast is the default */
	case kQATag_CSGTag: return "kQATag_CSGTag";
	case kQATag_CSGEquation: return "kQATag_CSGEquation";
	case kQATag_FogMode: return "kQATag_FogMode (fog)";
	case kQATag_ChromakeyEnable: return "kQATag_ChromakeyEnable";
	case kQATag_MultiTextureEnable: return "kQATag_MultiTextureEnable (multitexture)";
	case kQATagGL_DrawBuffer: return "kQATagGL_DrawBuffer";
	default: return NULL;
	}
}

static void SetFloat(TQADrawContext *dc, TQATagFloat tag, float value)
{
	TQADrawPrivate *c = dc->drawPrivate;

	if ((unsigned long)tag >= UTM_MAX_TAG) {
		if (UTMRAVEFirst(kUTMFirstTag, (unsigned long)tag))
			UTMRAVELog("SetFloat: tag %ld out of range", (long)tag);
		return;
	}
	c->state[tag].f = value;
	c->dirty |= DirtyFor(tag);
}

static void SetInt(TQADrawContext *dc, TQATagInt tag, unsigned long value)
{
	TQADrawPrivate *c = dc->drawPrivate;
	const char *what;

	if ((unsigned long)tag >= UTM_MAX_TAG) {
		if (UTMRAVEFirst(kUTMFirstTag, (unsigned long)tag))
			UTMRAVELog("SetInt: tag %ld out of range", (long)tag);
		return;
	}
	c->state[tag].i = value;
	c->dirty |= DirtyFor(tag);
	if (tag == kQATag_TextureFilter)
		c->glFilterSet = 0;		/* the generic filter now decides */
	else if (tag == kQATagGL_TextureMagFilter || tag == kQATagGL_TextureMinFilter)
		c->glFilterSet = 1;		/* the explicit GL filters decide */
	else if (tag == kQATagGL_TextureWrapU || tag == kQATagGL_TextureWrapV)
		c->glWrapSet = 1;		/* the explicit GL wrap modes decide */
	what = UnimplementedIntTag(tag, value);
	if (what && UTMRAVEFirst(kUTMFirstTag, (unsigned long)tag))
		UTMRAVELog("SetInt: %s = %lu is not implemented", what, value);
}

static void SetPtr(TQADrawContext *dc, TQATagPtr tag, const void *value)
{
	TQADrawPrivate *c = dc->drawPrivate;

	if ((unsigned long)tag >= UTM_MAX_TAG) {
		if (UTMRAVEFirst(kUTMFirstTag, (unsigned long)tag))
			UTMRAVELog("SetPtr: tag %ld out of range", (long)tag);
		return;
	}
	if (tag == kQATag_MultiTexture && value && UTMRAVEFirst(kUTMFirstTag, (unsigned long)tag))
		UTMRAVELog("SetPtr: kQATag_MultiTexture is not implemented");
	c->state[tag].p = value;
}

static float GetFloat(const TQADrawContext *dc, TQATagFloat tag)
{
	const TQADrawPrivate *c = dc->drawPrivate;

	return (unsigned long)tag < UTM_MAX_TAG ? c->state[tag].f : 0.0f;
}

static unsigned long GetInt(const TQADrawContext *dc, TQATagInt tag)
{
	const TQADrawPrivate *c = dc->drawPrivate;

	return (unsigned long)tag < UTM_MAX_TAG ? c->state[tag].i : 0;
}

static void *GetPtr(const TQADrawContext *dc, TQATagPtr tag)
{
	const TQADrawPrivate *c = dc->drawPrivate;

	return (unsigned long)tag < UTM_MAX_TAG ? (void *)c->state[tag].p : NULL;
}

void UTMRAVEInstallStateMethods(TQADrawContext *dc)
{
	dc->setFloat = SetFloat;
	dc->setInt = SetInt;
	dc->setPtr = SetPtr;
	dc->getFloat = GetFloat;
	dc->getInt = GetInt;
	dc->getPtr = GetPtr;
}

/* ---- GL state */

void UTMRAVESetCap(TQADrawPrivate *c, int cap, GLenum glCap, int on)
{
	unsigned bit = 1u << cap;

	if ((c->capsKnown & bit) && ((c->capsOn & bit) != 0) == (on != 0))
		return;
	if (on) {
		gGL.glEnable(glCap);
		c->capsOn |= bit;
	} else {
		gGL.glDisable(glCap);
		c->capsOn &= ~bit;
	}
	c->capsKnown |= bit;
}

static GLenum CompareFunc(unsigned long f)
{
	switch (f) {
	case 1: return GL_LESS;
	case 2: return GL_EQUAL;
	case 3: return GL_LEQUAL;
	case 4: return GL_GREATER;
	case 5: return GL_NOTEQUAL;
	case 6: return GL_GEQUAL;
	case 7: return GL_ALWAYS;
	case 8: return GL_NEVER;
	default: return GL_ALWAYS;
	}
}

/* kQATagGL_BlendSrc/Dst carry OpenGL blend factor enums. */
static GLenum BlendFactor(unsigned long v, GLenum fallback)
{
	switch (v) {
	case GL_ZERO: case GL_ONE: case GL_SRC_COLOR: case GL_ONE_MINUS_SRC_COLOR: case GL_SRC_ALPHA:
	case GL_ONE_MINUS_SRC_ALPHA: case GL_DST_ALPHA: case GL_ONE_MINUS_DST_ALPHA: case GL_DST_COLOR:
	case GL_ONE_MINUS_DST_COLOR: case GL_SRC_ALPHA_SATURATE:
		return (GLenum)v;
	default:
		if (UTMRAVEFirst(kUTMFirstMisc, 10 + (v & 15)))
			UTMRAVELog("blend factor %lx is not an OpenGL blend factor, using %x", v, (unsigned)fallback);
		return fallback;
	}
}

void UTMRAVEApplyState(TQADrawPrivate *c)
{
	unsigned long d = c->dirty;
	const UTMState *s = c->state;

	if (!d)
		return;
	c->dirty = 0;
	if (d & UTM_DIRTY_DEPTH) {
		unsigned long fn = (c->flags & kQAContext_NoZBuffer) ? kQAZFunction_None : s[kQATag_ZFunction].i;

		UTMRAVESetCap(c, kUTMCapDepth, GL_DEPTH_TEST, fn != kQAZFunction_None);
		if (fn != kQAZFunction_None)
			gGL.glDepthFunc(CompareFunc(fn));
		gGL.glDepthMask(s[kQATag_ZBufferMask].i != kQAZBufferMask_Disable);
	}
	if (d & UTM_DIRTY_BLEND) {
		switch (s[kQATag_Blend].i) {
		case kQABlend_Interpolate:
			gGL.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			break;
		case kQABlend_OpenGL:
			gGL.glBlendFunc(BlendFactor(s[kQATagGL_BlendSrc].i, GL_SRC_ALPHA),
				BlendFactor(s[kQATagGL_BlendDst].i, GL_ONE_MINUS_SRC_ALPHA));
			break;
		default:			/* kQABlend_PreMultiply */
			gGL.glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			break;
		}
	}
	if (d & UTM_DIRTY_ALPHA) {
		unsigned long fn = s[kQATag_AlphaTestFunc].i;

		/* Without an explicit kQATag_AlphaTestFunc, fragments with alpha 0 are
		 * discarded (no colour, no depth), as Apple's Classic RAVE does with
		 * glEnable(GL_ALPHA_TEST) + glAlphaFunc(GL_GREATER, 0) on every
		 * context: transparent texels of overlapping glyph quads must not
		 * write depth. */
		UTMRAVESetCap(c, kUTMCapAlpha, GL_ALPHA_TEST, 1);
		if (fn != kQAAlphaTest_None)
			gGL.glAlphaFunc(CompareFunc(fn), s[kQATag_AlphaTestRef].f);
		else
			gGL.glAlphaFunc(GL_GREATER, 0.0f);
	}
	if (d & UTM_DIRTY_MASK) {
		unsigned long m = s[kQATag_ChannelMask].i;

		gGL.glColorMask((m & kQAChannelMask_r) != 0, (m & kQAChannelMask_g) != 0, (m & kQAChannelMask_b) != 0,
			(m & kQAChannelMask_a) != 0);
	}
	if (d & UTM_DIRTY_SCISSOR) {
		long x0 = (long)s[kQATagGL_ScissorXMin].i, y0 = (long)s[kQATagGL_ScissorYMin].i;
		long x1 = (long)s[kQATagGL_ScissorXMax].i, y1 = (long)s[kQATagGL_ScissorYMax].i;
		int whole;

		if (x0 < 0)
			x0 = 0;
		if (y0 < 0)
			y0 = 0;
		if (x1 > c->width)
			x1 = c->width;
		if (y1 > c->height)
			y1 = c->height;
		whole = x0 == 0 && y0 == 0 && x1 == c->width && y1 == c->height;
		UTMRAVESetCap(c, kUTMCapScissor, GL_SCISSOR_TEST, !whole);
		if (!whole) {
			if (x1 < x0)
				x1 = x0;
			if (y1 < y0)
				y1 = y0;
			/* RAVE y is down, GL's window origin is the bottom left */
			gGL.glScissor((GLint)x0, (GLint)(c->height - y1), (GLsizei)(x1 - x0), (GLsizei)(y1 - y0));
		}
	}
	if (d & UTM_DIRTY_LINE) {
		float width = s[kQATag_Width].f;
		unsigned long pattern = s[kQATagGL_LinePattern].i & 0xFFFF, factor = s[kQATagGL_LinePatternFactor].i;
		unsigned char stipple[128];
		int i, any = 0;

		if (width < 1.0f)
			width = 1.0f;
		gGL.glLineWidth(width);
		gGL.glPointSize(width);
		UTMRAVESetCap(c, kUTMCapLineStipple, GL_LINE_STIPPLE, pattern != 0xFFFF);
		if (pattern != 0xFFFF)
			gGL.glLineStipple(factor < 1 ? 1 : (GLint)factor, (GLushort)pattern);
		for (i = 0; i < 32; i++) {
			unsigned long row = s[kQATagGL_AreaPattern0 + i].i;

			if (row != 0xFFFFFFFFUL)
				any = 1;
			stipple[i * 4] = (unsigned char)(row >> 24);
			stipple[i * 4 + 1] = (unsigned char)(row >> 16);
			stipple[i * 4 + 2] = (unsigned char)(row >> 8);
			stipple[i * 4 + 3] = (unsigned char)row;
		}
		UTMRAVESetCap(c, kUTMCapPolyStipple, GL_POLYGON_STIPPLE, any);
		if (any)
			gGL.glPolygonStipple(stipple);
	}
}

/* ---- frame transfers */

/* GL RGBA, bottom-up, in c->rgba; 0 when memory is short. */
static int ReadFrameRGBA(TQADrawPrivate *c)
{
	if (!c->rgba) {
		c->rgba = (unsigned char *)NewPtr(c->width * c->height * 4);
		if (!c->rgba) {
			UTMRAVELog("frame read-back: out of memory for %ld x %ld", c->width, c->height);
			return 0;
		}
	}
	gGL.glReadPixels(0, 0, (GLsizei)c->width, (GLsizei)c->height, GL_RGBA, GL_UNSIGNED_BYTE, c->rgba);
	return 1;
}

/* Memory device: converts c->rgba into the application's buffer (big-endian
 * pixels), top row first. RGB32/RGB16 leave the alpha byte/bit 0/clear. */
static void WriteMemoryDevice(TQADrawPrivate *c)
{
	const TQADeviceMemory *m = &c->device.device.memoryDevice;
	int bytes = (m->pixelType == kQAPixel_ARGB32 || m->pixelType == kQAPixel_RGB32) ? 4 : 2;
	long y, x, w = c->width, h = c->height;
	long maxW = m->width - c->memLeft, maxH = m->height - c->memTop;

	if (w > maxW)
		w = maxW;
	if (h > maxH)
		h = maxH;
	for (y = 0; y < h; y++) {
		const unsigned char *s = c->rgba + (c->height - 1 - y) * c->width * 4;
		unsigned char *d = (unsigned char *)m->baseAddr + (c->memTop + y) * m->rowBytes + c->memLeft * bytes;

		for (x = 0; x < w; x++, s += 4, d += bytes) {
			switch (m->pixelType) {
			case kQAPixel_ARGB32:
				d[0] = s[3]; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
				break;
			case kQAPixel_RGB32:
				d[0] = 0; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
				break;
			default: {		/* ARGB16 / RGB16: 1-5-5-5 */
				unsigned v = ((s[0] >> 3) << 10) | ((s[1] >> 3) << 5) | (s[2] >> 3);

				if (m->pixelType == kQAPixel_ARGB16 && s[3] >= 128)
					v |= 0x8000;
				d[0] = (unsigned char)(v >> 8);
				d[1] = (unsigned char)v;
				break;
			}
			}
		}
	}
}

/* c->rgba (bottom-up RGBA) <-> c->frame (top-down ARGB32), for the notice
 * methods that read and write the image. */
static int FrameFromGL(TQADrawPrivate *c)
{
	long x, y, w = c->width, h = c->height;

	if (!c->frame) {
		c->frame = (unsigned char *)NewPtr(w * h * 4);
		if (!c->frame) {
			UTMRAVELog("frame buffer: out of memory for %ld x %ld", w, h);
			return 0;
		}
	}
	if (!ReadFrameRGBA(c))
		return 0;
	for (y = 0; y < h; y++) {
		const unsigned char *s = c->rgba + (h - 1 - y) * w * 4;
		unsigned char *d = c->frame + y * w * 4;

		for (x = 0; x < w; x++, s += 4, d += 4) {
			d[0] = s[3]; d[1] = s[0]; d[2] = s[1]; d[3] = s[2];
		}
	}
	return 1;
}

/* Sends c->frame into the GL color buffer (no depth, no blending). */
static void FrameToGL(TQADrawPrivate *c)
{
	long x, y, w = c->width, h = c->height;

	for (y = 0; y < h; y++) {
		const unsigned char *s = c->frame + y * w * 4;
		unsigned char *d = c->rgba + (h - 1 - y) * w * 4;

		for (x = 0; x < w; x++, s += 4, d += 4) {
			d[0] = s[1]; d[1] = s[2]; d[2] = s[3]; d[3] = s[0];
		}
	}
	UTMRAVESetCap(c, kUTMCapDepth, GL_DEPTH_TEST, 0);
	UTMRAVESetCap(c, kUTMCapBlend, GL_BLEND, 0);
	UTMRAVESetCap(c, kUTMCapAlpha, GL_ALPHA_TEST, 0);
	UTMRAVESetCap(c, kUTMCapScissor, GL_SCISSOR_TEST, 0);
	UTMRAVESetCap(c, kUTMCapTexture, GL_TEXTURE_2D, 0);
	UTMRAVESetCap(c, kUTMCapColorSum, GL_COLOR_SUM, 0);
	UTMRAVESetCap(c, kUTMCapPolyStipple, GL_POLYGON_STIPPLE, 0);
	gGL.glColorMask(1, 1, 1, 1);
	gGL.glRasterPos2f(0.0f, (GLfloat)h);	/* bottom left in the y-down projection */
	gGL.glDrawPixels((GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, c->rgba);
	c->dirty = UTM_DIRTY_ALL;
}

/* Calls a buffer notice method with the context's ARGB32 frame buffer. */
static void CallBufferNotice(TQADrawPrivate *c, int selector, const TQARect *rect)
{
	TQADevice buffer;
	TQARect whole;

	buffer.deviceType = kQADeviceMemory;
	buffer.device.memoryDevice.rowBytes = c->width * 4;
	buffer.device.memoryDevice.pixelType = kQAPixel_ARGB32;
	buffer.device.memoryDevice.width = c->width;
	buffer.device.memoryDevice.height = c->height;
	buffer.device.memoryDevice.baseAddr = c->frame;
	if (!rect) {
		whole.left = 0;
		whole.top = 0;
		whole.right = c->width;
		whole.bottom = c->height;
		rect = &whole;
	}
	c->notice[selector].bufferNoticeMethod(c->drawContext, &buffer, rect, c->noticeRefCon[selector]);
}

/* ---- frame methods */

static void ClearBuffers(TQADrawPrivate *c, const TQARect *r, int color, int depth)
{
	GLbitfield mask = 0;
	long x0 = 0, y0 = 0, x1 = c->width, y1 = c->height;

	if (r) {
		x0 = r->left < 0 ? 0 : r->left;
		y0 = r->top < 0 ? 0 : r->top;
		x1 = r->right > c->width ? c->width : r->right;
		y1 = r->bottom > c->height ? c->height : r->bottom;
		if (x1 <= x0 || y1 <= y0)
			return;
	}
	if (color) {
		gGL.glColorMask(1, 1, 1, 1);
		gGL.glClearColor(c->state[kQATag_ColorBG_r].f, c->state[kQATag_ColorBG_g].f, c->state[kQATag_ColorBG_b].f,
			c->state[kQATag_ColorBG_a].f);
		mask |= GL_COLOR_BUFFER_BIT;
	}
	if (depth) {
		gGL.glDepthMask(1);
		gGL.glClearDepth((GLdouble)c->state[kQATagGL_DepthBG].f);
		mask |= GL_DEPTH_BUFFER_BIT;
	}
	if (x0 == 0 && y0 == 0 && x1 == c->width && y1 == c->height)
		UTMRAVESetCap(c, kUTMCapScissor, GL_SCISSOR_TEST, 0);
	else {
		UTMRAVESetCap(c, kUTMCapScissor, GL_SCISSOR_TEST, 1);
		gGL.glScissor((GLint)x0, (GLint)(c->height - y1), (GLsizei)(x1 - x0), (GLsizei)(y1 - y0));
	}
	gGL.glClear(mask);
	c->dirty |= UTM_DIRTY_MASK | UTM_DIRTY_DEPTH | UTM_DIRTY_SCISSOR;	/* re-sent before the next primitive */
}

static void RenderStart(const TQADrawContext *dc, const TQARect *dirty, const TQADrawContext *initial)
{
	TQADrawPrivate *c = dc->drawPrivate;

	UTMRAVEForgetCurrent();			/* the application may have used AGL since the last frame */
	UTMRAVEMakeCurrent(c->agl);
	if (initial && UTMRAVEFirst(kUTMFirstMisc, 20))
		UTMRAVELog("RenderStart: initialContext is not supported, ignored");
	if (c->notice[kQAMethod_ImageBufferInitialize].bufferNoticeMethod) {
		/* the application supplies the starting image: hand it a background-filled ARGB32 buffer */
		unsigned char a = (unsigned char)(c->state[kQATag_ColorBG_a].f * 255.0f + 0.5f);
		unsigned char r = (unsigned char)(c->state[kQATag_ColorBG_r].f * 255.0f + 0.5f);
		unsigned char g = (unsigned char)(c->state[kQATag_ColorBG_g].f * 255.0f + 0.5f);
		unsigned char b = (unsigned char)(c->state[kQATag_ColorBG_b].f * 255.0f + 0.5f);
		long i;

		if (!c->frame)
			c->frame = (unsigned char *)NewPtr(c->width * c->height * 4);
		if (c->frame && (c->rgba || (c->rgba = (unsigned char *)NewPtr(c->width * c->height * 4)))) {
			for (i = 0; i < c->width * c->height; i++) {
				c->frame[i * 4] = a; c->frame[i * 4 + 1] = r; c->frame[i * 4 + 2] = g; c->frame[i * 4 + 3] = b;
			}
			CallBufferNotice(c, kQAMethod_ImageBufferInitialize, dirty);
			FrameToGL(c);
			ClearBuffers(c, dirty, 0, 1);
			return;
		}
		UTMRAVELog("RenderStart: out of memory for the initial image buffer, clearing instead");
	}
	ClearBuffers(c, dirty, 1, 1);
}

static TQAError Present(TQADrawPrivate *c)
{
	GLenum err;

	if (c->isMemory) {
		if (!ReadFrameRGBA(c))
			return kQAOutOfMemory;
		WriteMemoryDevice(c);
		return kQANoErr;
	}
	gGL.aglGetError();			/* clear an old error: report only this swap's */
	gGL.aglSwapBuffers(c->agl);
	err = gGL.aglGetError();
	if (err) {
		if (UTMRAVEFirst(kUTMFirstMisc, 21))
			UTMRAVELog("aglSwapBuffers failed: AGL error %d", (int)err);
		return kQAError;
	}
	return kQANoErr;
}

static void CompletionNotice(TQADrawPrivate *c)
{
	if (c->notice[kQAMethod_RenderCompletion].standardNoticeMethod)
		c->notice[kQAMethod_RenderCompletion].standardNoticeMethod(c->drawContext,
			c->noticeRefCon[kQAMethod_RenderCompletion]);
}

static TQAError RenderEnd(const TQADrawContext *dc, const TQARect *modified)
{
	TQADrawPrivate *c = dc->drawPrivate;
	TQAError err;

	(void)modified;			/* the whole frame is presented */
	UTMRAVEMakeCurrent(c->agl);
	if (c->notice[kQAMethod_ImageBuffer2DComposite].bufferNoticeMethod) {
		if (FrameFromGL(c)) {
			CallBufferNotice(c, kQAMethod_ImageBuffer2DComposite, NULL);
			FrameToGL(c);
		} else
			UTMRAVELog("RenderEnd: composite notice skipped (out of memory)");
	}
	if (c->state[kQATag_DontSwap].i && !c->isMemory) {
		gGL.glFlush();
		return kQANoErr;
	}
	err = Present(c);
	if (err == kQANoErr)
		CompletionNotice(c);
	return err;
}

static TQAError RenderAbort(const TQADrawContext *dc)
{
	TQADrawPrivate *c = dc->drawPrivate;

	UTMRAVEMakeCurrent(c->agl);
	gGL.glFlush();
	return kQANoErr;
}

static TQAError Flush(const TQADrawContext *dc)
{
	TQADrawPrivate *c = dc->drawPrivate;

	UTMRAVEMakeCurrent(c->agl);
	gGL.glFlush();
	return kQANoErr;
}

static TQAError Sync(const TQADrawContext *dc)
{
	TQADrawPrivate *c = dc->drawPrivate;

	UTMRAVEMakeCurrent(c->agl);
	gGL.glFinish();
	return kQANoErr;
}

static TQABoolean Busy(const TQADrawContext *dc)
{
	(void)dc;
	return 0;			/* rendering is synchronous from the application's side */
}

static TQAError SwapBuffers(const TQADrawContext *dc, const TQARect *dirty)
{
	TQADrawPrivate *c = dc->drawPrivate;
	TQAError err;

	(void)dirty;
	UTMRAVEMakeCurrent(c->agl);
	err = Present(c);
	if (err == kQANoErr)
		CompletionNotice(c);
	return err;
}

static TQAError ClearDrawBuffer(const TQADrawContext *dc, const TQARect *rect, const TQADrawContext *initial)
{
	TQADrawPrivate *c = dc->drawPrivate;

	if (initial && UTMRAVEFirst(kUTMFirstMisc, 22))
		UTMRAVELog("ClearDrawBuffer: initialContext is not supported, ignored");
	UTMRAVEMakeCurrent(c->agl);
	ClearBuffers(c, rect, 1, 0);
	return kQANoErr;
}

static TQAError ClearZBuffer(const TQADrawContext *dc, const TQARect *rect, const TQADrawContext *initial)
{
	TQADrawPrivate *c = dc->drawPrivate;

	(void)initial;
	UTMRAVEMakeCurrent(c->agl);
	ClearBuffers(c, rect, 0, 1);
	return kQANoErr;
}

static TQAError SetNoticeMethod(const TQADrawContext *dc, TQAMethodSelector method, TQANoticeMethod callBack,
	void *refCon)
{
	TQADrawPrivate *c = dc->drawPrivate;

	if ((unsigned long)method >= kQAMethod_NumSelectors)
		return kQAParamErr;
	if ((method == kQAMethod_DisplayModeChanged || method == kQAMethod_ReloadTextures) &&
	    callBack.standardNoticeMethod && UTMRAVEFirst(kUTMFirstMisc, 30 + (unsigned long)method))
		UTMRAVELog("SetNoticeMethod: selector %d is stored but never called", (int)method);
	c->notice[method] = callBack;
	c->noticeRefCon[method] = refCon;
	return kQANoErr;
}

static TQAError GetNoticeMethod(const TQADrawContext *dc, TQAMethodSelector method, TQANoticeMethod *callBack,
	void **refCon)
{
	const TQADrawPrivate *c = dc->drawPrivate;

	if ((unsigned long)method >= kQAMethod_NumSelectors || !callBack || !refCon)
		return kQAParamErr;
	*callBack = c->notice[method];
	*refCon = c->noticeRefCon[method];
	return kQANoErr;
}

/* Direct access to the draw buffer / z buffer, and textures made from a draw
 * context: the buffers live on the host. Not claimed in gestalt. */
static TQAError NotSupported(const char *what, int id)
{
	if (UTMRAVEFirst(kUTMFirstMethod, id))
		UTMRAVELog("%s is not supported", what);
	return kQANotSupported;
}

static TQAError AccessDrawBuffer(const TQADrawContext *dc, TQAPixelBuffer *b)
{
	(void)dc; (void)b;
	return NotSupported("QAAccessDrawBuffer", 1);
}

static TQAError AccessDrawBufferEnd(const TQADrawContext *dc, const TQARect *r)
{
	(void)dc; (void)r;
	return NotSupported("QAAccessDrawBufferEnd", 2);
}

static TQAError AccessZBuffer(const TQADrawContext *dc, TQAZBuffer *b)
{
	(void)dc; (void)b;
	return NotSupported("QAAccessZBuffer", 3);
}

static TQAError AccessZBufferEnd(const TQADrawContext *dc, const TQARect *r)
{
	(void)dc; (void)r;
	return NotSupported("QAAccessZBufferEnd", 4);
}

static TQAError TextureFromContext(const TQADrawContext *dc, unsigned long flags, TQATexture **t)
{
	(void)dc; (void)flags;
	if (t)
		*t = NULL;
	return NotSupported("QATextureNewFromDrawContext", 5);
}

static TQAError BitmapFromContext(const TQADrawContext *dc, unsigned long flags, TQABitmap **b)
{
	(void)dc; (void)flags;
	if (b)
		*b = NULL;
	return NotSupported("QABitmapNewFromDrawContext", 6);
}

void UTMRAVEInstallFrameMethods(TQADrawContext *dc)
{
	dc->renderStart = RenderStart;
	dc->renderEnd = RenderEnd;
	dc->renderAbort = RenderAbort;
	dc->flush = Flush;
	dc->sync = Sync;
	dc->setNoticeMethod = SetNoticeMethod;
	dc->getNoticeMethod = GetNoticeMethod;
	dc->accessDrawBuffer = AccessDrawBuffer;
	dc->accessDrawBufferEnd = AccessDrawBufferEnd;
	dc->accessZBuffer = AccessZBuffer;
	dc->accessZBufferEnd = AccessZBufferEnd;
	dc->clearDrawBuffer = ClearDrawBuffer;
	dc->clearZBuffer = ClearZBuffer;
	dc->textureFromContext = TextureFromContext;
	dc->bitmapFromContext = BitmapFromContext;
	dc->busy = Busy;
	dc->swapBuffers = SwapBuffers;
}
