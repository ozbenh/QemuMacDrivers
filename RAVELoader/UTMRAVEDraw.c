/* UTM RAVE: drawing methods (points, lines, triangles, vertex arrays, meshes,
 * bitmaps).
 *
 * Vertex mapping. RAVE x, y are pixels relative to the draw context (y down)
 * and the projection is glOrtho(0, w, h, 0, 0, -1) (UTMRAVEContext.c), so
 * x, y go to GL unchanged. Depth, z in [0, 1]:
 *  - kQAPerspectiveZ_Off: GL depth = z.
 *  - kQAPerspectiveZ_On: GL depth = 1 / (1 + invW). invW = 1/w grows towards
 *    the viewer, so the depth falls with it (nearer = smaller, as with z, so
 *    the ZFunction values keep their meaning); it is monotonic, stays in
 *    (0, 1] for any invW >= 0 and has full precision where invW is small
 *    (far), where perspective z-buffers need it. A negative invW is taken as 0.
 * Texture coordinates. glTexCoord4f(u/w, (1-v)/w, 0, 1/w) with screen-space
 * vertices: GL interpolates the four values linearly in screen space and
 * divides s/q, t/q per pixel, which is the perspective-correct u, v RAVE
 * wants. t = 1 - v because RAVE's v = 0 is the bottom of the image while
 * images are uploaded top row first (the mapping Apple's Classic RAVE uses).
 *
 * Texture operations (kQATag_TextureOp), from RAVE.h's pseudo-code:
 *  - none:       texture color; alpha = texel alpha * vertex a.
 *                (GL_MODULATE with vertex color (1, 1, 1, a)).
 *  - Modulate:   texture color * kd (GL_MODULATE, vertex color (kd, a)). GL
 *                clamps the vertex color to 1, RAVE only to >= 0.
 *  - Highlight:  + ks, through the secondary color and GL_COLOR_SUM.
 *  - Decal:      texel alpha mixes the texture over the vertex rgb, alpha =
 *                vertex a (GL_DECAL with vertex color (r, g, b, a)). Decal
 *                together with Modulate would need GL_COMBINE; the kd factor
 *                is ignored then (logged once).
 *  - Blend:      GL_BLEND texture environment with kQATagGL_TextureEnvColor.
 *  - Shrink:     a non-wrapping texture: clamped unless GL wrap tags are set.
 * Transparency. Blending and the alpha test are always on, as in Apple's
 * Classic RAVE: fragments with alpha 0 are discarded (GL_GREATER 0 unless
 * the application sets kQATag_AlphaTestFunc). Blend functions: PreMultiply (ONE,
 * ONE_MINUS_SRC_ALPHA), Interpolate (SRC_ALPHA, ONE_MINUS_SRC_ALPHA). The
 * destination alpha channel follows the same factors (RAVE's formulas for A
 * differ slightly for Interpolate); it is only visible in memory devices.
 * Fog, multitexture, antialiasing, chroma keying and CSG are not implemented
 * and not claimed in gestalt. */
#include <MacTypes.h>
#include <Memory.h>
#include <string.h>
#include "UTMRAVEEngine.h"
#include "UTMRAVELog.h"

static float DepthOf(const TQADrawPrivate *c, float z, float invW)
{
	if (c->state[kQATag_PerspectiveZ].i == kQAPerspectiveZ_On) {
		if (invW < 0.0f)
			invW = 0.0f;
		return 1.0f / (1.0f + invW);
	}
	return z;
}

static void EmitG(const TQADrawPrivate *c, const TQAVGouraud *v)
{
	gGL.glColor4f(v->r, v->g, v->b, v->a);
	gGL.glVertex3f(v->x, v->y, DepthOf(c, v->z, v->invW));
}

