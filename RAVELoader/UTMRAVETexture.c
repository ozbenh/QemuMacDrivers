/* UTM RAVE: textures, bitmaps and color tables.
 *
 * Every RAVE pixel type is converted here to RGBA8 bytes (R, G, B, A in
 * memory order) and uploaded as GL_RGBA / GL_UNSIGNED_BYTE, which crosses the
 * paravirtual wire without endian issues. Texture objects live in the
 * engine's shared AGL context (UTMRAVEShared), so all draw contexts of the
 * process see them.
 *
 * Copy semantics: the image data is converted and uploaded inside
 * QATextureNew / QABitmapNew; the application's buffer is not referenced
 * afterwards, so QATextureDetach / QABitmapDetach have nothing left to copy,
 * and kQATexture_NoCopy and kQATexture_Lock change nothing (neither is
 * claimed in gestalt). The exception is a color-table type (CL4, CL8,
 * ACL16_88): its index data cannot be converted before a table is bound
 * (QATextureBindColorTable), so the engine keeps its own copy of the indices
 * and uploads when the table is bound (or at Detach if it was bound before).
 * Row order: images are uploaded top row first, as Apple's Classic RAVE
 * does; RAVE's v (0 at the image's bottom) becomes GL t = 1 - v at draw time
 * (UTMRAVEDraw.c). kQATexture_FlipOrigin / kQABitmap_FlipOrigin images are
 * bottom-up, so their rows are reversed to the same top-down order. */
#include <MacTypes.h>
#include <Memory.h>
#include <string.h>
#include "UTMRAVEEngine.h"
#include "UTMRAVELog.h"

#define MAX_TEXTURE_SIZE 4096

struct TQAColorTable {
	int entries;			/* 16 or 256 */
	int transparentIndex0;		/* index 0 is transparent */
	unsigned long rgb[256];		/* 0x00RRGGBB */
};

/* A conversion buffer: application heap, or temporary memory when the heap
 * cannot hold it (textures can be megabytes). */
typedef struct {
	unsigned char *p;
	Handle temp;
} TempBuf;

static int TempBufNew(TempBuf *b, long n)
{
	OSErr err;

	b->temp = NULL;
	b->p = (unsigned char *)NewPtr(n);
	if (b->p)
		return 1;
	b->temp = TempNewHandle(n, &err);
	if (!b->temp)
		return 0;
	HLock(b->temp);
	b->p = (unsigned char *)*b->temp;
	return 1;
}

static void TempBufFree(TempBuf *b)
{
	if (b->temp)
		DisposeHandle(b->temp);
	else if (b->p)
		DisposePtr((Ptr)b->p);
	b->p = NULL;
	b->temp = NULL;
}

int UTMRAVEPixelNeedsTable(TQAImagePixelType type)
{
	return type == kQAPixel_CL4 || type == kQAPixel_CL8 || type == kQAPixel_ACL16_88;
}

static int PixelSupported(TQAImagePixelType type, int isBitmap)
{
	switch (type) {
	case kQAPixel_Alpha1:
		return isBitmap;
	case kQAPixel_RGB16: case kQAPixel_ARGB16: case kQAPixel_RGB32: case kQAPixel_ARGB32:
	case kQAPixel_CL4: case kQAPixel_CL8: case kQAPixel_RGB16_565: case kQAPixel_RGB24:
	case kQAPixel_RGB8_332: case kQAPixel_ARGB16_4444: case kQAPixel_ACL16_88: case kQAPixel_I8:
	case kQAPixel_AI16_88:
		return 1;
	default:
		return 0;
	}
}

/* Bytes per row of `width` pixels with no padding; Alpha1 and CL4 pack. */
static long TightRowBytes(TQAImagePixelType type, long width)
{
	switch (type) {
	case kQAPixel_Alpha1: return (width + 7) / 8;
	case kQAPixel_CL4: return (width + 1) / 2;
	case kQAPixel_CL8: case kQAPixel_RGB8_332: case kQAPixel_I8: return width;
	case kQAPixel_RGB24: return width * 3;
	case kQAPixel_RGB32: case kQAPixel_ARGB32: return width * 4;
	default: return width * 2;
	}
}

