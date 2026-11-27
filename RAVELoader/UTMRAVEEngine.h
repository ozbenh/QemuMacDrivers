/* UTM RAVE engine: internal declarations shared by its modules.
 *
 * The engine draws RAVE 1.6 through OpenGLLibrary (AGL + GL over the
 * paravirtual wire) instead of Apple's Classic RAVE. See UTMRAVEEngine.c for
 * the overview and the design decisions. */
#ifndef UTMRAVEENGINE_H
#define UTMRAVEENGINE_H
#include <MacTypes.h>
#include <Quickdraw.h>
#include <CodeFragments.h>
#include <RAVE.h>
#include <RAVESystem.h>
#include "UTMRAVEGL.h"

/* Our own vendor and engine ids. RAVE vendor ids are assigned by Apple; none
 * of the known ones (Apple, ATI, 3dfx, ...) uses an ASCII code, so 'UTM '
 * cannot collide with them. */
#define kUTMRAVEVendorID 0x55544D20L	/* 'UTM ' */
#define kUTMRAVEEngineID 1L
#define kUTMRAVERevision 1L
#define kUTMRAVEName "UTM RAVE"

/* ---- engine (UTMRAVEEngine.c) */

/* Resolves the GL table in `lib` and creates the pixel format. Returns 1 when
 * the engine can be registered; logs and returns 0 otherwise. */
int UTMRAVEEngineInit(CFragConnectionID lib);

/* The engine's TQAEngineGetMethod, passed to QARegisterEngine. */
TQAError UTMRAVEEngineGetMethod(TQAEngineMethodTag tag, TQAEngineMethod *method);

/* Destroys every draw context and the shared context still alive. */
void UTMRAVEEngineTerm(void);

/* Makes `ctx` the current AGL context (a no-op when it already is) and
 * returns 1; the engine tracks the current context itself so that it does not
 * call aglSetCurrentContext for every primitive. */
void UTMRAVEMakeCurrent(AGLContext ctx);
/* The AGL context the engine last made current (NULL when none or forgotten). */
AGLContext UTMRAVECurrentContext(void);
/* Forgets the tracked current context: the next MakeCurrent always calls AGL.
 * Used at the start of each frame in case the application used AGL itself. */
void UTMRAVEForgetCurrent(void);

/* The context textures and bitmaps live in; draw contexts share with it. The
 * first call creates it (NULL on failure, logged). */
AGLContext UTMRAVEShared(void);
AGLPixelFormat UTMRAVEPixelFormat(void);

/* Draw contexts register here so UTMRAVEEngineTerm can release them. */
void UTMRAVELinkContext(TQADrawPrivate *c);
void UTMRAVEUnlinkContext(TQADrawPrivate *c);

/* ---- textures, bitmaps, color tables (UTMRAVETexture.c) */

#define UTM_MAX_LEVELS 13		/* mipmap levels of a 4096 texture */

struct UTMImage {
	GLuint name;			/* host texture object (shared context) */
	long width, height;
	int levels;			/* images supplied: 1, or the mipmap pyramid */
	int mipmapped;			/* kQATexture_Mipmap */
	int isBitmap;
	int hasAlpha;			/* some texel is not opaque */
	int uploaded;			/* the host texture has its images */
	int flip;			/* kQATexture_FlipOrigin / kQABitmap_FlipOrigin */
	TQAImagePixelType pixelType;
	TQAColorTable *table;		/* the table being converted (CL types), set only during an upload */
	unsigned char *raw[UTM_MAX_LEVELS];	/* index data kept until the table is bound (CL types) */
	/* sampler state last sent to the host texture object */
	int paramsValid;
	GLint wrapS, wrapT, mag, min;
	float border[4];
};
typedef struct UTMImage UTMImage;

/* 1 when the type needs a color table before it can be converted. */
int UTMRAVEPixelNeedsTable(TQAImagePixelType type);

/* Applies the sampler parameters to the bound texture when they changed. */
void UTMRAVEImageParams(UTMImage *img, GLint wrapS, GLint wrapT, GLint mag, GLint min, const float border[4]);

TQAError UTMRAVETextureNew(unsigned long flags, TQAImagePixelType pixelType, const TQAImage images[],
	TQATexture **newTexture);
TQAError UTMRAVETextureDetach(TQATexture *texture);
void UTMRAVETextureDelete(TQATexture *texture);
TQAError UTMRAVEBitmapNew(unsigned long flags, TQAImagePixelType pixelType, const TQAImage *image,
	TQABitmap **newBitmap);
TQAError UTMRAVEBitmapDetach(TQABitmap *bitmap);
void UTMRAVEBitmapDelete(TQABitmap *bitmap);
TQAError UTMRAVEColorTableNew(TQAColorTableType type, void *pixelData, long transparentIndex,
	TQAColorTable **newTable);