static void EmitT(const TQADrawPrivate *c, const TQAVTexture *v)
{
	if (c->texDecal)
		gGL.glColor4f(v->r, v->g, v->b, v->a);
	else if (c->texModulate)
		gGL.glColor4f(v->kd_r, v->kd_g, v->kd_b, v->a);
	else
		gGL.glColor4f(1.0f, 1.0f, 1.0f, v->a);
	if (c->texHighlight)
		gGL.glSecondaryColor3fEXT(v->ks_r, v->ks_g, v->ks_b);
	gGL.glTexCoord4f(v->uOverW, v->invW - v->vOverW, 0.0f, v->invW);	/* t = 1 - v, see the file comment */
	gGL.glVertex3f(v->x, v->y, DepthOf(c, v->z, v->invW));
}

/* Blending is always on, as in Apple's Classic RAVE (RAVE on OpenGL), which
 * enables GL_BLEND once per context and only switches the blend function with
 * kQATag_Blend. Opaque fragments (alpha 1) come out unchanged under both
 * PreMultiply (ONE, ONE_MINUS_SRC_ALPHA) and Interpolate (SRC_ALPHA,
 * ONE_MINUS_SRC_ALPHA). */
static void SetBlending(TQADrawPrivate *c)
{
	UTMRAVESetCap(c, kUTMCapBlend, GL_BLEND, 1);
}

static void SetEnvMode(TQADrawPrivate *c, GLint mode)
{
	if (c->envMode != mode) {
		gGL.glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
		c->envMode = mode;
	}
}

/* State common to every primitive; untextured primitives switch texturing off. */
static void PrepareGouraud(TQADrawPrivate *c)
{
	UTMRAVEMakeCurrent(c->agl);
	UTMRAVEApplyState(c);
	UTMRAVESetCap(c, kUTMCapTexture, GL_TEXTURE_2D, 0);
	UTMRAVESetCap(c, kUTMCapColorSum, GL_COLOR_SUM, 0);
}

/* A GL_TEXTURE_MIN/MAG_FILTER value from a kQATagGL_TextureMag/MinFilter
 * value: small values are Nearest (0) / Linear (1), larger ones are OpenGL
 * enums. Mipmap filters fall back to their base filter on textures without
 * mipmaps (and always for magnification). */
static GLint GLFilter(unsigned long v, int mipmapped)
{
	GLint f;

	if (v == 0)
		f = GL_NEAREST;
	else if (v == 1)
		f = GL_LINEAR;
	else if (v >= GL_NEAREST && v <= GL_LINEAR_MIPMAP_LINEAR)
		f = (GLint)v;
	else {
		if (UTMRAVEFirst(kUTMFirstMisc, 40 + (v & 15)))
			UTMRAVELog("texture filter value %lx is not understood, using linear", v);
		f = GL_LINEAR;
	}
	if (!mipmapped) {
		if (f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR)
			f = GL_NEAREST;
		else if (f == GL_LINEAR_MIPMAP_NEAREST || f == GL_LINEAR_MIPMAP_LINEAR)
			f = GL_LINEAR;
	}
	return f;
}

static GLint MagFilter(GLint f)
{
	return f == GL_NEAREST || f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR ? GL_NEAREST : GL_LINEAR;
}

/* kQAGL_Repeat / kQAGL_Clamp, or the OpenGL wrap enums. Clamp is
 * CLAMP_TO_EDGE (no seam from the border), or CLAMP_TO_BORDER when a border
 * color was set. */
static GLint WrapMode(unsigned long v, int borderSet)
{
	if (v == kQAGL_Clamp || (v >= GL_CLAMP && v != GL_REPEAT))
		return borderSet ? GL_CLAMP_TO_BORDER : GL_CLAMP_TO_EDGE;
	return GL_REPEAT;
}

/* Texture state for textured primitives. Returns 0 when there is nothing to
 * draw with (logged once). */
