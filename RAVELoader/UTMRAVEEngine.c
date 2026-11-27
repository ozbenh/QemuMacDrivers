/* UTM RAVE: a QuickDraw 3D RAVE 1.6 drawing engine for Mac OS 9 that renders
 * through OpenGLLibrary (the paravirtual AGL + OpenGL of this project) instead
 * of Apple's Classic RAVE.
 *
 * This file: registration with the RAVE manager, the engine method table,
 * gestalt, device check, and the AGL objects shared by the whole engine.
 *
 * Structure follows Apple's "Empty Engine" sample (Tinsel Town, Mike W.
 * Kelley, (C) 1994-95 Apple Computer, Inc.; RAVE 0.9 era): registration with
 * QARegisterEngine, a TQAEngineGetMethod that hands the manager one method
 * per tag, DrawPrivateNew filling the TQADrawContext method table, state
 * variables kept in a tag-indexed array. It is extended here to the RAVE 1.6
 * method set (RAVE.h / RAVESystem.h of Apple's Universal Interfaces).
 *
 * Design decisions:
 *  - One AGL context per RAVE draw context. Textures, bitmaps and their GL
 *    names live in one "shared" AGL context without a drawable, created on
 *    first use; every draw context is created sharing with it (so a texture
 *    made before any draw context exists is usable by all of them).
 *  - The OpenGLLibrary entry points are found at run time (UTMRAVEGL.c): there
 *    is no link-time import, so the loader works without our library.
 *  - The engine tracks the current AGL context itself (UTMRAVEMakeCurrent) and
 *    re-asserts it at every RenderStart.
 *  - Failures are logged (UTMRAVELog, only with OpenGLLibrary.debug in the
 *    System Folder), never silent; unsupported tags, methods and pixel types
 *    are logged once each. */
#include <MacTypes.h>
#include <Quickdraw.h>
#include <string.h>
#include "UTMRAVEEngine.h"
#include "UTMRAVELog.h"

static AGLPixelFormat gFormat;
static AGLContext gShared;
static AGLContext gCurrent;
static int gFormatTried;
static TQADrawPrivate *gContexts;	/* draw contexts alive in this process */

/* The pixel format every context uses: RGBA, double buffered, 24-bit depth.
 * Chosen on first use, not at load: the extension is loaded into every
 * process, and choosing a format attaches to the paravirtual device. NULL
 * (logged) when the device has no GL stream. */
AGLPixelFormat UTMRAVEPixelFormat(void)
{
	static const GLint attribs[] = { AGL_RGBA, AGL_DOUBLEBUFFER, AGL_DEPTH_SIZE, 24, AGL_NONE };

	if (!gFormat && !gFormatTried) {
		gFormatTried = 1;
		gFormat = gGL.aglChoosePixelFormat(NULL, 0, attribs);
		if (!gFormat)
			UTMRAVELog("aglChoosePixelFormat failed: AGL error %d (no PVMAC GL stream?)", (int)gGL.aglGetError());
	}
	return gFormat;
}

AGLContext UTMRAVEShared(void)
{
	AGLPixelFormat fmt;

	if (gShared)
		return gShared;
	fmt = UTMRAVEPixelFormat();
	if (!fmt)
		return NULL;
	gShared = gGL.aglCreateContext(fmt, NULL);
	if (!gShared)
		UTMRAVELog("aglCreateContext (shared texture context) failed: AGL error %d", (int)gGL.aglGetError());
	return gShared;
}

void UTMRAVEMakeCurrent(AGLContext ctx)
{
	if (gCurrent == ctx)
		return;
	gGL.aglSetCurrentContext(ctx);
	gCurrent = ctx;
}

AGLContext UTMRAVECurrentContext(void)
{
	return gCurrent;
}

void UTMRAVEForgetCurrent(void)
{
	gCurrent = NULL;
}

/* ---- engine methods */

/* Memory devices: ARGB16, ARGB32, RGB16, RGB32 (big-endian, as the RAVE
 * pixel layouts say). GDevices: 16- and 32-bit, and only the main device:
 * the paravirtual present (OpenGLLibrary's agl.c) writes to the main screen. */