/* 5-, 6-, 4-, 3- and 2-bit channels to 8 bits (replicating the top bits). */
#define E5(x) ((unsigned char)(((x) << 3) | ((x) >> 2)))
#define E6(x) ((unsigned char)(((x) << 2) | ((x) >> 4)))
#define E4(x) ((unsigned char)((x) * 17))
#define E3(x) ((unsigned char)(((x) << 5) | ((x) << 2) | ((x) >> 1)))
#define E2(x) ((unsigned char)((x) * 85))

static void PutTable(unsigned char *o, const TQAColorTable *t, int index)
{
	unsigned long e = index < t->entries ? t->rgb[index] : 0;

	o[0] = (unsigned char)(e >> 16);
	o[1] = (unsigned char)(e >> 8);
	o[2] = (unsigned char)e;
	o[3] = (unsigned char)(index == 0 && t->transparentIndex0 ? 0 : 255);
}

/* Converts one image level to RGBA8, top row first. Returns 1 when some
 * pixel is not opaque. `table` is needed for the color-table types. */
static int ConvertLevel(TQAImagePixelType type, const unsigned char *src, long w, long h, long rowBytes,
	const TQAColorTable *table, int flip, unsigned char *out)
{
	long x, y;
	int alpha = 0;

	for (y = 0; y < h; y++) {
		const unsigned char *s = src + (flip ? h - 1 - y : y) * rowBytes;
		unsigned char *o = out + y * w * 4;

		for (x = 0; x < w; x++, o += 4) {
			unsigned v;

			switch (type) {
			case kQAPixel_Alpha1:
				o[0] = o[1] = o[2] = 255;
				o[3] = (s[x >> 3] & (0x80 >> (x & 7))) ? 255 : 0;
				break;
			case kQAPixel_RGB16: case kQAPixel_ARGB16:
				v = (s[x * 2] << 8) | s[x * 2 + 1];
				o[0] = E5((v >> 10) & 31);
				o[1] = E5((v >> 5) & 31);
				o[2] = E5(v & 31);
				o[3] = type == kQAPixel_ARGB16 && !(v & 0x8000) ? 0 : 255;
				break;
			case kQAPixel_RGB16_565:
				v = (s[x * 2] << 8) | s[x * 2 + 1];
				o[0] = E5(v >> 11);
				o[1] = E6((v >> 5) & 63);
				o[2] = E5(v & 31);
				o[3] = 255;
				break;
			case kQAPixel_ARGB16_4444:
				v = (s[x * 2] << 8) | s[x * 2 + 1];
				o[0] = E4((v >> 8) & 15);
				o[1] = E4((v >> 4) & 15);
				o[2] = E4(v & 15);
				o[3] = E4(v >> 12);
				break;
			case kQAPixel_RGB32: case kQAPixel_ARGB32:
				o[0] = s[x * 4 + 1];
				o[1] = s[x * 4 + 2];
				o[2] = s[x * 4 + 3];
				o[3] = type == kQAPixel_ARGB32 ? s[x * 4] : 255;
				break;
			case kQAPixel_RGB24:
				o[0] = s[x * 3];
				o[1] = s[x * 3 + 1];
				o[2] = s[x * 3 + 2];
				o[3] = 255;
				break;
			case kQAPixel_RGB8_332:
				v = s[x];
				o[0] = E3(v >> 5);
				o[1] = E3((v >> 2) & 7);
				o[2] = E2(v & 3);
				o[3] = 255;
				break;
			case kQAPixel_I8:
				o[0] = o[1] = o[2] = s[x];
				o[3] = 255;
				break;
			case kQAPixel_AI16_88:
				o[0] = o[1] = o[2] = s[x * 2 + 1];
				o[3] = s[x * 2];
				break;
			case kQAPixel_CL8:
				PutTable(o, table, s[x]);
				break;
			case kQAPixel_CL4:
				PutTable(o, table, (x & 1) ? s[x >> 1] & 15 : s[x >> 1] >> 4);
				break;
			case kQAPixel_ACL16_88:
				PutTable(o, table, s[x * 2 + 1]);
				o[3] = (unsigned char)((o[3] * s[x * 2] + 127) / 255);	/* palette transparency times the alpha byte */
				break;
			default:
				o[0] = o[1] = o[2] = o[3] = 0;
				break;
			}
			if (o[3] != 255)
				alpha = 1;
		}
	}
	return alpha;
}