static int PrepareTexture(TQADrawPrivate *c)
{
	UTMImage *img = (UTMImage *)c->state[kQATag_Texture].p;
	unsigned long op = c->state[kQATag_TextureOp].i;
	GLint mag, min, mode;
	float border[4];
	float bias;
	int borderSet;

	UTMRAVEMakeCurrent(c->agl);
	UTMRAVEApplyState(c);
	if (!img || img->isBitmap || !img->uploaded) {
		if (UTMRAVEFirst(kUTMFirstMisc, 50 + (img ? 1 : 0)))
			UTMRAVELog("textured primitive without a usable texture (%s): not drawn",
				img ? "no image uploaded, color table not bound?" : "kQATag_Texture is NULL");
		return 0;
	}
	c->texDecal = (op & kQATextureOp_Decal) != 0;
	c->texModulate = (op & kQATextureOp_Modulate) != 0;
	c->texHighlight = (op & kQATextureOp_Highlight) != 0;
	if (c->texDecal && c->texModulate && UTMRAVEFirst(kUTMFirstMisc, 52))
		UTMRAVELog("TextureOp Decal + Modulate: the kd factor is ignored");
	UTMRAVESetCap(c, kUTMCapTexture, GL_TEXTURE_2D, 1);
	UTMRAVESetCap(c, kUTMCapColorSum, GL_COLOR_SUM, c->texHighlight);
	gGL.glBindTexture(GL_TEXTURE_2D, img->name);

	border[0] = c->state[kQATagGL_TextureBorder_r].f;
	border[1] = c->state[kQATagGL_TextureBorder_g].f;
	border[2] = c->state[kQATagGL_TextureBorder_b].f;
	border[3] = c->state[kQATagGL_TextureBorder_a].f;
	borderSet = border[0] != 0.0f || border[1] != 0.0f || border[2] != 0.0f || border[3] != 0.0f;
	if (c->glFilterSet) {
		min = GLFilter(c->state[kQATagGL_TextureMinFilter].i, img->mipmapped);
		mag = MagFilter(GLFilter(c->state[kQATagGL_TextureMagFilter].i, img->mipmapped));
	} else {
		switch (c->state[kQATag_TextureFilter].i) {
		case kQATextureFilter_Best:
			mag = GL_LINEAR;
			min = img->mipmapped ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
			break;
		case kQATextureFilter_Mid:
			mag = GL_LINEAR;
			min = img->mipmapped ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
			break;
		default:			/* kQATextureFilter_Fast: nearest */
			mag = GL_NEAREST;
			min = img->mipmapped ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
			break;
		}
	}
	if (c->glWrapSet)
		UTMRAVEImageParams(img, WrapMode(c->state[kQATagGL_TextureWrapU].i, borderSet),
			WrapMode(c->state[kQATagGL_TextureWrapV].i, borderSet), mag, min, border);
	else {
		/* As Apple's Classic RAVE: without explicit GL wrap tags, a Shrink
		 * texture (non-wrapping, coordinates within 0..1) is clamped and any
		 * other one repeats. Classic RAVE uses GL_CLAMP; CLAMP_TO_EDGE keeps
		 * the intent without blending in the (black) border colour, which
		 * drew dark seams between QuickDraw 3D's terrain tiles. */
		GLint wrap = (op & kQATextureOp_Shrink) ? GL_CLAMP_TO_EDGE : GL_REPEAT;

		UTMRAVEImageParams(img, wrap, wrap, mag, min, border);
	}

	mode = c->texDecal ? GL_DECAL : ((op & kQATextureOp_Blend) ? GL_BLEND : GL_MODULATE);
	SetEnvMode(c, mode);
	if (mode == GL_BLEND) {
		float ec[4];

		ec[0] = c->state[kQATagGL_TextureEnvColor_r].f;
		ec[1] = c->state[kQATagGL_TextureEnvColor_g].f;
		ec[2] = c->state[kQATagGL_TextureEnvColor_b].f;
		ec[3] = c->state[kQATagGL_TextureEnvColor_a].f;
		if (memcmp(ec, c->envColor, sizeof ec) != 0) {
			gGL.glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, ec);
			memcpy(c->envColor, ec, sizeof ec);
		}
	}
	bias = c->state[kQATag_MipmapBias].f;
	if (bias != c->lodBias) {
		gGL.glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_LOD_BIAS, bias);
		c->lodBias = bias;
	}
	return 1;
}