TQAError UTMRAVEEngineCheckDevice(const TQADevice *device)
{
	if (!device)
		return kQAParamErr;
	if (device->deviceType == kQADeviceMemory) {
		const TQADeviceMemory *m = &device->device.memoryDevice;

		switch (m->pixelType) {
		case kQAPixel_ARGB16: case kQAPixel_ARGB32: case kQAPixel_RGB16: case kQAPixel_RGB32:
			break;
		default:
			if (UTMRAVEFirst(kUTMFirstPixelType, m->pixelType))
				UTMRAVELog("CheckDevice: memory device pixel type %d rejected", (int)m->pixelType);
			return kQAError;
		}
		if (!m->baseAddr || m->width < 1 || m->height < 1 || m->rowBytes < 1)
			return kQAParamErr;
	} else if (device->deviceType == kQADeviceGDevice) {
		GDHandle gd = device->device.gDevice;
		int depth;

		if (!gd || !*gd)
			return kQAParamErr;
		if (gd != GetMainDevice()) {
			if (UTMRAVEFirst(kUTMFirstMisc, 1))
				UTMRAVELog("CheckDevice: GDevice %lx is not the main device, rejected", (unsigned long)gd);
			return kQAError;
		}
		depth = (**(**gd).gdPMap).pixelSize;
		if (depth != 16 && depth != 32) {
			if (UTMRAVEFirst(kUTMFirstMisc, 2))
				UTMRAVELog("CheckDevice: %d-bit GDevice rejected", depth);
			return kQAError;
		}
	} else {
		if (UTMRAVEFirst(kUTMFirstMisc, 3))
			UTMRAVELog("CheckDevice: device type %d rejected", (int)device->deviceType);
		return kQAError;
	}
	if (!UTMRAVEPixelFormat())
		return kQAError;
	return kQANoErr;
}

#define PIXELBIT(t) (1UL << (t))
#define TEXTURE_TYPES (PIXELBIT(kQAPixel_RGB16) | PIXELBIT(kQAPixel_ARGB16) | PIXELBIT(kQAPixel_RGB32) | \
	PIXELBIT(kQAPixel_ARGB32) | PIXELBIT(kQAPixel_CL4) | PIXELBIT(kQAPixel_CL8) | PIXELBIT(kQAPixel_RGB16_565) | \
	PIXELBIT(kQAPixel_RGB24) | PIXELBIT(kQAPixel_RGB8_332) | PIXELBIT(kQAPixel_ARGB16_4444) | \
	PIXELBIT(kQAPixel_ACL16_88) | PIXELBIT(kQAPixel_I8) | PIXELBIT(kQAPixel_AI16_88))

/* Exactly what is implemented (see UTMRAVEDraw.c / UTMRAVEContext.c). Not
 * claimed: Antialias, ZSorted, CSG, FogAlpha/FogDepth, MultiTextures, Chromakey,
 * Access*, TextureDrawContexts/BitmapDrawContexts, DrawContextScale, NoCopy,
 * PriorityBits, NonRelocatable. */
#define OPTIONAL_FEATURES (kQAOptional_DeepZ | kQAOptional_Texture | kQAOptional_TextureHQ | \
	kQAOptional_TextureColor | kQAOptional_Blend | kQAOptional_BlendAlpha | kQAOptional_PerspectiveZ | \
	kQAOptional_OpenGL | kQAOptional_CL4 | kQAOptional_CL8 | kQAOptional_BufferComposite | \
	kQAOptional_NoDither | kQAOptional_MipmapBias | kQAOptional_ChannelMask | kQAOptional_ZBufferMask | \
	kQAOptional_AlphaTest | kQAOptional_ClearDrawBuffer | kQAOptional_ClearZBuffer)
#define OPTIONAL_FEATURES2 (kQAOptional2_Busy | kQAOptional2_SwapBuffers | kQAOptional2_FlipOrigin | \
	kQAOptional2_BitmapScale)
#define FAST_FEATURES (kQAFast_Line | kQAFast_Gouraud | kQAFast_Texture | kQAFast_TextureHQ | kQAFast_Blend | \
	kQAFast_CL4 | kQAFast_CL8 | kQAFast_BitmapScale)

TQAError UTMRAVEEngineGestalt(TQAGestaltSelector selector, void *response)
{
	static int logged;

	if (!response)
		return kQAParamErr;
	switch (selector) {
	case kQAGestalt_OptionalFeatures: *(unsigned long *)response = OPTIONAL_FEATURES; break;
	case kQAGestalt_OptionalFeatures2: *(unsigned long *)response = OPTIONAL_FEATURES2; break;
	case kQAGestalt_FastFeatures: *(unsigned long *)response = FAST_FEATURES; break;
	case kQAGestalt_VendorID: *(long *)response = kUTMRAVEVendorID; break;
	case kQAGestalt_EngineID: *(long *)response = kUTMRAVEEngineID; break;
	case kQAGestalt_Revision: *(long *)response = kUTMRAVERevision; break;
	case kQAGestalt_ASCIINameLength: *(long *)response = (long)strlen(kUTMRAVEName); break;
	case kQAGestalt_ASCIIName: strcpy((char *)response, kUTMRAVEName); break;
	case kQAGestalt_TextureMemory:
	case kQAGestalt_FastTextureMemory:
		*(long *)response = 64L * 1024L * 1024L;	/* host textures: the figure OpenGLLibrary reports as VRAM */
		break;
	case kQAGestalt_DrawContextPixelTypesAllowed:
		*(unsigned long *)response = PIXELBIT(kQAPixel_ARGB16) | PIXELBIT(kQAPixel_ARGB32) |
			PIXELBIT(kQAPixel_RGB16) | PIXELBIT(kQAPixel_RGB32);
		break;
	case kQAGestalt_DrawContextPixelTypesPreferred:
		*(unsigned long *)response = PIXELBIT(kQAPixel_ARGB32);
		break;
	case kQAGestalt_TexturePixelTypesAllowed: *(unsigned long *)response = TEXTURE_TYPES; break;
	case kQAGestalt_TexturePixelTypesPreferred:
		*(unsigned long *)response = PIXELBIT(kQAPixel_ARGB32) | PIXELBIT(kQAPixel_RGB32);
		break;
	case kQAGestalt_BitmapPixelTypesAllowed: *(unsigned long *)response = TEXTURE_TYPES | PIXELBIT(kQAPixel_Alpha1); break;
	case kQAGestalt_BitmapPixelTypesPreferred: *(unsigned long *)response = PIXELBIT(kQAPixel_Alpha1); break;
	case kQAGestalt_MultiTextureMax: *(long *)response = 0; break;	/* no secondary texture layers */
	default:
		if (UTMRAVEFirst(kUTMFirstMisc, 100 + (unsigned long)selector))
			UTMRAVELog("Gestalt: unknown selector %d", (int)selector);
		return kQAGestaltUnknown;
	}
	if (!logged && selector == kQAGestalt_OptionalFeatures) {
		logged = 1;
		UTMRAVELog("Gestalt: optional %lx optional2 %lx fast %lx", (unsigned long)OPTIONAL_FEATURES,
			(unsigned long)OPTIONAL_FEATURES2, (unsigned long)FAST_FEATURES);
	}
	return kQANoErr;
}