/* Uploads every level of `img` into its host texture. The shared context
 * must be current. `images` is the application's data, or NULL to use the
 * engine's copies (img->raw, tight rows). */
static int UploadImage(UTMImage *img, const TQAImage *images)
{
	int level;
	int hasAlpha = 0;

	if (!img->name) {
		gGL.glGenTextures(1, &img->name);
		if (!img->name) {
			UTMRAVELog("glGenTextures returned no name");
			return 0;
		}
	}
	gGL.glBindTexture(GL_TEXTURE_2D, img->name);
	gGL.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, img->levels - 1);
	gGL.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, img->levels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
	gGL.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	for (level = 0; level < img->levels; level++) {
		long w = img->width >> level, h = img->height >> level;
		const unsigned char *src;
		long rowBytes;
		TempBuf buf;

		if (w < 1)
			w = 1;
		if (h < 1)
			h = 1;
		if (images) {
			src = (const unsigned char *)images[level].pixmap;
			rowBytes = images[level].rowBytes;
		} else {
			src = img->raw[level];
			rowBytes = TightRowBytes(img->pixelType, w);
		}
		if (!TempBufNew(&buf, w * h * 4)) {
			UTMRAVELog("texture level %d: out of memory for %ld x %ld", level, w, h);
			return 0;
		}
		if (ConvertLevel(img->pixelType, src, w, h, rowBytes, img->table, img->flip, buf.p))
			hasAlpha = 1;
		gGL.glTexImage2D(GL_TEXTURE_2D, level, GL_RGBA, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_UNSIGNED_BYTE, buf.p);
		TempBufFree(&buf);
	}
	img->hasAlpha = hasAlpha;
	img->uploaded = 1;
	img->paramsValid = 0;
	return 1;
}

/* Runs `UploadImage` with the shared context current, restoring the caller's. */
static int UploadShared(UTMImage *img, const TQAImage *images)
{
	AGLContext shared = UTMRAVEShared(), prev = UTMRAVECurrentContext();
	int ok;

	if (!shared)
		return 0;
	UTMRAVEMakeCurrent(shared);
	ok = UploadImage(img, images);
	UTMRAVEMakeCurrent(prev);
	return ok;
}