void UTMRAVEColorTableDelete(TQAColorTable *table);
TQAError UTMRAVETextureBindColorTable(TQATexture *texture, TQAColorTable *table);
TQAError UTMRAVEBitmapBindColorTable(TQABitmap *bitmap, TQAColorTable *table);
TQAError UTMRAVEAccessTexture(TQATexture *texture, long mipmapLevel, long flags, TQAPixelBuffer *buffer);
TQAError UTMRAVEAccessTextureEnd(TQATexture *texture, const TQARect *dirtyRect);
TQAError UTMRAVEAccessBitmap(TQABitmap *bitmap, long flags, TQAPixelBuffer *buffer);
TQAError UTMRAVEAccessBitmapEnd(TQABitmap *bitmap, const TQARect *dirtyRect);

/* ---- draw contexts (UTMRAVEContext.c, UTMRAVEDraw.c) */

#define UTM_MAX_TAG 160			/* state variables kept: tags 0..153 are defined */

typedef union {
	unsigned long i;
	float f;
	const void *p;
} UTMState;

/* Groups of GL state to re-send before the next primitive. */
#define UTM_DIRTY_DEPTH 0x01		/* ZFunction, ZBufferMask */
#define UTM_DIRTY_BLEND 0x02		/* Blend, BlendSrc/Dst */
#define UTM_DIRTY_ALPHA 0x04		/* AlphaTestFunc/Ref */
#define UTM_DIRTY_MASK 0x08		/* ChannelMask */
#define UTM_DIRTY_SCISSOR 0x10		/* Scissor tags */
#define UTM_DIRTY_LINE 0x20		/* Width, line and area patterns */
#define UTM_DIRTY_ALL 0x3F

/* Capabilities whose enable state the context caches (bit = index). */
enum {
	kUTMCapDepth, kUTMCapBlend, kUTMCapAlpha, kUTMCapScissor, kUTMCapTexture, kUTMCapColorSum,
	kUTMCapLineStipple, kUTMCapPolyStipple, kUTMCaps
};

struct TQADrawPrivate {
	struct TQADrawPrivate *next;	/* all live contexts, for UTMRAVEEngineTerm */
	TQADrawContext *drawContext;
	AGLContext agl;
	CGrafPtr port;			/* AGL drawable: gives the size, the place on screen and the visible region */
	TQADevice device;
	int isMemory;
	long memLeft, memTop;		/* memory device: where the rect starts in the buffer */
	long width, height;
	unsigned long flags;		/* kQAContext_xxx */
	UTMState state[UTM_MAX_TAG];
	unsigned long dirty;
	unsigned capsKnown, capsOn;	/* cached glEnable state */
	int glFilterSet;		/* a kQATagGL_Texture{Mag,Min}Filter was set after kQATag_TextureFilter */
	int glWrapSet;			/* the application set kQATagGL_TextureWrapU/V */
	/* how the textured primitive being drawn uses its vertex colors (set by the draw setup) */
	int texDecal, texModulate, texHighlight;
	/* cached texture environment */
	GLint envMode;
	float envColor[4];
	float lodBias;
	/* notice methods */
	TQANoticeMethod notice[kQAMethod_NumSelectors];
	void *noticeRefCon[kQAMethod_NumSelectors];
	/* frame transfer buffers, made on first use (width * height * 4 bytes) */
	unsigned char *rgba;		/* GL side: RGBA, bottom-up */
	unsigned char *frame;		/* RAVE side: ARGB32 big-endian, top-down, handed to notice methods */
	/* vertices stored by QASubmitVertices*, for the trimesh calls */
	TQAVGouraud *meshG;
	unsigned long meshGCount, meshGCap;
	TQAVTexture *meshT;
	unsigned long meshTCount, meshTCap;
};

TQAError UTMRAVEDrawPrivateNew(TQADrawContext *newDrawContext, const TQADevice *device, const TQARect *rect,
	const TQAClip *clip, unsigned long flags);
void UTMRAVEDrawPrivateDelete(TQADrawPrivate *drawPrivate);
TQAError UTMRAVEEngineCheckDevice(const TQADevice *device);
TQAError UTMRAVEEngineGestalt(TQAGestaltSelector selector, void *response);

/* Fills in every method of a context (UTMRAVEContext.c, UTMRAVEDraw.c). */
void UTMRAVEInstallStateMethods(TQADrawContext *dc);
void UTMRAVEInstallDrawMethods(TQADrawContext *dc);
void UTMRAVEInstallFrameMethods(TQADrawContext *dc);
/* Default state variable values (UTMRAVEContext.c). */
void UTMRAVEStateDefaults(TQADrawPrivate *c);

/* Cached glEnable / glDisable. */
void UTMRAVESetCap(TQADrawPrivate *c, int cap, GLenum glCap, int on);
/* Sends the dirty groups of state variables to GL. */
void UTMRAVEApplyState(TQADrawPrivate *c);

#endif