TQAError UTMRAVEEngineGetMethod(TQAEngineMethodTag tag, TQAEngineMethod *method)
{
	switch (tag) {
	case kQADrawPrivateNew: method->drawPrivateNew = UTMRAVEDrawPrivateNew; break;
	case kQADrawPrivateDelete: method->drawPrivateDelete = UTMRAVEDrawPrivateDelete; break;
	case kQAEngineCheckDevice: method->engineCheckDevice = UTMRAVEEngineCheckDevice; break;
	case kQAEngineGestalt: method->engineGestalt = UTMRAVEEngineGestalt; break;
	case kQATextureNew: method->textureNew = UTMRAVETextureNew; break;
	case kQATextureDetach: method->textureDetach = UTMRAVETextureDetach; break;
	case kQATextureDelete: method->textureDelete = UTMRAVETextureDelete; break;
	case kQABitmapNew: method->bitmapNew = UTMRAVEBitmapNew; break;
	case kQABitmapDetach: method->bitmapDetach = UTMRAVEBitmapDetach; break;
	case kQABitmapDelete: method->bitmapDelete = UTMRAVEBitmapDelete; break;
	case kQAColorTableNew: method->colorTableNew = UTMRAVEColorTableNew; break;
	case kQAColorTableDelete: method->colorTableDelete = UTMRAVEColorTableDelete; break;
	case kQATextureBindColorTable: method->textureBindColorTable = UTMRAVETextureBindColorTable; break;
	case kQABitmapBindColorTable: method->bitmapBindColorTable = UTMRAVEBitmapBindColorTable; break;
	case kQAAccessTexture: method->accessTexture = UTMRAVEAccessTexture; break;
	case kQAAccessTextureEnd: method->accessTextureEnd = UTMRAVEAccessTextureEnd; break;
	case kQAAccessBitmap: method->accessBitmap = UTMRAVEAccessBitmap; break;
	case kQAAccessBitmapEnd: method->accessBitmapEnd = UTMRAVEAccessBitmapEnd; break;
	default:
		if (UTMRAVEFirst(kUTMFirstMethod, 200 + (unsigned long)tag))
			UTMRAVELog("GetMethod: unknown engine method tag %d", (int)tag);
		return kQAParamErr;
	}
	return kQANoErr;
}

int UTMRAVEEngineInit(CFragConnectionID lib)
{
	if (!UTMRAVEGLResolve(lib))
		return 0;
	return 1;
}

/* Draw contexts register themselves here so that Term can release them. */
void UTMRAVELinkContext(TQADrawPrivate *c)
{
	c->next = gContexts;
	gContexts = c;
}

void UTMRAVEUnlinkContext(TQADrawPrivate *c)
{
	TQADrawPrivate **pp;

	for (pp = &gContexts; *pp; pp = &(*pp)->next)
		if (*pp == c) {
			*pp = c->next;
			return;
		}
}

void UTMRAVEEngineTerm(void)
{
	while (gContexts) {
		UTMRAVELog("Term: destroying draw context %lx", (unsigned long)gContexts);
		UTMRAVEDrawPrivateDelete(gContexts);	/* unlinks */
	}
	if (gShared) {
		gGL.aglDestroyContext(gShared);
		gShared = NULL;
	}
	if (gFormat) {
		gGL.aglDestroyPixelFormat(gFormat);
		gFormat = NULL;
	}
	gCurrent = NULL;
}