static TQAError NewImage(int isBitmap, unsigned long flags, TQAImagePixelType pixelType, const TQAImage *images,
	UTMImage **out)
{
	UTMImage *img;
	long w, h, size;
	int levels = 1, i;
	int mipmap = !isBitmap && (flags & kQATexture_Mipmap);
	int flip = isBitmap ? (flags & kQABitmap_FlipOrigin) != 0 : (flags & kQATexture_FlipOrigin) != 0;

	*out = NULL;
	if (!images || !images[0].pixmap)
		return kQAParamErr;
	if (!PixelSupported(pixelType, isBitmap)) {
		if (UTMRAVEFirst(kUTMFirstPixelType, pixelType))
			UTMRAVELog("%s: pixel type %d not supported", isBitmap ? "BitmapNew" : "TextureNew", (int)pixelType);
		return kQANotSupported;
	}
	w = images[0].width;
	h = images[0].height;
	if (w < 1 || h < 1 || w > MAX_TEXTURE_SIZE || h > MAX_TEXTURE_SIZE) {
		UTMRAVELog("%s: bad size %ld x %ld", isBitmap ? "BitmapNew" : "TextureNew", w, h);
		return kQAParamErr;
	}
	if (mipmap) {
		for (size = w > h ? w : h; size > 1; size >>= 1)
			levels++;
		for (i = 0; i < levels; i++)
			if (!images[i].pixmap)
				return kQAParamErr;
	}
	if (!UTMRAVEShared())
		return kQAError;
	img = (UTMImage *)NewPtrClear(sizeof *img);
	if (!img)
		return kQAOutOfMemory;
	img->width = w;
	img->height = h;
	img->levels = levels;
	img->mipmapped = mipmap != 0;
	img->isBitmap = isBitmap;
	img->flip = flip;
	img->pixelType = pixelType;
	if (UTMRAVEPixelNeedsTable(pixelType)) {
		/* keep the indices until a color table is bound */
		for (i = 0; i < levels; i++) {
			long lw = w >> i, lh = h >> i, rb, y;

			if (lw < 1)
				lw = 1;
			if (lh < 1)
				lh = 1;
			rb = TightRowBytes(pixelType, lw);
			img->raw[i] = (unsigned char *)NewPtr(rb * lh);
			if (!img->raw[i]) {
				UTMRAVETextureDelete((TQATexture *)img);
				return kQAOutOfMemory;
			}
			for (y = 0; y < lh; y++)
				memcpy(img->raw[i] + y * rb, (const unsigned char *)images[i].pixmap + y * images[i].rowBytes, rb);
		}
	} else if (!UploadShared(img, images)) {
		UTMRAVETextureDelete((TQATexture *)img);
		return kQAOutOfMemory;
	}
	*out = img;
	return kQANoErr;
}

TQAError UTMRAVETextureNew(unsigned long flags, TQAImagePixelType pixelType, const TQAImage images[],
	TQATexture **newTexture)
{
	UTMImage *img;
	TQAError err = NewImage(0, flags, pixelType, images, &img);

	*newTexture = (TQATexture *)img;
	return err;
}

TQAError UTMRAVEBitmapNew(unsigned long flags, TQAImagePixelType pixelType, const TQAImage *image,
	TQABitmap **newBitmap)
{
	UTMImage *img;
	TQAError err = NewImage(1, flags, pixelType, image, &img);

	*newBitmap = (TQABitmap *)img;
	return err;
}

/* The load is complete at New, or at BindColorTable for the color-table
 * types (see the file comment): nothing is left to copy. */
static TQAError DetachImage(UTMImage *img)
{
	return img ? kQANoErr : kQAParamErr;
}

TQAError UTMRAVETextureDetach(TQATexture *texture) { return DetachImage((UTMImage *)texture); }
TQAError UTMRAVEBitmapDetach(TQABitmap *bitmap) { return DetachImage((UTMImage *)bitmap); }

static void DeleteImage(UTMImage *img)
{
	int i;

	if (!img)
		return;
	if (img->name && UTMRAVEShared()) {
		AGLContext prev = UTMRAVECurrentContext();

		UTMRAVEMakeCurrent(UTMRAVEShared());
		gGL.glDeleteTextures(1, &img->name);
		UTMRAVEMakeCurrent(prev);
	}
	for (i = 0; i < UTM_MAX_LEVELS; i++)
		if (img->raw[i])
			DisposePtr((Ptr)img->raw[i]);
	DisposePtr((Ptr)img);
}

void UTMRAVETextureDelete(TQATexture *texture) { DeleteImage((UTMImage *)texture); }
void UTMRAVEBitmapDelete(TQABitmap *bitmap) { DeleteImage((UTMImage *)bitmap); }