static int VertexModeToGL(TQAVertexMode mode, GLenum *gl)
{
	switch (mode) {
	case kQAVertexMode_Point: *gl = GL_POINTS; return 1;
	case kQAVertexMode_Line: *gl = GL_LINES; return 1;
	case kQAVertexMode_Polyline: *gl = GL_LINE_STRIP; return 1;
	case kQAVertexMode_Tri: *gl = GL_TRIANGLES; return 1;
	case kQAVertexMode_Strip: *gl = GL_TRIANGLE_STRIP; return 1;
	case kQAVertexMode_Fan: *gl = GL_TRIANGLE_FAN; return 1;
	default:
		if (UTMRAVEFirst(kUTMFirstMisc, 60 + (unsigned long)mode))
			UTMRAVELog("vertex mode %d is not supported", (int)mode);
		return 0;
	}
}

/* ---- Gouraud */

static void DrawPoint(const TQADrawContext *dc, const TQAVGouraud *v)
{
	TQADrawPrivate *c = dc->drawPrivate;

	PrepareGouraud(c);
	SetBlending(c);
	gGL.glBegin(GL_POINTS);
	EmitG(c, v);
	gGL.glEnd();
}

static void DrawLine(const TQADrawContext *dc, const TQAVGouraud *v0, const TQAVGouraud *v1)
{
	TQADrawPrivate *c = dc->drawPrivate;

	PrepareGouraud(c);
	SetBlending(c);
	gGL.glBegin(GL_LINES);
	EmitG(c, v0);
	EmitG(c, v1);
	gGL.glEnd();
}

static void DrawTriGouraud(const TQADrawContext *dc, const TQAVGouraud *v0, const TQAVGouraud *v1,
	const TQAVGouraud *v2, unsigned long flags)
{
	TQADrawPrivate *c = dc->drawPrivate;

	(void)flags;			/* RAVE does not cull: kQATriFlags_Backfacing is informational */
	PrepareGouraud(c);
	SetBlending(c);
	gGL.glBegin(GL_TRIANGLES);
	EmitG(c, v0);
	EmitG(c, v1);
	EmitG(c, v2);
	gGL.glEnd();
}

static void DrawVGouraud(const TQADrawContext *dc, unsigned long n, TQAVertexMode mode, const TQAVGouraud vertices[],
	const unsigned long flags[])
{
	TQADrawPrivate *c = dc->drawPrivate;
	GLenum gl;
	unsigned long i;

	(void)flags;
	if (!n || !vertices || !VertexModeToGL(mode, &gl))
		return;
	PrepareGouraud(c);
	SetBlending(c);
	gGL.glBegin(gl);
	for (i = 0; i < n; i++)
		EmitG(c, &vertices[i]);
	gGL.glEnd();
}

/* ---- textured */

static void DrawTriTexture(const TQADrawContext *dc, const TQAVTexture *v0, const TQAVTexture *v1,
	const TQAVTexture *v2, unsigned long flags)
{
	TQADrawPrivate *c = dc->drawPrivate;

	(void)flags;
	if (!PrepareTexture(c))
		return;
	SetBlending(c);
	gGL.glBegin(GL_TRIANGLES);
	EmitT(c, v0);
	EmitT(c, v1);
	EmitT(c, v2);
	gGL.glEnd();
}

static void DrawVTexture(const TQADrawContext *dc, unsigned long n, TQAVertexMode mode, const TQAVTexture vertices[],
	const unsigned long flags[])
{
	TQADrawPrivate *c = dc->drawPrivate;
	GLenum gl;
	unsigned long i;

	(void)flags;
	if (!n || !vertices || !VertexModeToGL(mode, &gl) || !PrepareTexture(c))
		return;
	SetBlending(c);
	gGL.glBegin(gl);
	for (i = 0; i < n; i++)
		EmitT(c, &vertices[i]);
	gGL.glEnd();
}