TQAError UTMRAVEColorTableNew(TQAColorTableType type, void *pixelData, long transparentIndex, TQAColorTable **newTable)
{
	TQAColorTable *t;
	const unsigned char *p = (const unsigned char *)pixelData;
	int i, n;

	*newTable = NULL;
	if (!pixelData)
		return kQAParamErr;
	if (type == kQAColorTable_CL8_RGB32)
		n = 256;
	else if (type == kQAColorTable_CL4_RGB32)
		n = 16;
	else {
		UTMRAVELog("ColorTableNew: type %d not supported", (int)type);
		return kQANotSupported;
	}
	t = (TQAColorTable *)NewPtrClear(sizeof *t);
	if (!t)
		return kQAOutOfMemory;
	t->entries = n;
	t->transparentIndex0 = transparentIndex != 0;
	for (i = 0; i < n; i++)		/* big-endian 0x00RRGGBB */
		t->rgb[i] = ((unsigned long)p[i * 4 + 1] << 16) | ((unsigned long)p[i * 4 + 2] << 8) | p[i * 4 + 3];
	*newTable = t;
	return kQANoErr;
}

void UTMRAVEColorTableDelete(TQAColorTable *table)
{
	if (table)
		DisposePtr((Ptr)table);
}

/* Converts the kept indices through the table and uploads. The table is only
 * used during the upload: it may be deleted afterwards, and binding another
 * one re-uploads from the kept indices. */
static TQAError BindTable(UTMImage *img, TQAColorTable *table)
{
	int ok;

	if (!img || !table || !UTMRAVEPixelNeedsTable(img->pixelType))
		return kQAParamErr;
	img->table = table;
	ok = UploadShared(img, NULL);
	img->table = NULL;
	return ok ? kQANoErr : kQAOutOfMemory;
}

TQAError UTMRAVETextureBindColorTable(TQATexture *texture, TQAColorTable *table)
{
	return BindTable((UTMImage *)texture, table);
}

TQAError UTMRAVEBitmapBindColorTable(TQABitmap *bitmap, TQAColorTable *table)
{
	return BindTable((UTMImage *)bitmap, table);
}

/* ---- sampler state */

void UTMRAVEImageParams(UTMImage *img, GLint wrapS, GLint wrapT, GLint mag, GLint min, const float border[4])
{
	int borderChanged = !img->paramsValid || memcmp(img->border, border, sizeof img->border) != 0;

	if (!img->paramsValid || img->wrapS != wrapS)
		gGL.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
	if (!img->paramsValid || img->wrapT != wrapT)
		gGL.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
	if (!img->paramsValid || img->mag != mag)
		gGL.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag);
	if (!img->paramsValid || img->min != min)
		gGL.glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min);
	if (borderChanged)
		gGL.glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
	img->wrapS = wrapS;
	img->wrapT = wrapT;
	img->mag = mag;
	img->min = min;
	memcpy(img->border, border, sizeof img->border);
	img->paramsValid = 1;
}

/* ---- direct access to texture / bitmap memory: not implemented (the
 * images live on the host); not claimed in gestalt. */

static TQAError NoAccess(int id, const char *what)
{
	if (UTMRAVEFirst(kUTMFirstMethod, 100 + id))
		UTMRAVELog("%s not supported", what);
	return kQANotSupported;
}

TQAError UTMRAVEAccessTexture(TQATexture *texture, long mipmapLevel, long flags, TQAPixelBuffer *buffer)
{
	(void)texture; (void)mipmapLevel; (void)flags; (void)buffer;
	return NoAccess(0, "QAAccessTexture");
}

TQAError UTMRAVEAccessTextureEnd(TQATexture *texture, const TQARect *dirtyRect)
{
	(void)texture; (void)dirtyRect;
	return NoAccess(1, "QAAccessTextureEnd");
}

TQAError UTMRAVEAccessBitmap(TQABitmap *bitmap, long flags, TQAPixelBuffer *buffer)
{
	(void)bitmap; (void)flags; (void)buffer;
	return NoAccess(2, "QAAccessBitmap");
}

TQAError UTMRAVEAccessBitmapEnd(TQABitmap *bitmap, const TQARect *dirtyRect)
{
	(void)bitmap; (void)dirtyRect;
	return NoAccess(3, "QAAccessBitmapEnd");
}