/* ---- triangle meshes: the vertices are copied at Submit (the application may
 * reuse its array), the triangles index into the copy. */

static void SubmitVerticesGouraud(const TQADrawContext *dc, unsigned long n, const TQAVGouraud *vertices)
{
	TQADrawPrivate *c = dc->drawPrivate;

	c->meshGCount = 0;
	if (!n || !vertices)
		return;
	if (n > c->meshGCap) {
		TQAVGouraud *mem = (TQAVGouraud *)NewPtr((long)(n * sizeof *mem));

		if (!mem) {
			UTMRAVELog("SubmitVerticesGouraud: out of memory for %lu vertices", n);
			return;
		}
		if (c->meshG)
			DisposePtr((Ptr)c->meshG);
		c->meshG = mem;
		c->meshGCap = n;
	}
	memcpy(c->meshG, vertices, n * sizeof *vertices);
	c->meshGCount = n;
}

static void SubmitVerticesTexture(const TQADrawContext *dc, unsigned long n, const TQAVTexture *vertices)
{
	TQADrawPrivate *c = dc->drawPrivate;

	c->meshTCount = 0;
	if (!n || !vertices)
		return;
	if (n > c->meshTCap) {
		TQAVTexture *mem = (TQAVTexture *)NewPtr((long)(n * sizeof *mem));

		if (!mem) {
			UTMRAVELog("SubmitVerticesTexture: out of memory for %lu vertices", n);
			return;
		}
		if (c->meshT)
			DisposePtr((Ptr)c->meshT);
		c->meshT = mem;
		c->meshTCap = n;
	}
	memcpy(c->meshT, vertices, n * sizeof *vertices);
	c->meshTCount = n;
}

static void DrawTriMeshGouraud(const TQADrawContext *dc, unsigned long nTriangles, const TQAIndexedTriangle *tris)
{
	TQADrawPrivate *c = dc->drawPrivate;
	unsigned long i, skipped = 0;

	if (!nTriangles || !tris || !c->meshGCount)
		return;
	PrepareGouraud(c);
	SetBlending(c);
	gGL.glBegin(GL_TRIANGLES);
	for (i = 0; i < nTriangles; i++) {
		if (tris[i].vertices[0] >= c->meshGCount || tris[i].vertices[1] >= c->meshGCount ||
		    tris[i].vertices[2] >= c->meshGCount) {
			skipped++;
			continue;
		}
		EmitG(c, &c->meshG[tris[i].vertices[0]]);
		EmitG(c, &c->meshG[tris[i].vertices[1]]);
		EmitG(c, &c->meshG[tris[i].vertices[2]]);
	}
	gGL.glEnd();
	if (skipped && UTMRAVEFirst(kUTMFirstMisc, 70))
		UTMRAVELog("DrawTriMeshGouraud: %lu triangles index past the %lu submitted vertices", skipped, c->meshGCount);
}

static void DrawTriMeshTexture(const TQADrawContext *dc, unsigned long nTriangles, const TQAIndexedTriangle *tris)
{
	TQADrawPrivate *c = dc->drawPrivate;
	unsigned long i, skipped = 0;

	if (!nTriangles || !tris || !c->meshTCount || !PrepareTexture(c))
		return;
	SetBlending(c);
	gGL.glBegin(GL_TRIANGLES);
	for (i = 0; i < nTriangles; i++) {
		if (tris[i].vertices[0] >= c->meshTCount || tris[i].vertices[1] >= c->meshTCount ||
		    tris[i].vertices[2] >= c->meshTCount) {
			skipped++;
			continue;
		}
		EmitT(c, &c->meshT[tris[i].vertices[0]]);
		EmitT(c, &c->meshT[tris[i].vertices[1]]);
		EmitT(c, &c->meshT[tris[i].vertices[2]]);
	}
	gGL.glEnd();
	if (skipped && UTMRAVEFirst(kUTMFirstMisc, 71))
		UTMRAVELog("DrawTriMeshTexture: %lu triangles index past the %lu submitted vertices", skipped, c->meshTCount);
}

static void SubmitMultiTextureParams(const TQADrawContext *dc, unsigned long n, const TQAVMultiTexture *params)
{
	(void)dc; (void)n; (void)params;
	if (UTMRAVEFirst(kUTMFirstMethod, 80))
		UTMRAVELog("QASubmitMultiTextureParams: multitexture is not implemented");
}

/* ---- bitmaps */

/* A bitmap is a textured quad at (x, y), scaled by kQATag_BitmapScale_x/y,
 * nearest-filtered unless kQATag_BitmapFilter asks for filtering. A 1-bit
 * bitmap takes its color from the vertex (argb); other pixel types draw their
 * own colors. */
static void DrawBitmap(const TQADrawContext *dc, const TQAVGouraud *v, TQABitmap *bitmap)
{
	TQADrawPrivate *c = dc->drawPrivate;
	UTMImage *img = (UTMImage *)bitmap;
	static const float noBorder[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float sx = c->state[kQATag_BitmapScale_x].f, sy = c->state[kQATag_BitmapScale_y].f;
	float w, h, z;
	GLint filter;
	int alpha1;

	if (!img || !img->isBitmap || !img->uploaded) {
		if (UTMRAVEFirst(kUTMFirstMisc, 81))
			UTMRAVELog("QADrawBitmap: no usable bitmap (color table not bound?)");
		return;
	}
	UTMRAVEMakeCurrent(c->agl);
	UTMRAVEApplyState(c);
	alpha1 = img->pixelType == kQAPixel_Alpha1;
	w = (float)img->width * sx;
	h = (float)img->height * sy;
	z = DepthOf(c, v->z, v->invW);
	filter = (sx != 1.0f || sy != 1.0f) && c->state[kQATag_BitmapFilter].i != kQAFilter_Fast ? GL_LINEAR : GL_NEAREST;
	UTMRAVESetCap(c, kUTMCapTexture, GL_TEXTURE_2D, 1);
	UTMRAVESetCap(c, kUTMCapColorSum, GL_COLOR_SUM, 0);
	gGL.glBindTexture(GL_TEXTURE_2D, img->name);
	UTMRAVEImageParams(img, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, filter, filter, noBorder);
	SetEnvMode(c, GL_MODULATE);
	SetBlending(c);
	if (alpha1)
		gGL.glColor4f(v->r, v->g, v->b, v->a);
	else
		gGL.glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	gGL.glBegin(GL_QUADS);
	gGL.glTexCoord2f(0.0f, 0.0f);
	gGL.glVertex3f(v->x, v->y, z);
	gGL.glTexCoord2f(1.0f, 0.0f);
	gGL.glVertex3f(v->x + w, v->y, z);
	gGL.glTexCoord2f(1.0f, 1.0f);
	gGL.glVertex3f(v->x + w, v->y + h, z);
	gGL.glTexCoord2f(0.0f, 1.0f);
	gGL.glVertex3f(v->x, v->y + h, z);
	gGL.glEnd();
}

void UTMRAVEInstallDrawMethods(TQADrawContext *dc)
{
	dc->drawPoint = DrawPoint;
	dc->drawLine = DrawLine;
	dc->drawTriGouraud = DrawTriGouraud;
	dc->drawTriTexture = DrawTriTexture;
	dc->drawVGouraud = DrawVGouraud;
	dc->drawVTexture = DrawVTexture;
	dc->drawBitmap = DrawBitmap;
	dc->submitVerticesGouraud = SubmitVerticesGouraud;
	dc->submitVerticesTexture = SubmitVerticesTexture;
	dc->drawTriMeshGouraud = DrawTriMeshGouraud;
	dc->drawTriMeshTexture = DrawTriMeshTexture;
	dc->submitMultiTextureParams = SubmitMultiTextureParams;
}
